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
#include <sys/resource.h>
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

/* --listen is a list: "ADDR:PORT" (IPv4), "[ADDR]:PORT" (IPv6), or a bare "PORT", which is both families on any address.
 * Several may be given, separated by commas. */
static int open_one(const lw_node *n, int family, const struct sockaddr *sa, socklen_t sl, int *fd, uint16_t *port)
{
	struct sockaddr_storage bound;
	socklen_t bl = sizeof bound;
	int one = 1;

	*fd = socket(family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (*fd < 0)
		return -1;
	if (family == AF_INET6)
		setsockopt(*fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
	if (tie_fd(n, *fd) < 0 || bind(*fd, sa, sl) < 0) {
		close(*fd);
		*fd = -1;
		return -1;
	}
	if (getsockname(*fd, (struct sockaddr *)&bound, &bl) == 0)
		*port = ntohs(bound.ss_family == AF_INET6 ? ((struct sockaddr_in6 *)&bound)->sin6_port : ((struct sockaddr_in *)&bound)->sin_port);
	return 0;
}

static int bind_listen(lw_node *n, const char *spec, char *err, size_t errn)
{
	char list[256], *item, *save = 0;
	int any = 0;

	snprintf(list, sizeof list, "%s", spec);
	for (item = strtok_r(list, ",", &save); item; item = strtok_r(0, ",", &save)) {
		char *colon = strrchr(item, ':');
		int bare = strchr(item, ':') == 0;

		if (item[0] == '[' && colon) {		/* IPv6 */
			struct sockaddr_in6 a6;
			char h[64];

			memset(&a6, 0, sizeof a6);
			a6.sin6_family = AF_INET6;
			snprintf(h, sizeof h, "%.*s", (int)(colon - item) - 2, item + 1);
			if (inet_pton(AF_INET6, h, &a6.sin6_addr) != 1) {
				snprintf(err, errn, "bad listen address %s", item);
				return -1;
			}
			a6.sin6_port = htons((uint16_t)atoi(colon + 1));
			if (open_one(n, AF_INET6, (struct sockaddr *)&a6, sizeof a6, &n->udp6, &n->udp6_port)) {
				snprintf(err, errn, "udp %s: %s", item, strerror(errno));
				return -1;
			}
		} else {
			struct sockaddr_in a4;
			unsigned port = (unsigned)atoi(colon ? colon + 1 : item);

			memset(&a4, 0, sizeof a4);
			a4.sin_family = AF_INET;
			if (colon) {
				char h[64];

				snprintf(h, sizeof h, "%.*s", (int)(colon - item), item);
				if (inet_pton(AF_INET, h, &a4.sin_addr) != 1) {
					snprintf(err, errn, "bad listen address %s", item);
					return -1;
				}
			}
			a4.sin_port = htons((uint16_t)port);
			if (open_one(n, AF_INET, (struct sockaddr *)&a4, sizeof a4, &n->udp, &n->udp_port)) {
				snprintf(err, errn, "udp %s: %s", item, strerror(errno));
				return -1;
			}
			if (bare) {			/* a bare port is both families: IPv6 follows the port IPv4 got, if it can */
				struct sockaddr_in6 a6;

				memset(&a6, 0, sizeof a6);
				a6.sin6_family = AF_INET6;
				a6.sin6_port = htons(n->udp_port);
				if (open_one(n, AF_INET6, (struct sockaddr *)&a6, sizeof a6, &n->udp6, &n->udp6_port)) {
					a6.sin6_port = 0;
					open_one(n, AF_INET6, (struct sockaddr *)&a6, sizeof a6, &n->udp6, &n->udp6_port);	/* best effort */
				}
			}
		}
		any = 1;
	}
	if (!any) {
		snprintf(err, errn, "empty --listen");
		return -1;
	}
	return 0;
}

lw_node *lw_node_open(const lw_cfg *cfg, char *err, size_t errn)
{
	lw_node *n = calloc(1, sizeof *n);
	int i;
	uint8_t b[LW_BUNDLE];

	if (!n) {
		snprintf(err, errn, "out of memory");
		return 0;
	}
	n->cfg = *cfg;
	n->udp = n->udp2 = n->udp6 = n->port_fd = n->ctl_fd = n->uudp = -1;
	n->rekey_after = cfg->rekey_after > 0 ? cfg->rekey_after : 120;
	n->sb_rekey = cfg->sb_rekey > 0 ? cfg->sb_rekey : 600;
	n->keepalive = cfg->keepalive > 0 ? cfg->keepalive : 25;
	if (n->cfg.direct_retry <= 0)
		n->cfg.direct_retry = 60;
	n->mtu = cfg->mtu > 0 ? cfg->mtu : 1400;
	for (i = 0; i < MAC_SLOTS; i++)
		n->macs[i].port = -2;
	n->now = n->started = lw_now();
	n->role = cfg->role;
	n->hs_t = n->now;
	n->hs_tokens = n->role ? 2000.0 : 50.0;
	n->maxpeers = cfg->maxpeers > 0 ? cfg->maxpeers : (cfg->role ? 1024 : 32);
	if (n->maxpeers > 65536) {
		snprintf(err, errn, "at most 65536 peers");
		free(n);
		return 0;
	}
	for (n->idxbits = 1; (1 << n->idxbits) < n->maxpeers; n->idxbits++)
		;
	lw_rand(&n->hsalt, sizeof n->hsalt);
	for (i = 0; i < MAXSUBS; i++)		/* no watcher: -1, not the 0 that calloc leaves, which is a descriptor */
		n->subs[i] = -1;
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
	if (under_tie(n, err, errn) || bind_listen(n, cfg->listen, err, errn))
		goto fail;
	{	/* bursts of full-size frames: ask for room (the kernel clamps to rmem_max) */
		int buf = 4 << 20, k;

		for (k = 0; k < 2; k++) {
			int fd = k ? n->udp6 : n->udp;

			if (fd >= 0) {
				setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
				setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);
			}
		}
	}
	if (n->role && n->udp >= 0 && n->udp_port < 65535) {	/* ECHO's port; the switchboard works without it if it cannot be had */
		struct sockaddr_in la;
		socklen_t ll = sizeof la;

		memset(&la, 0, sizeof la);
		if (getsockname(n->udp, (struct sockaddr *)&la, &ll) == 0) {
			la.sin_port = htons((uint16_t)(n->udp_port + 1));
			n->udp2 = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
			if (n->udp2 >= 0 && bind(n->udp2, (struct sockaddr *)&la, sizeof la) < 0) {
				close(n->udp2);
				n->udp2 = -1;
			}
		}
	}
	if (port_open(n, err, errn) || ctl_open(n, err, errn))
		goto fail;
	if (n->role ? board_open(n, err, errn) : node_init(n, err, errn))
		goto fail;
	if (site_init(n, err, errn))
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
	ban_free(n);
	if (n->udp2 >= 0)
		close(n->udp2);
	if (n->uudp >= 0)
		close(n->uudp);
	if (n->udp6 >= 0)
		close(n->udp6);
	if (n->port_fd >= 0)
		close(n->port_fd);
	if (n->ctl_fd >= 0)
		close(n->ctl_fd);
	crypto_wipe(&n->id, sizeof n->id);
	free(n->calls);
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
			if (fd != n->udp && fd != n->udp2 && fd != n->udp6 && fd != n->uudp && fd != n->port_fd && fd != n->ctl_fd)
				close(fd);
		return;
	}
	while ((e = readdir(d))) {
		fd = atoi(e->d_name);
		if (fd > 2 && fd != dfd && fd != n->udp && fd != n->udp2 && fd != n->udp6 && fd != n->uudp && fd != n->port_fd && fd != n->ctl_fd)
			close(fd);
	}
	closedir(d);
}

