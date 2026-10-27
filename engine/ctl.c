/* SPDX-License-Identifier: GPL-3.0-or-later
 * Control socket: one text line in, one JSON object out, or `watch` to keep
 * the connection and receive events as JSON lines. Mode 0600 and the peer's uid
 * checked, unless a group is named (cfg.ctl_group): then the socket is mode 0660 in
 * that group and the file mode is the check (ADR 0032). Keys never cross it. */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "int.h"

int ctl_open(lw_node *n, char *err, size_t errn)
{
	struct sockaddr_un a;
	mode_t old;
	int fd;

	if (!n->cfg.ctl_path[0]) {
		n->ctl_fd = -1;
		return 0;
	}
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", n->cfg.ctl_path);
	if (fd >= 0 && connect(fd, (struct sockaddr *)&a, sizeof a) == 0) {	/* somebody answers */
		snprintf(err, errn, "a node is already running (%s); `longwire down` first", a.sun_path);
		close(fd);
		return -1;
	}
	if (fd >= 0) {			/* a stale socket file from a dead node */
		close(fd);
		fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	}
	unlink(a.sun_path);
	old = umask(0177);
	if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 8) < 0) {
		umask(old);
		snprintf(err, errn, "control socket %s: %s", a.sun_path, strerror(errno));
		if (fd >= 0)
			close(fd);
		return -1;
	}
	umask(old);
	if (n->cfg.ctl_group &&
	    (chown(a.sun_path, (uid_t)-1, (gid_t)n->cfg.ctl_gid) < 0 || chmod(a.sun_path, 0660) < 0)) {
		snprintf(err, errn, "control socket %s: cannot give it to group %u: %s", a.sun_path, n->cfg.ctl_gid, strerror(errno));
		close(fd);
		unlink(a.sun_path);
		return -1;
	}
	n->ctl_fd = fd;
	{
		int i;

		for (i = 0; i < MAXSUBS; i++)
			n->subs[i] = -1;
	}
	return 0;
}

static void ev_send(lw_node *n, int owner_only, const char *fmt, va_list ap)
{
	char b[4096];
	int l, i;

	l = vsnprintf(b, sizeof b - 1, fmt, ap);
	if (l <= 0)
		return;
	if (l > (int)sizeof b - 2)
		l = (int)sizeof b - 2;
	b[l++] = '\n';
	for (i = 0; i < MAXSUBS; i++) {
		if (n->subs[i] < 0 || (owner_only && !n->subs_owner[i]))
			continue;
		ssize_t w = send(n->subs[i], b, (size_t)l, MSG_DONTWAIT | MSG_NOSIGNAL);

		if ((w < 0 && errno != EAGAIN) || (w >= 0 && w < l)) {	/* a line cut short would run into the next one: that watcher starts again */
			close(n->subs[i]);
			n->subs[i] = -1;
		}
	}
}

void ev_emit(lw_node *n, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ev_send(n, 0, fmt, ap);
	va_end(ap);
}

/* For what only the owner of the worker is told: a registration's token and address, which a group member watching must not see. */
void ev_emit_owner(lw_node *n, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ev_send(n, 1, fmt, ap);
	va_end(ap);
}

void ctl_subs_reap(lw_node *n)
{
	char tmp[64];
	int i;

	for (i = 0; i < MAXSUBS; i++) {
		ssize_t r;

		if (n->subs[i] < 0)
			continue;
		r = recv(n->subs[i], tmp, sizeof tmp, MSG_DONTWAIT);
		if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
			close(n->subs[i]);
			n->subs[i] = -1;
		}
	}
}

