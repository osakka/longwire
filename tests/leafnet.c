/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The phone's engine, embedded in a process of its own as the app embeds it (android/jni.c does the same): a node whose port is the adapter, on a thread, handed its tunnel from another thread.
 * tests/leafnet.py plays the home LAN and the OS: this takes DIR PEERBUNDLE@HOST:PORT TUNFD, and runs until it is sent a signal.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <dirent.h>
#include "../engine/all.c"

static volatile sig_atomic_t quit;
static int g_tun;
static void on_sig(int s) { (void)s; quit = 1; }

static void *serve(void *a)
{
	lw_node_serve(a);
	return 0;
}

static int nfds(void)
{
	DIR *d = opendir("/proc/self/fd");
	struct dirent *e;
	int k = 0;

	while (d && (e = readdir(d)))
		k++;
	if (d)
		closedir(d);
	return k - 1;				/* the directory's own descriptor */
}

/* one run of the engine, as the app has it: open, a thread, the tunnel handed over from this one, a signal to stop, release */
static int round_of(lw_cfg *c)
{
	char err[300] = "";
	lw_node *n;
	pthread_t th;

	quit = 0;
	if (!(n = lw_node_open(c, err, sizeof err))) {
		fprintf(stderr, "cannot open the node: %s\n", err);
		return 1;
	}
	pthread_create(&th, 0, serve, n);
	leaf_attach(n, g_tun);			/* from this thread, as the app does it: the engine's own thread takes it */
	printf("attached\n");
	fflush(stdout);
	while (!quit)
		usleep(50000);
	n->stop = 1;
	pthread_join(th, 0);
	lw_node_release(n);
	return 0;
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char *at;
	int before, rounds = argc > 4 ? atoi(argv[4]) : 1, i;

	if (argc < 4)
		return 2;
	g_tun = atoi(argv[3]);
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", argv[1]);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", argv[1]);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", argv[1]);
	snprintf(c.port_kind, sizeof c.port_kind, "member");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	if (!strncmp(argv[2], "sb:", 3)) {	/* as the app has it: the line's switchboard, and a call to make over the control socket */
		snprintf(c.switchboard, sizeof c.switchboard, "%s", argv[2] + 3);
		c.listed = 0;
	} else {
		snprintf(c.peers[0].bundle_path, sizeof c.peers[0].bundle_path, "%s", argv[2]);
		if ((at = strchr(c.peers[0].bundle_path, '@'))) {
			*at = 0;
			snprintf(c.peers[0].endpoint, sizeof c.peers[0].endpoint, "%s", at + 1);
		}
		c.npeers = 1;
	}
	c.embedded = 1;
	c.answer = 1;
	c.no_nat_pmp = 1;
	c.cand_loopback = 1;
	c.mtu = 1400;
	c.maxpeers = 8;
	c.rekey_after = 4;
	c.keepalive = 2;
	signal(SIGTERM, on_sig);
	before = nfds();
	for (i = 0; i < rounds; i++) {
		if (round_of(&c))
			return 1;
		printf("stopped %d %d\n", before, nfds());	/* descriptors before the first run, and now: an app that turns the line on and off a hundred times must not leak */
		fflush(stdout);
	}
	return 0;
}
