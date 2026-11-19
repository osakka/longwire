/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The phone's engine, embedded in a process of its own as the app embeds it (android/jni.c does the same): a node whose port is the adapter, on a thread, handed its tunnel from another thread.
 * tests/leafnet.py plays the home LAN and the OS: this takes DIR PEERBUNDLE@HOST:PORT TUNFD, and runs until it is sent a signal.
 */
#define _GNU_SOURCE
#include <signal.h>
#include "../engine/all.c"

static volatile sig_atomic_t quit;
static void on_sig(int s) { (void)s; quit = 1; }

static void *serve(void *a)
{
	lw_node_serve(a);
	return 0;
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[300] = "", *at;
	lw_node *n;
	pthread_t th;

	if (argc < 4)
		return 2;
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", argv[1]);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", argv[1]);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", argv[1]);
	snprintf(c.port_kind, sizeof c.port_kind, "member");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	snprintf(c.peers[0].bundle_path, sizeof c.peers[0].bundle_path, "%s", argv[2]);
	if ((at = strchr(c.peers[0].bundle_path, '@'))) {
		*at = 0;
		snprintf(c.peers[0].endpoint, sizeof c.peers[0].endpoint, "%s", at + 1);
	}
	c.npeers = 1;
	c.embedded = 1;
	c.no_nat_pmp = 1;
	c.cand_loopback = 1;
	c.mtu = 1400;
	c.maxpeers = 8;
	c.rekey_after = 4;
	c.keepalive = 2;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open the node: %s\n", err);
		return 1;
	}
	signal(SIGTERM, on_sig);
	pthread_create(&th, 0, serve, n);
	leaf_attach(n, atoi(argv[3]));		/* from this thread, as the app does it: the engine's own thread takes it */
	printf("attached\n");
	fflush(stdout);
	while (!quit)
		usleep(50000);
	n->stop = 1;
	pthread_join(th, 0);
	lw_node_release(n);
	return 0;
}