/* One datagram off the wire (or out of a relay's wrapping) to whoever handles its type. */
void node_dispatch(lw_node *n, const uint8_t *buf, size_t r, const struct sockaddr *ss, socklen_t sl)
{
	if (buf[0] == T_DATA)
		sess_input(n, buf, r, ss, sl);
	else if (buf[0] == 0)			/* NAT-PMP has version 0 */
		pmp_input(n, buf, r, ss, sl);
	else if (buf[0] == T_COOKIE)
		cookie_input(n, buf, r, ss, sl);
	else if (buf[0] == T_PUNCH)
		punch_input(n, buf, r, ss, sl);
	else if (buf[0] == T_ECHO)
		echo_input(n, buf, r, ss, sl);
	else if (buf[0] == T_HELLO)
		hello_input(n, buf, r, ss, sl);
	else if (buf[0] == T_HS_INIT || buf[0] == T_HS_RESP)
		hs_input(n, buf, r, ss, sl);
	else if (buf[0] == T_RELAY)
		relay_input(n, buf, r, ss, sl);
	else
		n->drop_garbage++;
}

int lw_node_serve(lw_node *n)
{
	struct pollfd *pf = malloc((size_t)(6 + SPRAY_N + n->maxpeers) * sizeof *pf);
	int nf, k;
	uint8_t buf[2048];
	static uint8_t big[65600];		/* a NIC may hand us a 64 KB coalesced frame */
	struct sockaddr_storage ss;
	socklen_t sl;
	ssize_t r;
	int i;

	n->rx_fd = n->udp >= 0 ? n->udp : n->udp6;
	close_inherited(n);
	{					/* a crash must not leave the seed or the KEM key in a file */
		struct rlimit nocore = { 0, 0 };

		setrlimit(RLIMIT_CORE, &nocore);
	}
	mlock(&n->id, sizeof n->id);		/* locks don't survive fork(); failure (rlimit) is not fatal */
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, SIG_IGN);
	while (!n->stop && !g_stop) {
		pf[0].fd = n->udp;
		pf[1].fd = n->port_fd;
		pf[2].fd = n->ctl_fd;
		pf[3].fd = n->udp2;			/* a negative descriptor is ignored by poll */
		pf[4].fd = n->udp6;
		nf = 5;
		if (n->uudp >= 0)			/* the underlay socket (under.c) */
			pf[nf++].fd = n->uudp;
		for (k = 0; k < n->nspray && nf < 6 + SPRAY_N; k++)	/* sockets opened to be found (spray.c) */
			pf[nf++].fd = n->spray_fd[k];
		for (k = 0; k < n->maxpeers && nf < 6 + SPRAY_N + 256; k++)
			if (n->peers[k].used && n->peers[k].sfd)
				pf[nf++].fd = n->peers[k].sfd;
		for (k = 0; k < nf; k++)
			pf[k].events = POLLIN;
		if (poll(pf, (nfds_t)nf, 100) < 0 && errno != EINTR)
			break;
		n->now = lw_now();
		jnets_fresh(n);				/* the jack's networks, once a second (under.c) */
		for (k = 4; k < nf; k++)
			if (pf[k].revents & POLLIN) {
				int fd = pf[k].fd;

				n->rx_fd = fd;
				for (i = 0; i < 64; i++) {
					sl = sizeof ss;
					r = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&ss, &sl);
					if (r < 1)
						break;
					n->rx_pkts++;
					node_dispatch(n, buf, (size_t)r, (struct sockaddr *)&ss, sl);
				}
				n->rx_fd = n->udp >= 0 ? n->udp : n->udp6;
			}
		if (pf[0].revents & POLLIN)
			for (i = 0; i < 256; i++) {
				sl = sizeof ss;
				r = recvfrom(n->udp, buf, sizeof buf, 0, (struct sockaddr *)&ss, &sl);
				if (r < 1)
					break;
				n->rx_pkts++;
				node_dispatch(n, buf, (size_t)r, (struct sockaddr *)&ss, sl);
			}
		if (pf[1].revents & POLLIN)
			for (i = 0; i < 256; i++) {
				r = port_read(n, big, sizeof big);
				if (r < 1)
					break;
				if (r > FRAME_MAX) {		/* a coalesced frame from a NIC: cut it down to size */
					if (resegment(n, big, (size_t)r))
						n->resegmented++;
					else
						n->drop_oversize++;
				} else
					sw_from_local(n, big, (size_t)r);
			}
		if (pf[3].revents & POLLIN)
			echo_serve(n);
		if (pf[2].revents & POLLIN)
			ctl_accept(n);
		sess_timers(n);
		if (n->role)
			board_timers(n);
		else
			node_timers(n);
		site_timers(n);
		ctl_subs_reap(n);
	}
	ctl_close(n);
	return 0;
}
