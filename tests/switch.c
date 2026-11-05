/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * What a member of the line can and cannot do to the switch (docs/adr/0063): a frame of the site boxes' own EtherType never comes in from the line, and a peer's frame cannot
 * take the address of a host that is speaking on our own LAN. Frames are put into the switch by hand; the port is none, so what reaches the LAN is counted.
 */
#include "../engine/all.c"

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

static void frame(uint8_t *f, const uint8_t *dst, const uint8_t *src, unsigned type)
{
	memcpy(f, dst, 6);
	memcpy(f + 6, src, 6);
	f[12] = (uint8_t)(type >> 8);
	f[13] = (uint8_t)type;
	memset(f + 14, 0xab, 60);
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[200];
	lw_node *n;
	uint8_t f[74];
	const uint8_t host[6] = { 2, 0, 0, 0, 0, 0x11 }, other[6] = { 2, 0, 0, 0, 0, 0x22 }, bc[6] = { 255, 255, 255, 255, 255, 255 };
	mac_ent *e;
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

	/* the site boxes' own frames never come in from the line */
	tx = n->local_tx;
	frame(f, bc, other, 0x88B5);
	sw_from_peer(n, 1, f, sizeof f);
	check(n->local_tx == tx, "a frame of the site boxes' own EtherType (0x88B5) from the line is dropped, not put on the LAN");
	frame(f, bc, other, 0x0800);
	sw_from_peer(n, 1, f, sizeof f);
	check(n->local_tx == tx + 1, "while an ordinary frame from the line is delivered");

	/* a host on our LAN speaks; a peer says it is that host */
	frame(f, bc, host, 0x0800);
	sw_from_local(n, f, sizeof f);
	check((e = mac_find(n, host, 0)) && e->port == -1, "a host on our own LAN is learned as local");
	n->now = 1003.0;
	frame(f, bc, host, 0x0800);
	sw_from_peer(n, 1, f, sizeof f);
	check((e = mac_find(n, host, 0)) && e->port == -1, "a peer's frame that claims its address a moment later does not take it");
	n->now = 1020.0;
	frame(f, bc, host, 0x0800);
	sw_from_peer(n, 1, f, sizeof f);
	check((e = mac_find(n, host, 0)) && e->port == 1, "once the host has been silent for a while the address can move, as a host that moved to another site does");
	n->now = 1021.0;
	frame(f, bc, host, 0x0800);
	sw_from_local(n, f, sizeof f);
	check((e = mac_find(n, host, 0)) && e->port == -1, "and the host speaking here takes it back at once");

	/* an eviction dated far ahead is refused: it would bar the key for as long as that date (a member may evict a member; the time is its own word) */
	{
		lw_id m;
		uint8_t pl[8 + 32 + 8 + 32 + 64], msg[53], v1[32], v2[32];
		uint64_t t;
		int k;

		memset(&m, 0, sizeof m);
		lw_rand(m.seed, 32);
		lw_id_derive(&m);
		lw_rand(v1, 32);
		lw_rand(v2, 32);
		n->line.have = 1;
		lw_rand(n->line.id, 8);
		n->peers[2].used = 1;
		n->peers[2].cur.valid = 1;
		memcpy(n->peers[2].key_id, m.key_id, 32);
		memcpy(n->peers[2].bundle, m.bundle, LW_BUNDLE);		/* the session's own bundle is what the eviction is checked against */
		n->peers[2].have_bundle = 1;
		mem_add(n, "111111111", m.key_id, m.bundle + ED_OFF, 0, v1);
		mem_add(n, "222222222", v1, v1, 0, v1);
		mem_add(n, "333333333", v2, v2, 0, v2);
		for (k = 0; k < 2; k++) {
			const uint8_t *victim = k ? v2 : v1;

			t = k ? (uint64_t)time(0) : (uint64_t)time(0) + 5 * 365 * 86400ull;	/* the second is dated now, the first five years on */
			memcpy(pl, n->line.id, 8);
			memcpy(pl + 8, victim, 32);
			put64(pl + 40, t);
			memcpy(pl + 48, m.key_id, 32);
			memcpy(msg, pl, 48);
			memcpy(msg + 48, "evict", 5);
			lw_sign(&m, msg, 53, pl + 80);
			handle_evict(n, &n->peers[2], pl, sizeof pl);
			if (!k)
				check(mem_find(n, v1) != 0, "an eviction dated five years ahead is refused: the member is still there");
			else
				check(mem_find(n, v2) == 0, "and the same eviction dated now is applied");
		}
	}

	/* what a switchboard says in a refusal is shown on a terminal: printable ASCII and nothing else */
	{
		uint8_t pl[40];

		n->sb = 0;
		n->peers[0].used = 1;
		n->peers[0].is_sb = 1;
		memcpy(pl, "a\x1b]0;x\x07\n b\xc3\xa9", 13);
		node_msg(n, &n->peers[0], M_REFUSED, pl, 13);
		check(!strpbrk(n->reg_why, "\\\x1b\x07\n") && strstr(n->reg_why, "a?]0;x??"), "a switchboard's refusal text has no control character in it (no terminal escape reaches the person)");
	}

	/* a table full of peers with no session: the one a call is waiting on is not the one recycled */
	{
		uint8_t bundle[LW_BUNDLE];
		lw_peer *got;
		int k;

		for (k = 0; k < n->maxpeers; k++) {
			memset(&n->peers[k], 0, sizeof n->peers[k]);
			n->peers[k].used = 1;
			n->peers[k].touched = 500.0 + k;		/* peer 0 is the stalest */
			lw_rand(n->peers[k].key_id, 32);
		}
		n->calls[0].state = CALL_PUNCH;
		n->calls[0].peer = 0;
		memset(bundle, 7, sizeof bundle);
		bundle[0] = 0x01;
		got = peer_add(n, bundle);
		check(got && got != &n->peers[0], "a call is waiting on the stalest peer: another is recycled, not that one (the call would otherwise pin its number to a stranger's key)");
		n->calls[0].state = CALL_IDLE;
	}

	/* member records are claims (docs/adr/0064): the key a record carries is the bundle's when we hold the bundle, and a number is only ever one key's */
	{
		uint8_t true_ed[32], forged[32], kid_a[32], kid_b[32], sb[32], bundle[LW_BUNDLE];
		lw_peer *pa;
		lw_member *m;
		int k;

		for (k = 0; k < n->maxpeers; k++)
			memset(&n->peers[k], 0, sizeof n->peers[k]);
		memset(true_ed, 0x11, 32);
		memset(forged, 0x66, 32);
		memset(sb, 0x33, 32);
		lw_rand(kid_a, 32);
		lw_rand(kid_b, 32);
		memset(bundle, 0, sizeof bundle);
		bundle[0] = 0x01;
		memcpy(bundle + ED_OFF, true_ed, 32);
		n->line.nm = 0;
		pa = &n->peers[2];
		pa->used = 1;
		pa->have_bundle = 1;
		memcpy(pa->key_id, kid_a, 32);
		memcpy(pa->bundle, bundle, sizeof bundle);
		snprintf(pa->number, sizeof pa->number, "11-11");

		m = mem_add(n, "11-11", kid_a, forged, 0, sb);
		check(m && !memcmp(m->ed_pub, true_ed, 32), "a record that carries another key than the bundle we hold has the bundle's key, not the record's");
		m = mem_add(n, "22-22", kid_b, forged, 0, sb);
		check(m && !memcmp(m->ed_pub, forged, 32), "a record of a member we hold no bundle of keeps what it says, until we do");
		memcpy(m->key_id, kid_b, 32);
		memcpy(mem_find(n, kid_a)->ed_pub, forged, 32);
		n->line.have = 1;
		line_call_up(n, pa);
		check(!memcmp(mem_find(n, kid_a)->ed_pub, true_ed, 32), "a session coming up writes the member's own key over what any record said");

		check(line_number_is(n, "11-11", kid_a) == 1 && line_number_is(n, "11-11", kid_b) == -1 && line_number_is(n, "99-99", kid_a) == 0,
		      "a number is the member's key (1), another key's (-1), or nobody's (0)");
	}

	/* the exit: never "the lowest number" (any member can offer itself); the one named, if pinned to its key; or the only one */
	{
		uint8_t ka[32], kb[32], sbk[32];
		lw_peer *pa = &n->peers[2], *pb = &n->peers[3];

		lw_rand(ka, 32);
		lw_rand(kb, 32);
		memset(pa, 0, sizeof *pa);
		memset(pb, 0, sizeof *pb);
		pa->used = pb->used = 1;
		pa->cur.valid = pb->cur.valid = 1;
		pa->exit_seen = pb->exit_seen = n->now;
		memcpy(pa->key_id, ka, 32);
		memcpy(pb->key_id, kb, 32);
		snprintf(pa->number, sizeof pa->number, "10-00");	/* the lowest: what an attacker would take */
		snprintf(pb->number, sizeof pb->number, "20-00");
		n->line.nm = 0;
		mem_add(n, "10-00", ka, ka, 0, ka);
		mem_add(n, "20-00", kb, kb, 0, kb);
		n->cfg.exit_through[0] = 0;
		check(exit_pick(n) == -1, "two members offer and none is named: none is used (the lowest number is whoever made one)");
		pa->exit_seen = 0;
		check(exit_pick(n) == 3, "one member offers and none is named: that one");
		pa->exit_seen = n->now;
		snprintf(n->cfg.exit_through, sizeof n->cfg.exit_through, "20-00");
		check(exit_pick(n) == -1, "a member is named, offers, but its number is not pinned to its key here: none");
		home_kid(n, sbk);
		n->npins = 1;
		n->cappins = 1;
		n->pins = calloc(1, sizeof *n->pins);
		snprintf(n->pins[0].number, sizeof n->pins[0].number, "20-00");
		memcpy(n->pins[0].key_id, ka, 32);			/* pinned to another member's key */
		memcpy(n->pins[0].sb, sbk, 32);
		check(exit_pick(n) == -1, "a member is named, but its number is pinned to a different key: none");
		memcpy(n->pins[0].key_id, kb, 32);
		check(exit_pick(n) == 3, "a member is named, offers, and its number is pinned to its key: that one");
		n->npins = 0;
		n->cfg.exit_through[0] = 0;
	}

	printf(bad ? "FAILED\n" : "all switch checks passed\n");
	return bad;
}
