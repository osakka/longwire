/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Address claims (docs/adr/0090): an address that a host of the node's own LAN has said it holds (ARP, neighbour discovery) may not be claimed by a member of the line, whatever the
 * source MAC of the frame, behind a priority tag or an extension header. Frames are put in front of the switch directly.
 */
#include "../engine/all.c"

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

static const uint8_t HOST[6] = { 2, 0, 0, 0, 0, 0x10 }, MEMBER[6] = { 2, 0, 0, 0, 0, 0x77 }, BC[6] = { 255, 255, 255, 255, 255, 255 };

/* An ARP request from `mac`, saying its sender address is ip. */
static size_t arp(uint8_t *f, const uint8_t *mac, uint32_t ip)
{
	memset(f, 0, 64);
	memcpy(f, BC, 6);
	memcpy(f + 6, mac, 6);
	put16(f + 12, 0x0806);
	put16(f + 14, 1);
	put16(f + 16, 0x0800);
	f[18] = 6;
	f[19] = 4;
	put16(f + 20, 1);
	memcpy(f + 22, mac, 6);
	put32(f + 28, ip);
	put32(f + 38, 0xc0a80101u);
	return 42;
}

/* An IPv6 neighbour message from `mac`: type 135 (src is the asker's address) or 136 (tgt is the sender's); tags 0..1 priority tags, ext 1 a destination options header. */
static size_t nd(uint8_t *f, const uint8_t *mac, int type, const uint8_t *src, const uint8_t *tgt, int tags, int ext)
{
	size_t o = 12, p;
	int i;

	memset(f, 0, 200);
	memcpy(f, BC, 6);
	memcpy(f + 6, mac, 6);
	for (i = 0; i < tags; i++) {
		put16(f + o, 0x8100);
		o += 4;
	}
	put16(f + o, 0x86dd);
	o += 2;
	f[o] = 0x60;
	f[o + 6] = ext ? 60 : 58;
	f[o + 7] = 255;
	memcpy(f + o + 8, src, 16);
	f[o + 24] = 0xff;
	f[o + 25] = 2;
	f[o + 39] = 1;
	p = o + 40;
	if (ext) {
		f[p] = 58;
		f[p + 1] = 0;
		p += 8;
	}
	f[p] = (uint8_t)type;
	memcpy(f + p + 8, tgt, 16);
	put16(f + o + 4, (unsigned)(p + 24 - o - 40));
	return p + 24;
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[200];
	lw_node *n;
	uint8_t f[300], a6[16] = { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, b6[16] = { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 }, none[16] = { 0 };
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
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		return 2;
	}
	n->now = 1000.0;
	n->peers[1].used = 1;
	n->peers[1].cur.valid = 1;
	memset(n->peers[1].key_id, 0x42, 32);

#define PASSES(frame_len, what) do { n->now += 0.5; tx = n->local_tx; sw_from_peer(n, 1, f, (frame_len)); check(n->local_tx == tx + 1, what); } while (0)
#define REFUSED(frame_len, what) do { n->now += 0.5; tx = n->local_tx; sw_from_peer(n, 1, f, (frame_len)); check(n->local_tx == tx, what); } while (0)

	/* before any host has spoken, nothing is a local host's */
	l = arp(f, MEMBER, 0xc0a8010au);
	PASSES(l, "a member's ARP for an address no local host has said is let through");
	/* a local host says 192.168.1.10 */
	l = arp(f, HOST, 0xc0a8010au);
	sw_from_local(n, f, l);
	l = arp(f, MEMBER, 0xc0a8010au);
	REFUSED(l, "a member that says it is 192.168.1.10, which a host of our LAN has just said, is refused");
	check(n->drop_claim == 1, "...and counted");
	l = arp(f, MEMBER, 0xc0a80132u);
	PASSES(l, "an address no local host has said (192.168.1.50) passes");
	l = arp(f, MEMBER, 0);
	PASSES(l, "an ARP probe (sender 0.0.0.0) says nothing and passes");
	l = arp(f, MEMBER, 0xc0a8010au);
	n->now += CLAIM_FOR + 1;
	PASSES(l, "once the host has been silent for two minutes the address is nobody's claim");
	/* IPv6: a local host's neighbour solicitation says its address */
	l = nd(f, HOST, 135, a6, none, 0, 0);
	sw_from_local(n, f, l);
	l = nd(f, MEMBER, 136, b6, a6, 0, 0);
	REFUSED(l, "a member's neighbour advertisement for a local host's IPv6 address is refused");
	l = nd(f, MEMBER, 136, b6, a6, 1, 0);
	REFUSED(l, "...behind a priority tag");
	l = nd(f, MEMBER, 136, b6, a6, 0, 1);
	REFUSED(l, "...behind a destination options header");
	l = nd(f, MEMBER, 135, a6, none, 0, 0);
	REFUSED(l, "a member's solicitation FROM that address is refused too");
	check(n->drop_claim == 5, "...all counted");
	l = nd(f, MEMBER, 136, b6, b6, 0, 0);
	PASSES(l, "an advertisement for an address no local host holds passes");
	l = nd(f, MEMBER, 135, none, b6, 0, 0);
	PASSES(l, "a solicitation from the unspecified address (duplicate detection) says nothing and passes");
	printf(bad ? "\nFAILED\n" : "\nall claim checks passed\n");
	return bad;
}
