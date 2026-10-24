/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * longwire.so -- the hibr module. One builtin, `lw`, and the
 * /dev/longwire/events scheme. The engine does the work in a forked worker
 * process; everything here is argument parsing and glue.
 * See docs/ARCHITECTURE.md §7.
 */
#define _GNU_SOURCE
#include "hibr.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../engine/lw.h"

static void default_sock(char *out, size_t n)
{
	const char *e = getenv("LW_SOCK"), *h = getenv("HOME");

	if (e && *e)
		snprintf(out, n, "%s", e);
	else
		snprintf(out, n, "%s/.longwire/ctl", h ? h : ".");
}

static int ctl_connect(const char *sock)
{
	struct sockaddr_un a;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", sock);
	if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	return fd;
}

/* One request/reply on the control socket; the reply lands in $RET. With
 * `fail_on_error`, a {"error":...} reply becomes a failed command whose
 * message is in $ERRMSG. */
static int ask(sh *s, const char *sock, const char *req, int fail_on_error)
{
	char buf[8192];
	struct pollfd pf;
	ssize_t r;
	int fd = ctl_connect(sock), len = 0;

	if (fd < 0) {
		hibr_fail(s, "lw: no node is running (control socket)");
		return HIBR_FAIL;
	}
	(void)!write(fd, req, strlen(req));
	pf.fd = fd;
	pf.events = POLLIN;
	while (len < (int)sizeof buf - 1 && poll(&pf, 1, 2000) > 0 && (r = read(fd, buf + len, sizeof buf - 1 - (size_t)len)) > 0)
		len += (int)r;
	close(fd);
	buf[len] = 0;
	while (len && (buf[len - 1] == '\n'))
		buf[--len] = 0;
	if (fail_on_error && !strncmp(buf, "{\"error\":\"", 10)) {
		char *e = buf + 10, *q = strchr(e, '"');

		if (q)
			*q = 0;
		hibr_fail(s, e);
		return HIBR_FAIL;
	}
	hibr_ret(s, buf);
	if (!s->bind)
		printf("%s\n", buf);
	return HIBR_OK;
}

static int usage(sh *s)
{
	hibr_fail(s, "usage: lw start|stop|status|dial|pickup|reject|hangup|unpin|evict|peers|key|sb ...  (see docs/CLI.md)");
	return HIBR_FAIL;
}

