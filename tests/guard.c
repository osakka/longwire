/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The guard (docs/adr/0080): what the line may say to a node's own side. A DHCP reply, a DHCPv6 reply or a router advertisement from a member we did not ask for is refused, through the tricks that get past a
 * switch that only reads the first header: a priority tag, an extension header, a chain that runs off the frame. Frames are put into the switch by hand; the port is none, so what reaches the LAN is counted.
 */
#include "../engine/all.c"

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

static const uint8_t SRC[6] = { 2, 0, 0, 0, 0, 0x77 }, BC[6] = { 255, 255, 255, 255, 255, 255 }, M6[6] = { 0x33, 0x33, 0, 0, 0, 1 };

/* Ethernet, optional tags, IPv4 and UDP sp to dp; the first fragment unless `frag` */
static size_t v4(uint8_t *f, int sp, int dp, int tags, int frag)
{
	size_t o = 12;
	int i;

	memset(f, 0, 400);
	memcpy(f, BC, 6);
	memcpy(f + 6, SRC, 6);
	for (i = 0; i < tags; i++) {
		put16(f + o, i ? 0x88a8 : 0x8100);
		o += 4;
	}
	put16(f + o, 0x0800);
	o += 2;
	f[o] = 0x45;
	put16(f + o + 2, 20 + 8 + 240);
	put16(f + o + 6, (unsigned)(frag ? 185 : 0));
	f[o + 8] = 64;
	f[o + 9] = 17;
	put32(f + o + 12, 0xc0a80101u);
	put32(f + o + 16, 0xffffffffu);
	put16(f + o + 20, (unsigned)sp);
	put16(f + o + 22, (unsigned)dp);
	put16(f + o + 24, 8 + 240);
	return o + 20 + 8 + 240;
}

/* Ethernet and IPv6, then `ext` extension headers of 8 bytes each (the type nh0 first, each saying the next), then the upper layer: UDP sp to dp (nh 17) or ICMPv6 of type icmp (nh 58) */
static size_t v6(uint8_t *f, int nh, int sp, int dp, int icmp, int ext, int ext_nh)
{
	size_t o = 14, p;
	int i;

	memset(f, 0, 400);
	memcpy(f, M6, 6);
	memcpy(f + 6, SRC, 6);
	put16(f + 12, 0x86dd);
	f[o] = 0x60;
	f[o + 6] = (uint8_t)(ext ? ext_nh : nh);
	f[o + 7] = 255;
	f[o + 8] = 0xfe;
	f[o + 9] = 0x80;
	f[o + 23] = 1;
	f[o + 24] = 0xff;
	f[o + 25] = 2;
	f[o + 39] = 1;
	p = o + 40;
	for (i = 0; i < ext; i++) {
		f[p] = (uint8_t)(i == ext - 1 ? nh : ext_nh);
		f[p + 1] = 0;
		p += 8;
	}
	if (nh == 17) {
		put16(f + p, (unsigned)sp);
		put16(f + p + 2, (unsigned)dp);
		put16(f + p + 4, 40);
		p += 48;
	} else {
		f[p] = (uint8_t)icmp;
		p += 40;
	}
	put16(f + o + 4, (unsigned)(p - o - 40));
	return p;
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[200];
	lw_node *n;
	uint8_t f[400], key[32];
	size_t l;
	uint64_t tx;

	if (argc < 2)
		return 2;
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", argv[1]);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", argv[1]);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", argv[1]);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.maxpeers = 8;
	c.no_nat_pmp = 1;
	c.dhcp_guard = 1;				/* on, whatever the port: a socket port stands in for the LAN */
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		return 2;
	}
	n->now = 1000.0;
	n->peers[1].used = 1;
	n->peers[1].cur.valid = 1;
	memset(n->peers[1].key_id, 0x42, 32);

