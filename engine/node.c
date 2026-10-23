/* SPDX-License-Identifier: GPL-3.0-or-later
 * A node: open (everything that needs privilege) and serve (the event loop). */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <dirent.h>
#include <poll.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "int.h"

static volatile sig_atomic_t g_stop;

static void on_term(int s)
{
	(void)s;
	g_stop = 1;
}

static int unhex(const char *s, uint8_t *out, size_t n)
{
	size_t i;

	for (i = 0; i < n * 2; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
			return -1;
	for (i = 0; i < n; i++) {
		int a = s[2 * i] <= '9' ? s[2 * i] - '0' : s[2 * i] - 'a' + 10;
		int b = s[2 * i + 1] <= '9' ? s[2 * i + 1] - '0' : s[2 * i + 1] - 'a' + 10;

		out[i] = (uint8_t)(a << 4 | b);
	}
	return 0;
}

static int load_bundle(uint8_t *out, const char *path, char *err, size_t errn)
{
	char buf[LW_BUNDLE * 2 + 8];
	FILE *f = fopen(path, "r");
	size_t r;

	if (!f) {
		snprintf(err, errn, "%s: %s", path, strerror(errno));
		return -1;
	}
	r = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[r] = 0;
	if (r < LW_BUNDLE * 2 || unhex(buf, out, LW_BUNDLE) || out[0] != 0x01) {
		snprintf(err, errn, "%s: not a longwire bundle (hex, %d bytes)", path, LW_BUNDLE);
		return -1;
	}
	return 0;
}

lw_node *lw_node_open(const lw_cfg *cfg, char *err, size_t errn)
{
	lw_node *n = calloc(1, sizeof *n);
	struct sockaddr_in la;
	char *colon;
	int i;
	uint8_t b[LW_BUNDLE];

	if (!n) {
		snprintf(err, errn, "out of memory");
		return 0;
	}
	n->cfg = *cfg;
	n->udp = n->port_fd = n->ctl_fd = -1;
	n->rekey_after = cfg->rekey_after > 0 ? cfg->rekey_after : 120;
	n->keepalive = cfg->keepalive > 0 ? cfg->keepalive : 25;
	n->mtu = cfg->mtu > 0 ? cfg->mtu : 1400;
	for (i = 0; i < MAC_SLOTS; i++)
		n->macs[i].port = -2;
	n->now = n->started = lw_now();
	n->hs_t = n->now;
	n->hs_tokens = n->role ? 2000.0 : 50.0;
	n->role = cfg->role;
	n->maxpeers = cfg->maxpeers > 0 ? cfg->maxpeers : (cfg->role ? 1024 : 16);
	n->peers = calloc((size_t)n->maxpeers, sizeof *n->peers);
	if (!n->peers) {
		snprintf(err, errn, "out of memory");
		goto fail;
	}
	if (!n->cfg.state_dir[0]) {
		char *sl;

		snprintf(n->cfg.state_dir, sizeof n->cfg.state_dir, "%s", cfg->seed_path);
		if ((sl = strrchr(n->cfg.state_dir, '/')))
			*sl = 0;
		else
			snprintf(n->cfg.state_dir, sizeof n->cfg.state_dir, ".");
	}
	if (lw_id_load(&n->id, cfg->seed_path, 1)) {
		snprintf(err, errn, "seed %s: %s", cfg->seed_path, strerror(errno));
		goto fail;
	}
	lw_h2(n->mac1_key, "mac1", 4, n->id.key_id, 32);
	for (i = 0; i < cfg->npeers; i++) {
		lw_peer *p;

		if (load_bundle(b, cfg->peers[i].bundle_path, err, errn))
			goto fail;
		if (!(p = peer_add(n, b))) {
			snprintf(err, errn, "peer table full");
			goto fail;
		}
		if (!crypto_verify32(p->key_id, n->id.key_id)) {
			snprintf(err, errn, "peer %d is this node's own bundle", i + 1);
			goto fail;
		}
		if (cfg->peers[i].endpoint[0]) {
			if (lw_resolve(cfg->peers[i].endpoint, &p->ep, &p->eplen)) {
				snprintf(err, errn, "cannot resolve %s", cfg->peers[i].endpoint);
				goto fail;
			}
			p->dial = 1;
		}
	}
	memset(&la, 0, sizeof la);
	la.sin_family = AF_INET;
	colon = strrchr(cfg->listen, ':');
	if (colon) {
		char h[64];

		snprintf(h, sizeof h, "%.*s", (int)(colon - cfg->listen), cfg->listen);
		if (inet_pton(AF_INET, h, &la.sin_addr) != 1) {
			snprintf(err, errn, "bad listen address %s", cfg->listen);
			goto fail;
		}
	}
	la.sin_port = htons((uint16_t)atoi(colon ? colon + 1 : cfg->listen));
	n->udp = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (n->udp < 0 || bind(n->udp, (struct sockaddr *)&la, sizeof la) < 0) {
		snprintf(err, errn, "udp %s: %s", cfg->listen, strerror(errno));
		goto fail;
	}
	{	/* bursts of full-size frames: ask for room (the kernel clamps to rmem_max) */
		int buf = 4 << 20;

		setsockopt(n->udp, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
		setsockopt(n->udp, SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);
	}
	{
		struct sockaddr_in bound;
		socklen_t bl = sizeof bound;

		if (getsockname(n->udp, (struct sockaddr *)&bound, &bl) == 0)
			n->udp_port = ntohs(bound.sin_port);
	}
	if (port_open(n, err, errn) || ctl_open(n, err, errn))
		goto fail;
	if (n->role ? board_open(n, err, errn) : node_init(n, err, errn))
		goto fail;
	return n;
fail:
	lw_node_release(n);
	return 0;
}

void lw_node_release(lw_node *n)
{
	if (!n)
		return;
	if (n->udp >= 0)
		close(n->udp);
	if (n->port_fd >= 0)
		close(n->port_fd);
	if (n->ctl_fd >= 0)
		close(n->ctl_fd);
	crypto_wipe(&n->id, sizeof n->id);
	free(n->peers);
	free(n->pins);
	free(n->bd);
	free(n);
}

/* The worker must not hold whatever the hibr shell had open (files, sockets,
 * a desktop's descriptors): close everything but stdio and the node's own. */
static void close_inherited(const lw_node *n)
{
	DIR *d = opendir("/proc/self/fd");
	struct dirent *e;
	int dfd = d ? dirfd(d) : -1, fd, max = getdtablesize();

	if (!d) {
		for (fd = 3; fd < max && fd < 65536; fd++)
			if (fd != n->udp && fd != n->port_fd && fd != n->ctl_fd)
				close(fd);
		return;
	}
	while ((e = readdir(d))) {
		fd = atoi(e->d_name);
		if (fd > 2 && fd != dfd && fd != n->udp && fd != n->port_fd && fd != n->ctl_fd)
			close(fd);
	}
	closedir(d);
}

int lw_node_serve(lw_node *n)
{
	struct pollfd pf[3];
	uint8_t buf[2048];
	struct sockaddr_storage ss;
	socklen_t sl;
	ssize_t r;
	int i;

	close_inherited(n);
	mlock(&n->id, sizeof n->id);		/* locks don't survive fork(); failure (rlimit) is not fatal */
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, SIG_IGN);
	while (!n->stop && !g_stop) {
		pf[0].fd = n->udp;
		pf[1].fd = n->port_fd;
		pf[2].fd = n->ctl_fd;
		pf[0].events = pf[1].events = pf[2].events = POLLIN;
		if (poll(pf, 3, 100) < 0 && errno != EINTR)
			break;
		n->now = lw_now();
		if (pf[0].revents & POLLIN)
			for (i = 0; i < 256; i++) {
				sl = sizeof ss;
				r = recvfrom(n->udp, buf, sizeof buf, 0, (struct sockaddr *)&ss, &sl);
				if (r < 1)
					break;
				n->rx_pkts++;
				if (buf[0] == T_DATA)
					sess_input(n, buf, (size_t)r, (struct sockaddr *)&ss, sl);
				else if (buf[0] == T_PUNCH)
					punch_input(n, buf, (size_t)r, (struct sockaddr *)&ss, sl);
				else if (buf[0] == T_HELLO)
					hello_input(n, buf, (size_t)r, (struct sockaddr *)&ss, sl);
				else if (buf[0] == T_HS_INIT || buf[0] == T_HS_RESP)
					hs_input(n, buf, (size_t)r, (struct sockaddr *)&ss, sl);
				else
					n->drop_garbage++;
			}
		if (pf[1].revents & POLLIN)
			for (i = 0; i < 256; i++) {
				r = port_read(n, buf, sizeof buf);
				if (r < 1)
					break;
				sw_from_local(n, buf, (size_t)r);
			}
		if (pf[2].revents & POLLIN)
			ctl_accept(n);
		sess_timers(n);
		if (n->role)
			board_timers(n);
		else
			node_timers(n);
		ctl_subs_reap(n);
	}
	ctl_close(n);
	return 0;
}