static int do_key(sh *s, int ac, char **av)
{
	lw_id *id = calloc(1, sizeof *id);
	char *hex = malloc(LW_BUNDLE * 2 + 1);
	char addr[60];
	int rc = HIBR_FAIL;

	if (ac >= 4 && !strcmp(av[2], "export")) {	/* the words go to the terminal only, never into a variable */
		char words[600];

		if (lw_id_load(id, av[3], 0)) {
			hibr_fail(s, "lw key export: cannot read seed");
			goto out;
		}
		lw_seed_words(words, sizeof words, id->seed);
		printf("%s\n", words);
		explicit_bzero(words, sizeof words);
		rc = HIBR_OK;
		goto out;
	}
	if (ac >= 4 && !strcmp(av[2], "import")) {	/* the words come on standard input, not on a command line */
		char text[1024];
		ssize_t r = read(0, text, sizeof text - 1);
		int fd, k;

		if (r <= 0) {
			hibr_fail(s, "lw key import: give the words on standard input");
			goto out;
		}
		text[r] = 0;
		k = lw_seed_parse(text, id->seed);
		explicit_bzero(text, sizeof text);
		if (k) {
			hibr_fail(s, k == -2 ? "lw key import: the checksum word does not match: a word is wrong or out of order"
					     : "lw key import: expected 33 words from the list");
			goto out;
		}
		fd = open(av[3], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (fd < 0 || write(fd, id->seed, 32) != 32) {
			hibr_fail(s, errno == EEXIST ? "lw key import: this node already has an identity (move the seed file away first)"
						      : "lw key import: cannot write the seed file");
			if (fd >= 0) {
				close(fd);
				unlink(av[3]);
			}
			goto out;
		}
		close(fd);
		rc = HIBR_OK;
		goto out;
	}
	if (ac < 4 || (strcmp(av[2], "id") && strcmp(av[2], "bundle") && strcmp(av[2], "new") && strcmp(av[2], "address"))) {
		hibr_fail(s, "usage: lw key new|id|address|bundle|export|import SEEDFILE");
		goto out;
	}
	if (lw_id_load(id, av[3], !strcmp(av[2], "new"))) {
		hibr_fail(s, "lw key: cannot read seed (use `lw key new SEEDFILE` to create one)");
		goto out;
	}
	if (!strcmp(av[2], "bundle"))
		lw_hex(hex, id->bundle, LW_BUNDLE);
	else if (!strcmp(av[2], "address")) {
		lw_address(addr, id->key_id);
		snprintf(hex, 70, "%s", addr);
	} else
		lw_hex(hex, id->key_id, 32);
	hibr_ret(s, hex);
	if (!s->bind)
		printf("%s\n", hex);
	rc = HIBR_OK;
out:
	if (id)
		explicit_bzero(id, sizeof *id);
	free(id);
	free(hex);
	return rc;
}

static int do_start(sh *s, int ac, char **av, int role)
{
	lw_cfg *c = calloc(1, sizeof *c);
	char err[300] = "", sockdef[256], msg[200];
	lw_node *n;
	pid_t pid;
	int i, rc = HIBR_FAIL;

	snprintf(c->listen, sizeof c->listen, "0");
	default_sock(sockdef, sizeof sockdef);
	snprintf(c->ctl_path, sizeof c->ctl_path, "%s", sockdef);
	{
		const char *h = getenv("HOME");

		snprintf(c->seed_path, sizeof c->seed_path, "%s/.longwire/seed", h ? h : ".");
	}
	c->role = role;
	if (role)
		snprintf(c->port_kind, sizeof c->port_kind, "none");
	for (i = 2; i < ac; i++) {
		const char *a = av[i], *v = i + 1 < ac ? av[i + 1] : 0;

		if (!strcmp(a, "--cand-loopback")) {
			c->cand_loopback = 1;
			continue;
		}
		if (!strcmp(a, "--listed") && !role) {
			c->listed = 1;
			continue;
		}
		if (!strcmp(a, "--issue-codes") && role) {
			c->issue_codes = 1;
			continue;
		}
		if (!v) {
			snprintf(msg, sizeof msg, "lw start: option %s needs a value", a);
			hibr_fail(s, msg);
			goto out;
		}
		i++;
		if (!strcmp(a, "--jack") && !role) {
			snprintf(c->port_kind, sizeof c->port_kind, "jack");
			snprintf(c->port_arg, sizeof c->port_arg, "%s", v);
		} else if (!strcmp(a, "--splice") && !role) {
			snprintf(c->port_kind, sizeof c->port_kind, "splice");
			snprintf(c->port_arg, sizeof c->port_arg, "%s", v);
		} else if (!strcmp(a, "--site-key") && !role)
			snprintf(c->site_key_path, sizeof c->site_key_path, "%s", v);
		else if (!strcmp(a, "--sock-port") && !role) {
			snprintf(c->port_kind, sizeof c->port_kind, "sock");
			snprintf(c->port_arg, sizeof c->port_arg, "%s", v);
		} else if (!strcmp(a, "--listen"))
			snprintf(c->listen, sizeof c->listen, "%s", v);
		else if (!strcmp(a, "--seed"))
			snprintf(c->seed_path, sizeof c->seed_path, "%s", v);
		else if (!strcmp(a, "--sock"))
			snprintf(c->ctl_path, sizeof c->ctl_path, "%s", v);
		else if (!strcmp(a, "--state"))
			snprintf(c->state_dir, sizeof c->state_dir, "%s", v);
		else if (!strcmp(a, "--switchboard") && !role)
			snprintf(c->switchboard, sizeof c->switchboard, "%s", v);
		else if (!strcmp(a, "--name") && !role)
			snprintf(c->name, sizeof c->name, "%s", v);
		else if (!strcmp(a, "--registry") && role)
			snprintf(c->registry, sizeof c->registry, "%s", v);
		else if (!strcmp(a, "--public-switchboard") && role)
			snprintf(c->public_switchboard, sizeof c->public_switchboard, "%s", v);
		else if (!strcmp(a, "--caller-id") && role)
			c->no_caller_id = !strcmp(v, "off");
		else if (!strcmp(a, "--directory") && role)
			c->no_directory = !strcmp(v, "off");
		else if (!strcmp(a, "--token"))
			snprintf(c->token, sizeof c->token, "%s", v);
		else if (!strcmp(a, "--policy") && role)
			c->policy_script = !strcmp(v, "script");
		else if (!strcmp(a, "--maxpeers"))
			c->maxpeers = atoi(v);
		else if (!strcmp(a, "--rekey"))
			c->rekey_after = atoi(v);
		else if (!strcmp(a, "--relay")) {
			if (!strcmp(v, "yes"))
				c->relay = 1;
			else if (!strcmp(v, "no"))
				c->relay = 2;
			else if (strcmp(v, "auto")) {
				hibr_fail(s, "lw start: --relay is yes, no or auto");
				goto out;
			}
		} else if (!strcmp(a, "--direct-retry"))
			c->direct_retry = atoi(v);
		else if (!strcmp(a, "--keepalive"))
			c->keepalive = atoi(v);
		else if (!strcmp(a, "--peer") && !role) {
			char *at;

			if (c->npeers >= LW_MAXPEERS) {
				hibr_fail(s, "lw start: too many peers");
				goto out;
			}
			snprintf(c->peers[c->npeers].bundle_path, sizeof c->peers[0].bundle_path, "%s", v);
			if ((at = strchr(c->peers[c->npeers].bundle_path, '@'))) {
				*at = 0;
				snprintf(c->peers[c->npeers].endpoint, sizeof c->peers[0].endpoint, "%s", at + 1);
			}
			c->npeers++;
		} else {
			snprintf(msg, sizeof msg, "lw start: unknown option %s", a);
			hibr_fail(s, msg);
			goto out;
		}
	}
	if (!c->port_kind[0]) {
		hibr_fail(s, "lw start: need --splice NIC, --jack TAPNAME (or --sock-port LOCAL:PEER for tests)");
		goto out;
	}
	{
		char dir[256], *sl;

		snprintf(dir, sizeof dir, "%s", c->seed_path);
		if ((sl = strrchr(dir, '/'))) {
			*sl = 0;
			mkdir(dir, 0700);
		}
		if (c->state_dir[0])
			mkdir(c->state_dir, 0700);
	}
	if (!(n = lw_node_open(c, err, sizeof err))) {
		hibr_fail(s, err);
		goto out;
	}
	/* The port is open: nothing needs privilege again. Clear it in this
	 * process and (by inheritance) in the worker, before anything else runs. */
	if (lw_caps_drop()) {
		hibr_fail(s, "lw start: could not clear capabilities; refusing to run");
		lw_node_release(n);
		goto out;
	}
	fflush(0);
	pid = fork();
	if (pid < 0) {
		hibr_fail(s, "lw start: fork failed");
		lw_node_release(n);
		goto out;
	}
	if (pid == 0) {
		int fd;

		setsid();
		fd = open("/dev/null", O_RDWR);
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		hibr_title2("lw-worker", role ? "longwire switchboard worker" : "longwire worker");
		lw_node_serve(n);
		_exit(0);
	}
	lw_node_release(n);			/* the parent keeps no descriptors */
	{
		char b[32];

		snprintf(b, sizeof b, "%d", (int)pid);
		hibr_ret(s, b);
		if (!s->bind)
			printf("%s\n", b);
	}
	rc = HIBR_OK;
out:
	free(c);
	return rc;
}

int m_lw(sh *s, int ac, char **av)
{
	char sock[256], req[300];
	int i;

	if (ac < 2)
		return usage(s);
	default_sock(sock, sizeof sock);
	for (i = 2; i + 1 < ac; i++)
		if (!strcmp(av[i], "--sock"))
			snprintf(sock, sizeof sock, "%s", av[i + 1]);
	if (!strcmp(av[1], "start"))
		return do_start(s, ac, av, 0);
	if (!strcmp(av[1], "sb") && ac >= 3) {
		if (!strcmp(av[2], "start"))
			return do_start(s, ac - 1, av + 1, 1);
		if (!strcmp(av[2], "reply") && ac >= 5) {	/* lw sb reply ID accept|refuse [ARG...] */
			int o = snprintf(req, sizeof req, "reply %s %s", av[3], av[4]);

			for (i = 5; i < ac && o < (int)sizeof req - 2; i++) {
				if (!strcmp(av[i], "--sock")) {
					i++;
					continue;
				}
				o += snprintf(req + o, sizeof req - (size_t)o, " %s", av[i]);
			}
			snprintf(req + o, sizeof req - (size_t)o, "\n");
			return ask(s, sock, req, 1);
		}
		return usage(s);
	}
	if (!strcmp(av[1], "status"))
		return ask(s, sock, "status\n", 0);
	if (!strcmp(av[1], "stop"))
		return ask(s, sock, "stop\n", 0);
	if ((!strcmp(av[1], "dial") || !strcmp(av[1], "unpin") || !strcmp(av[1], "who") || !strcmp(av[1], "lookup") || !strcmp(av[1], "evict")) && ac >= 3) {
		snprintf(req, sizeof req, "%s %s\n", av[1], av[2]);
		return ask(s, sock, req, 1);
	}
	if (!strcmp(av[1], "peers"))
		return ask(s, sock, "peers\n", 1);
	if (!strcmp(av[1], "pickup") || !strcmp(av[1], "reject") || !strcmp(av[1], "hangup")) {
		const char *arg = 0;			/* pickup and reject may name a call, hangup a number */

		for (i = 2; i < ac; i++) {
			if (!strcmp(av[i], "--sock")) {
				i++;
				continue;
			}
			arg = av[i];
			break;
		}
		if (arg)
			snprintf(req, sizeof req, "%s %s\n", av[1], arg);
		else
			snprintf(req, sizeof req, "%s\n", av[1]);
		return ask(s, sock, req, 1);
	}
	if (!strcmp(av[1], "key"))
		return do_key(s, ac, av);
	if (!strcmp(av[1], "plan") && ac >= 3) {		/* the numbering plan, for tests and for operators planning ahead */
		char code[10];

		lw_nth_code(strtoul(av[2], 0, 10), code);
		hibr_ret(s, code);
		if (!s->bind)
			printf("%s\n", code);
		return HIBR_OK;
	}
	return usage(s);
}

/* /dev/longwire/events: a descriptor that yields the worker's events, one
 * JSON object per line. The worker's own sock is $LW_SOCK or ~/.longwire/ctl. */
static int events_open(sh *s, const char *rest)
{
	char sock[256];
	int fd;
	char c;

	if (strcmp(rest, "events")) {
		hibr_fail(s, "/dev/longwire/: only 'events' exists");
		return -1;
	}
	default_sock(sock, sizeof sock);
	if ((fd = ctl_connect(sock)) < 0) {
		hibr_fail(s, "/dev/longwire/events: no node is running");
		return -1;
	}
	if (write(fd, "watch\n", 6) != 6) {
		close(fd);
		return -1;
	}
	while (read(fd, &c, 1) == 1 && c != '\n')	/* the {"event":"watching"} acknowledgement */
		;
	return fd;
}

static int lw_ini(sh *s)
{
	return hibr_scheme(s, "longwire", events_open);
}

static void lw_fin(sh *s)
{
	hibr_unscheme(s, "longwire");
}

const hibr_bi lw_bi[] = { { "lw", m_lw, "longwire engine: start, stop, status, dial, pickup, key, sb" }, HIBR_BI_END };
HIBR_MODULE("longwire", "0.2", "longwire L2 mesh VPN engine", lw_bi, lw_ini, lw_fin);
