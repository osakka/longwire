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

#define PASSES(frame_len, what) do { n->now += 0.5; tx = n->local_tx; sw_from_peer(n, 1, f, (frame_len)); check(n->local_tx == tx + 1, what); } while (0)
#define REFUSED(frame_len, what) do { n->now += 0.5; tx = n->local_tx; sw_from_peer(n, 1, f, (frame_len)); check(n->local_tx == tx, what); } while (0)

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

	/* the mesh does not knock on an absent member's door every minute for ever: 8, 16, 32 s, then 60, 120, 240, 480, and a quarter of an hour at most */
	{
		static const double want[] = { 0, 8, 16, 32, 60, 120, 240, 480, 900, 900, 900 };
		int t, okw = 1;

		for (t = 1; t <= 10; t++)
			if (mesh_wait(t, 0) != want[t] || mesh_wait(t, 2) != want[t])
				okw = 0;
		check(okw, "the wait before calling an absent member again is 8, 16, 32, 60, 120, 240, 480 s and then a quarter of an hour, not a minute for ever");
		okw = 1;
		for (t = 1; t <= 4; t++)
			if (mesh_wait(t, 3) != want[t])
				okw = 0;
		check(okw && mesh_wait(9, 3) == 1920.0 && mesh_wait(20, 3) == 21600.0 && mesh_wait(20, 100) == 21600.0,
		      "a member that has refused three of the mesh's calls (a leaf) is called less and less often, up to six hours (ADR 0097)");
	}

	/* a pair of boxes is one site: the member we asked for says who is on its own segment, and they may answer DHCP for us too */
	{
		uint8_t mate[32], vouch[1 + 32 + 1], stranger[32];
		int d;

		n->cfg.dhcp_guard = 1;
		n->now = 3000.0;
		memset(mate, 0x61, 32);
		memset(stranger, 0x62, 32);
		memset(n->peers[2].key_id, 0x61, 32);
		n->peers[2].used = 1;
		n->peers[2].cur.valid = 1;
		memset(n->peers[3].key_id, 0x62, 32);
		n->peers[3].used = 1;
		n->peers[3].cur.valid = 1;
		vouch[0] = 1;
		memcpy(vouch + 1, mate, 32);
		vouch[33] = 1;					/* and which of the pair it is: active (ADR 0111) */
		n->peers[1].asked = 0;
		n->peers[1].number[0] = 0;
		site_vouch(n, &n->peers[1], vouch, sizeof vouch);			/* from a member we did not ask for: not believed */
		l = v4(f, 67, 68, 0, 0);
		n->now += 1;
		tx = n->local_tx;
		sw_from_peer(n, 2, f, l);
		check(n->local_tx == tx, "a member that has not been asked for cannot vouch for a box: its site-mate's DHCP reply is still refused");
		n->peers[1].asked = 1;
		site_vouch(n, &n->peers[1], vouch, sizeof vouch);			/* from the member we asked for: believed */
		n->now += 1;
		l = v4(f, 67, 68, 0, 0);
		f[100] = 1;
		tx = n->local_tx;
		sw_from_peer(n, 2, f, l);
		check(n->local_tx == tx + 1, "the member we asked for says a box is on its own segment: that box may answer DHCP for us (the active one of a pair carries the lease)");
		n->now += 1;
		f[100] = 2;
		tx = n->local_tx;
		sw_from_peer(n, 3, f, l);
		check(n->local_tx == tx, "and a box it did not name still may not");
		n->peers[1].used = 0;							/* the member we asked for goes: what it said is remembered, the other box may still answer */
		n->now += 1;
		f[100] = 3;
		tx = n->local_tx;
		sw_from_peer(n, 2, f, l);
		check(n->local_tx == tx + 1, "when the member we asked for is gone, the box it named still answers: the lease is renewed through the survivor");
		n->peers[1].used = 1;
		(void)d;
		{						/* the numbers a person has chosen (the phone's list) may answer DHCP whether or not they have been dialled */
			char out[200];
			uint64_t t2;

			memset(n->peers[3].number, 0, sizeof n->peers[3].number);
			snprintf(n->peers[3].number, sizeof n->peers[3].number, "312884076");
			n->now += 1;
			f[100] = 4;
			t2 = n->local_tx;
			sw_from_peer(n, 3, f, l);
			check(n->local_tx == t2, "a box whose number was never dialled or named is refused");
			node_cmd(n, "asked 312-884-076", out, sizeof out);
			check(strstr(out, "\"asked\":\"312-884-076\"") != 0, "the `asked` word takes a number and says it back");
			node_cmd(n, "asked 312-884-076", out, sizeof out);
			node_cmd(n, "asked bogus", out, sizeof out);
			check(strstr(out, "error") != 0, "and refuses what is not a number");
			n->now += 1;
			f[100] = 5;
			t2 = n->local_tx;
			sw_from_peer(n, 3, f, l);
			check(n->local_tx == t2 + 1, "a member with a number the person chose may answer DHCP");
		}
	}

	/* what a bridge never carries (docs/adr/0083): spanning tree and the rest of the link-local control frames, in both directions */
	{
		static const uint8_t bpdu[6] = { 0x01, 0x80, 0xc2, 0, 0, 0 }, lldp[6] = { 0x01, 0x80, 0xc2, 0, 0, 0x0e }, last[6] = { 0x01, 0x80, 0xc2, 0, 0, 0x0f },
				     beyond[6] = { 0x01, 0x80, 0xc2, 0, 0, 0x10 }, cdp[6] = { 0x01, 0, 0x0c, 0xcc, 0xcc, 0xcc }, pvst[6] = { 0x01, 0, 0x0c, 0xcc, 0xcc, 0xcd },
				     ipmc[6] = { 0x01, 0, 0x5e, 0, 0, 1 }, H[6] = { 2, 0, 0, 0, 0, 0x11 }, R[6] = { 2, 0, 0, 0, 0, 0x22 };
		const uint8_t *ctl[] = { bpdu, lldp, last, cdp, pvst };
		uint64_t dc, fl;
		int k, all = 1;
		mac_ent *e;

		n->cfg.dhcp_guard = 1;
		snprintf(n->cfg.port_kind, sizeof n->cfg.port_kind, "none");
		for (k = 0; k < 5; k++) {
			memset(f, 0xab, 80);
			memcpy(f, ctl[k], 6);
			memcpy(f + 6, SRC, 6);
			put16(f + 12, 0x0026);			/* an 802.3 length, as a BPDU has */
			dc = n->drop_ctrl;
			fl = n->flood;
			tx = n->local_tx;
			sw_from_local(n, f, 60);
			sw_from_peer(n, 1, f, 60);
			if (n->drop_ctrl != dc + 2 || n->flood != fl || n->local_tx != tx)
				all = 0;
		}
		check(all, "spanning tree, LLDP, the last of 01:80:C2:00:00:0x, CDP and PVST+ cross in neither direction: from the LAN they are not sent over the line, from a member they are not put on the LAN");
		for (k = 0; k < 2; k++) {
			const uint8_t *pass = k ? ipmc : beyond;

			static const uint8_t other[6] = { 2, 0, 0, 0, 0, 0x33 }, mine[6] = { 2, 0, 0, 0, 0, 0x44 };

			memset(f, 0xab, 80);
			memcpy(f, pass, 6);
			memcpy(f + 6, mine, 6);
			put16(f + 12, 0x0800);
			tx = n->local_tx;
			fl = n->flood;
			sw_from_local(n, f, 60);
			memcpy(f + 6, other, 6);
			sw_from_peer(n, 1, f, 60);
			check(n->local_tx == tx + 1 && n->flood == fl + 1, k ? "IP multicast (01:00:5E) is carried" : "01:80:C2:00:00:10, past the reserved range, is carried");
		}

		/* a frame that has come round: a member hands us the bytes of a frame that came off our LAN a moment ago. Three times in five seconds it is on our segment, and nothing is carried between it and the LAN */
		n->now = 5000.0;
		memset(f, 0xab, 80);
		memcpy(f, BC, 6);
		memcpy(f + 6, H, 6);
		put16(f + 12, 0x0800);
		fl = n->flood;
		sw_from_local(n, f, 60);			/* H speaks on our LAN: carried over the line, and H is local */
		e = mac_find(n, H, 0);
		check(n->flood == fl + 1 && e && e->port == -1, "a host that speaks on the LAN is carried over the line, and is local");
		tx = n->local_tx;
		dc = n->drop_echo;
		sw_from_peer(n, 1, f, 60);			/* the same bytes from a member: the line's copy of what is already on our LAN */
		check(n->local_tx == tx && n->drop_echo == dc + 1 && !n->peers[1].mate_until, "the same bytes from a member a moment later are not put on the LAN: another box's copy of a frame we already carried");
		for (k = 0; k < 2; k++) {
			f[40] = (uint8_t)(k + 1);
			n->now += 0.5;
			sw_from_local(n, f, 60);
			sw_from_peer(n, 1, f, 60);
		}
		check(n->peers[1].mate_until > n->now, "three such frames in five seconds make that member a segment-mate: it is on our own LAN");
		tx = n->local_tx;
		f[41] = 0x77;				/* a different frame from it */
		f[7] ^= 0x10;
		sw_from_peer(n, 1, f, 60);
		check(n->local_tx == tx, "nothing a segment-mate carries is put on the LAN, whatever its bytes (the host has not yet been heard, or is another's)");
		{
			uint64_t t0 = n->peers[1].tx_frames;

			f[42] = 0x31;
			f[7] ^= 0x20;
			sw_from_local(n, f, 60);
			check(n->peers[1].tx_frames == t0, "and nothing from the LAN is carried to it");
		}
		n->now += 61.0;
		check(n->peers[1].mate_until < n->now, "a minute later it is a segment-mate no more");
		memset(n->peers[1].number, 0, sizeof n->peers[1].number);
		n->peers[1].mate_hits = 0;
		n->peers[1].mate_until = 0;

		/* the other way round: a member's frame is put on the LAN, and its bytes come back off it (another box put them there) */
		memcpy(f + 6, R, 6);
		f[40] ^= 0x55;
		f[44] = 0x21;
		sw_from_peer(n, 1, f, 60);			/* R, remote: carried to the LAN */
		fl = n->flood;
		dc = n->drop_echo;
		sw_from_local(n, f, 60);			/* its bytes, off the LAN, a moment later */
		e = mac_find(n, R, 0);
		check(n->flood == fl && n->drop_echo == dc + 1 && e && e->port == 1, "the bytes of a frame we have just put on the LAN, coming off it, are not sent back, and nothing is learned from them (R stays behind the member)");
		n->now += 2.0;
		sw_from_local(n, f, 60);
		e = mac_find(n, R, 0);
		check(n->flood == fl + 1 && e && e->port == -1, "after a second the same bytes off the LAN are a new frame: carried, and the host is local");

		/* what is never touched: identical frames from one side, in a burst, a retransmission */
		{
			int ok1 = 1, ok2 = 1;

			memset(f, 0xcd, 80);
			memcpy(f, BC, 6);
			memcpy(f + 6, H, 6);
			put16(f + 12, 0x0800);
			for (k = 0; k < 10; k++) {
				n->now += 0.005;
				fl = n->flood;
				sw_from_local(n, f, 60);
				if (n->flood != fl + 1)
					ok1 = 0;
			}
			{
				static const uint8_t far_[6] = { 2, 0, 0, 0, 0, 0x66 };

				memcpy(f + 6, far_, 6);
			}
			n->now += 5.0;
			for (k = 0; k < 10; k++) {
				n->now += 0.005;
				tx = n->local_tx;
				sw_from_peer(n, 1, f, 60);
				if (n->local_tx != tx + 1)
					ok2 = 0;
			}
			check(ok1 && ok2, "ten identical frames five milliseconds apart, from the LAN and from a member, all go through: only a frame meeting its own copy from the other side is touched");
		}
	}

	printf(bad ? "FAILED\n" : "all guard checks passed\n");
	return bad;
}
