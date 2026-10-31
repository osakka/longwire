/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * A jack's own network is no way to reach the other end (docs/adr/0037): such an address is not offered, and what is sent to one
 * leaves through the underlay socket, tied to a device of its own. The loopback interface stands in for the jack (it has addresses
 * and a netmask, which is all the check reads), and for the device too; no privilege is needed.
 */
#include "../engine/all.c"

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

static lw_cand mk(int a, int b, int c, int d, int port, int type)
{
	lw_cand x;

	memset(&x, 0, sizeof x);
	x.ip[0] = (uint8_t)a; x.ip[1] = (uint8_t)b; x.ip[2] = (uint8_t)c; x.ip[3] = (uint8_t)d;
	x.port = (uint16_t)port;
	x.type = (uint8_t)type;
	x.fam = 4;
	return x;
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[200], dir[64];
	lw_node *n;
	lw_cand in[3], out[8], g[MAXCANDS];
	uint8_t wire[256];
	int nc, i, w, hit;

	if (argc < 2)
		return 2;
	snprintf(dir, sizeof dir, "%s", argv[1]);		/* a directory the caller made and removes */
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", dir);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", dir);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", dir);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.maxpeers = 8;
	c.no_nat_pmp = 1;
	c.cand_loopback = 1;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		return 2;
	}
	n->now = 1000.0;

	/* three addresses of the other end: one inside the "jack" 127.0.0.0/8, one in a private net, one public */
	in[0] = mk(127, 9, 9, 9, 1000, CAND_HOST);
	in[1] = mk(192, 168, 152, 2, 2000, CAND_HOST);
	in[2] = mk(203, 0, 113, 9, 3000, CAND_REFL);
	w = cands_put(n, wire, in, 3);

	/* a node on a socket (or anything but a jack) keeps all three */
	n->cand_skipped = 0;
	check(cands_get(n, wire, (size_t)w, out, &nc, 0) == w && nc == 3 && n->cand_skipped == 0, "not a jack: every address of the other end is kept");

	/* a jack on an interface with 127.0.0.1/8: the one inside it goes, the others stay, in order */
	snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "jack");
	snprintf(n->cfg.port_arg, sizeof n->cfg.port_arg, "lo");
	snprintf(n->cfg.underlay, sizeof n->cfg.underlay, "lwnone0");		/* a device that does not exist: no underlay socket can be had */
	n->cand_skipped = 0;
	n->cand_skip_said = 0;
	check(under_fd(n) < 0, "an underlay device that is not there gives no socket");
	check(cands_get(n, wire, (size_t)w, out, &nc, 0) == w && nc == 2, "a jack with no underlay: the address inside its own network is dropped");
	check(nc == 2 && out[0].ip[0] == 192 && out[0].port == 2000 && out[1].ip[0] == 203 && out[1].port == 3000, "and the others stay, in order");
	check(n->cand_skipped == 1 && n->cand_skip_said == 1000.0, "it is counted and said once");
	n->now = 1010.0;
	cands_get(n, wire, (size_t)w, out, &nc, 0);
	check(n->cand_skipped == 2 && n->cand_skip_said == 1000.0, "counted every time, but said only now and then");
	n->now = 1100.0;
	cands_get(n, wire, (size_t)w, out, &nc, 0);
	check(n->cand_skip_said == 1100.0, "and said again after a minute");

	/* the jack's own address is not offered to the other end; those of other interfaces are */
	n->cfg.cand_loopback = 1;
	nc = gather(n, g, MAXCANDS);
	for (i = 0, hit = 0; i < nc; i++)
		if (g[i].fam == 4 && g[i].ip[0] == 127)
			hit = 1;
	check(!hit, "a jack's own address is left out of what is offered");
	snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "sock");
	nc = gather(n, g, MAXCANDS);
	for (i = 0, hit = 0; i < nc; i++)
		if (g[i].fam == 4 && g[i].ip[0] == 127)
			hit = 1;
	check(hit, "but on a node that is not a jack, the same address is offered");

	/* with an underlay socket the address is kept, and what is sent to it leaves through that socket */
	snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "jack");
	snprintf(n->cfg.port_arg, sizeof n->cfg.port_arg, "lo");
	snprintf(n->cfg.underlay, sizeof n->cfg.underlay, "lo");
	if (n->uudp >= 0)
		close(n->uudp);
	n->uudp = -1;
	n->cand_skipped = 0;
	if (under_fd(n) < 0) {
		printf("skip underlay socket tests: SO_BINDTODEVICE is refused here\n");
	} else {
		struct sockaddr_in ra, la, from;
		socklen_t ll = sizeof la, fl = sizeof from;
		int rfd = socket(AF_INET, SOCK_DGRAM, 0);
		char got[8];

		check(cands_get(n, wire, (size_t)w, out, &nc, 0) == w && nc == 3 && n->cand_skipped == 0 && out[0].ip[0] == 127,
		      "a jack with an underlay socket: the address is kept");
		memset(&ra, 0, sizeof ra);
		ra.sin_family = AF_INET;
		ra.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		bind(rfd, (struct sockaddr *)&ra, sizeof ra);
		getsockname(rfd, (struct sockaddr *)&ra, &ll);
		usend(n, n->udp, "hello", 5, (struct sockaddr *)&ra, sizeof ra);
		usleep(50000);
		memset(got, 0, sizeof got);
		check(recvfrom(rfd, got, sizeof got, MSG_DONTWAIT, (struct sockaddr *)&from, &fl) == 5 && !strcmp(got, "hello"), "a packet for an address inside the jack's network arrives");
		getsockname(n->uudp, (struct sockaddr *)&la, &ll);
		check(from.sin_port == la.sin_port && from.sin_port != htons(n->udp_port), "and it left through the underlay socket, not the node's own");
		{
			lw_peer p;
			struct sockaddr_in *e = (struct sockaddr_in *)&p.ep;

			memset(&p, 0, sizeof p);
			*e = ra;
			check(peer_fd(n, &p) == n->uudp, "a peer whose address is inside the jack's network is sent to through it");
			e->sin_addr.s_addr = htonl(0xC0000201);		/* 192.0.2.1 */
			check(peer_fd(n, &p) == n->udp, "and one outside it through the node's own socket");
		}
		close(rfd);
	}

	/* a route through the jack makes its network one of the jack's: the engine's own traffic for it must not go into the tunnel either (docs/adr/0044) */
	{
		char rf[160];
		FILE *f;
		uint8_t in1[4] = { 203, 0, 113, 9 }, out1[4] = { 198, 51, 100, 1 }, mid[4] = { 8, 8, 8, 8 };

		snprintf(rf, sizeof rf, "%s/route", dir);
		f = fopen(rf, "w");
		fprintf(f, "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n");
		fprintf(f, "lo\t007100CB\t0100007F\t0003\t0\t0\t0\t00FFFFFF\t0\t0\t0\n");	/* 203.0.113.0/24 via the jack */
		fprintf(f, "eth9\t006433C6\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n");	/* 198.51.100.0/24 on another card */
		fclose(f);
		snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "jack");
		snprintf(n->cfg.port_arg, sizeof n->cfg.port_arg, "lo");
		snprintf(n->cfg.routes_file, sizeof n->cfg.routes_file, "%s", rf);
		jnets_refresh(n);
		check(jack_net_has(n, 4, in1), "a network routed through the jack counts as the jack's own");
		check(!jack_net_has(n, 4, out1), "one routed through another card does not");
		check(!jack_net_has(n, 4, mid), "and the rest of the world does not");
		f = fopen(rf, "a");
		fprintf(f, "lo\t00000000\t0100007F\t0003\t0\t0\t0\t00000080\t0\t0\t0\n");	/* 0.0.0.0/1 via the jack: half of everything */
		fclose(f);
		jnets_refresh(n);
		check(jack_net_has(n, 4, mid), "a route for half of everything (0.0.0.0/1) covers 8.8.8.8: the switchboard and every peer there would leave by the underlay");
		n->cfg.routes_file[0] = 0;
		jnets_refresh(n);
	}

	/* everything through the line (docs/adr/0045): every socket of ours tied to the real device, one per family */
	{
		char e2[200];
		int t;

		n->cfg.everything = 1;
		snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "sock");
		n->tie[0] = 0;
		check(under_tie(n, e2, sizeof e2) < 0 && strstr(e2, "needs a jack"), "everything through the line is refused without a jack");
		snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "jack");
		snprintf(n->cfg.port_arg, sizeof n->cfg.port_arg, "lo");
		snprintf(n->cfg.underlay, sizeof n->cfg.underlay, "lo");
		check(under_tie(n, e2, sizeof e2) == 0 && !strcmp(n->tie, "lo"), "on a jack, the device to tie to is the underlay's");
		t = socket(AF_INET, SOCK_DGRAM, 0);
		if (tie_fd(n, t) < 0)
			printf("skip tie tests: SO_BINDTODEVICE is refused here\n");
		else {
			char dv[32] = "";
			socklen_t dl = sizeof dv;

			check(getsockopt(t, SOL_SOCKET, SO_BINDTODEVICE, dv, &dl) == 0 && !strcmp(dv, "lo"), "a socket of ours is tied to it");
			check(under_fd(n) == n->udp, "and no second socket is opened: the one socket keeps its one NAT mapping");
		}
		close(t);
		n->tie[0] = 0;
		n->cfg.underlay[0] = 0;
		{
			int r = under_tie(n, e2, sizeof e2);

			check(r == 0 ? n->tie[0] != 0 : strstr(e2, "no default route") != 0, "with no device named, the default route's device, or a plain refusal when there is none");
		}
		n->cfg.everything = 0;
		n->tie[0] = 0;
	}

	/* an interface that does not exist: nothing is dropped */
	snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "jack");
	snprintf(n->cfg.port_arg, sizeof n->cfg.port_arg, "lwnone0");
	n->cand_skipped = 0;
	check(cands_get(n, wire, (size_t)w, out, &nc, 0) == w && nc == 3 && n->cand_skipped == 0, "a jack that is not there drops nothing");

	if (bad)
		printf("FAILED\n");
	else
		printf("all jacknet checks passed\n");
	return bad;
}