void ctl_accept(lw_node *n)
{
	struct ucred cr;
	socklen_t cl = sizeof cr;
	char line[400], out[8192];
	struct pollfd pf;
	int c = accept4(n->ctl_fd, 0, 0, SOCK_CLOEXEC), len, i;
	ssize_t r;

	if (c < 0)
		return;
	if (getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cr, &cl) < 0 || (!n->cfg.ctl_group && cr.uid != geteuid())) {
		close(c);
		return;
	}
	pf.fd = c;
	pf.events = POLLIN;
	if (poll(&pf, 1, 50) <= 0 || (r = read(c, line, sizeof line - 1)) <= 0) {
		close(c);
		return;
	}
	line[r] = 0;
	while (r > 0 && (line[r - 1] == '\n' || line[r - 1] == '\r'))
		line[--r] = 0;
	if (!strcmp(line, "watch")) {
		for (i = 0; i < MAXSUBS; i++)
			if (n->subs[i] < 0)
				break;
		if (i == MAXSUBS) {
			close(c);
			return;
		}
		n->subs[i] = c;
		n->subs_owner[i] = cr.uid == geteuid();
		len = snprintf(out, sizeof out, "{\"event\":\"watching\",\"role\":\"%s\"}\n", n->role ? "switchboard" : "node");
		(void)!write(c, out, (size_t)len);
		if (!n->role && (len = node_replay(n, out, sizeof out)) > 0)
			(void)!write(c, out, (size_t)len);
		return;				/* the connection stays open */
	}
	if (n->cfg.ctl_group && cr.uid != geteuid() && (!strcmp(line, "stop") || !strncmp(line, "reply ", 6))) {	/* the group runs the node; the service's owner stops it and decides its policy */
		len = snprintf(out, sizeof out, "{\"error\":\"only the user this runs as may do that (systemctl stops a service)\"}\n");
		(void)!write(c, out, (size_t)len);
		close(c);
		return;
	}
	if (!strcmp(line, "status"))
		len = n->role ? board_status_json(n, out, sizeof out) : node_status_json(n, out, sizeof out);
	else if (!strcmp(line, "stop")) {
		n->stop = 1;
		len = snprintf(out, sizeof out, "{\"node\":\"stopping\"}\n");
	} else
		len = n->role ? board_cmd(n, line, out, sizeof out) : node_cmd(n, line, out, sizeof out);
	(void)!write(c, out, (size_t)len);
	close(c);
}

void ctl_close(lw_node *n)
{
	int i;

	for (i = 0; i < MAXSUBS; i++)
		if (n->subs[i] >= 0)
			close(n->subs[i]);
	if (n->ctl_fd >= 0) {
		close(n->ctl_fd);
		unlink(n->cfg.ctl_path);
	}
}

/* ---- names: claims that pass through us and into JSON ----------------------------- */

int name_ok(const uint8_t *s, size_t n)
{
	size_t i = 0;

	if (n > LW_NAME_MAX)
		return 0;
	while (i < n) {
		uint8_t c = s[i];
		int extra = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 && c >= 0xc2 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 && c <= 0xf4 ? 3 : -1;
		uint32_t cp;

		if (extra < 0 || i + (size_t)extra >= n)
			return 0;
		if (c < 0x20 || c == 0x7f)
			return 0;
		cp = extra == 0 ? c : extra == 1 ? c & 0x1fu : extra == 2 ? c & 0x0fu : c & 0x07u;
		for (int k = 1; k <= extra; k++) {
			if ((s[i + (size_t)k] & 0xc0) != 0x80)
				return 0;
			cp = cp << 6 | (s[i + (size_t)k] & 0x3fu);
		}
		/* strict UTF-8 (RFC 3629), and no code point that moves or hides text: controls, line breaks, direction overrides */
		if ((extra == 2 && cp < 0x800) || (extra == 3 && (cp < 0x10000 || cp > 0x10ffff)) || (cp >= 0xd800 && cp <= 0xdfff) ||
		    (cp >= 0x80 && cp <= 0x9f) || cp == 0x2028 || cp == 0x2029 || (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069) ||
		    (cp >= 0x200b && cp <= 0x200f) || cp == 0xfeff)
			return 0;
		i += (size_t)extra + 1;
	}
	return 1;
}

void json_esc(char *out, size_t cap, const char *in)
{
	size_t o = 0;

	for (; *in && o + 7 < cap; in++) {
		unsigned char c = (unsigned char)*in;

		if (c == '"' || c == '\\') {
			out[o++] = '\\';
			out[o++] = (char)c;
		} else if (c < 0x20 || c == 0x7f)
			o += (size_t)snprintf(out + o, 7, "\\u%04x", c);
		else
			out[o++] = (char)c;
	}
	out[o] = 0;
}