#define PASSES(frame_len, what) do { tx = n->local_tx; sw_from_peer(n, 1, f, (frame_len)); check(n->local_tx == tx + 1, what); } while (0)
#define REFUSED(frame_len, what) do { tx = n->local_tx; sw_from_peer(n, 1, f, (frame_len)); check(n->local_tx == tx, what); } while (0)

	/* DHCPv4 */
	l = v4(f, 67, 68, 0, 0);
	REFUSED(l, "a DHCP reply from a member we did not ask for is refused");
	check(n->dhcp_guarded == 1 && !mac_find(n, SRC, 0), "...it is counted, and nothing is learned from it");
	l = v4(f, 68, 67, 0, 0);
	PASSES(l, "a DHCP request (68 to 67) is not a server's word: it passes");
	l = v4(f, 5353, 5353, 0, 0);
	PASSES(l, "and so does any other UDP");
	l = v4(f, 67, 68, 1, 0);
	REFUSED(l, "a DHCP reply behind a priority tag (VLAN 0) is refused");
	l = v4(f, 67, 68, 2, 0);
	REFUSED(l, "...and behind two tags");
	l = v4(f, 67, 68, 0, 1);
	PASSES(l, "a fragment that is not the first has no header to read and passes (the first, refused, leaves nothing to put it together with)");

	/* trust: asked, a number dialled, a key named */
	l = v4(f, 67, 68, 0, 0);
	n->peers[1].asked = 1;
	PASSES(l, "a member we asked for may answer");
	n->peers[1].asked = 0;
	snprintf(n->peers[1].number, sizeof n->peers[1].number, "312884076");
	snprintf(n->asked[0], sizeof n->asked[0], "312884076");
	PASSES(l, "a member whose number a person dialled may answer, though the other end made the last call");
	n->asked[0][0] = 0;
	REFUSED(l, "...and not once the number is forgotten");
	memcpy(n->cfg.dhcp_servers[0], n->peers[1].key_id, 32);
	n->cfg.n_dhcp = 1;
	PASSES(l, "a member the configuration names may answer");
	n->cfg.n_dhcp = 0;
	REFUSED(l, "...and not when it is not named");

	/* IPv6: DHCPv6 and router advertisements, plain and behind extension headers */
	l = v6(f, 17, 547, 546, 0, 0, 0);
	REFUSED(l, "a DHCPv6 reply is refused");
	l = v6(f, 17, 546, 547, 0, 0, 0);
	PASSES(l, "a DHCPv6 request passes");
	l = v6(f, 58, 0, 0, 134, 0, 0);
	REFUSED(l, "a router advertisement is refused");
	l = v6(f, 58, 0, 0, 137, 0, 0);
	REFUSED(l, "so is a redirect");
	l = v6(f, 58, 0, 0, 135, 0, 0);
	PASSES(l, "a neighbour solicitation (135) passes");
	l = v6(f, 58, 0, 0, 134, 1, 0);
	REFUSED(l, "a router advertisement behind a hop-by-hop header is refused");
	l = v6(f, 58, 0, 0, 134, 3, 60);
	REFUSED(l, "...behind three destination-option headers");
	l = v6(f, 17, 547, 546, 0, 2, 43);
	REFUSED(l, "a DHCPv6 reply behind routing headers is refused");
	l = v6(f, 58, 0, 0, 134, 1, 0);
	l -= 40;						/* the chain runs off the end of the frame: the upper layer cannot be found */
	REFUSED(l - 8, "an IPv6 frame whose headers run off the frame is refused rather than guessed at");
	l = v6(f, 17, 5353, 5353, 0, 0, 0);
	PASSES(l, "ordinary IPv6 UDP passes");

	/* the guard is not on for a socket port unless it is made to be; by the port it is */
	n->cfg.dhcp_guard = 0;
	l = v4(f, 67, 68, 0, 0);
	PASSES(l, "by the port, a socket port (the tests' own) is not guarded");
	snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "splice");
	REFUSED(l, "...and a splice is");
	snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "jack");
	REFUSED(l, "...and a jack");
	snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "member");
	REFUSED(l, "...and a phone");
	n->cfg.dhcp_guard = 2;
	PASSES(l, "and off means off");
	(void)key;

	printf(bad ? "FAILED\n" : "all guard checks passed\n");
	return bad;
}
