/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * What a member of the line can and cannot do to the switch (docs/adr/0063): a frame of the site boxes' own EtherType never comes in from the line, and a peer's frame cannot
 * take the address of a host that is speaking on our own LAN. Frames are put into the switch by hand; the port is none, so what reaches the LAN is counted.
 */
#include "../engine/all.c"

static int bad;

/* an operator's admit for `kid` in the array `a` of the caller; the line id is `ids` */
#define MKADMIT_FOR(signer, kid_, issued, expires) do { uint8_t mm_[8 + ADM_SIGNED]; memcpy(a, (kid_), 32); a[32] = 0; memcpy(a + ADM_BY, (signer)->bundle + ED_OFF, 32); \
	put64(a + ADM_ISSUED, (issued)); put64(a + ADM_EXPIRES, (expires)); memcpy(mm_, ids, 8); memcpy(mm_ + 8, a, ADM_SIGNED); lw_sign((signer), mm_, sizeof mm_, a + ADM_SIGNED); } while (0)

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

	/* an admit has an end, and a revocation does not outlive it (docs/adr/0065) */
	{
		lw_id op, other;
		uint8_t a[ADMIT_LEN], kid[32], ids[8], r[ROLL_LEN];
		uint64_t now = (uint64_t)time(0), day = 86400;
		lw_peer *pq = &n->peers[4];
		int k;

		memset(&op, 0, sizeof op);
		lw_rand(op.seed, 32);
		lw_id_derive(&op);
		memset(&other, 0, sizeof other);
		lw_rand(other.seed, 32);
		lw_id_derive(&other);
		for (k = 0; k < n->maxpeers; k++)
			memset(&n->peers[k], 0, sizeof n->peers[k]);
		memset(&n->line, 0, sizeof n->line);
		lw_rand(ids, 8);
		memcpy(n->line.id, ids, 8);
		memcpy(n->line.authority, op.bundle + ED_OFF, 32);
		memcpy(n->line.founder, n->id.key_id, 32);
		n->line.managed = n->line.have = 1;
		lw_rand(kid, 32);

#define MKADMIT(signer, issued, expires) do { uint8_t mm_[8 + ADM_SIGNED]; memcpy(a, kid, 32); a[32] = 0; memcpy(a + ADM_BY, (signer)->bundle + ED_OFF, 32); \
	put64(a + ADM_ISSUED, (issued)); put64(a + ADM_EXPIRES, (expires)); memcpy(mm_, ids, 8); memcpy(mm_ + 8, a, ADM_SIGNED); lw_sign((signer), mm_, sizeof mm_, a + ADM_SIGNED); } while (0)
		MKADMIT(&op, now - day, now + 30 * day);
		check(admit_verify(n, a, kid), "an operator's admit that has not ended is good");
		MKADMIT(&op, now - 40 * day, now - 10 * day);
		check(!admit_verify(n, a, kid), "one that has ended is not");
		MKADMIT(&op, now - day, now + 401 * day);
		check(!admit_verify(n, a, kid), "one that lasts longer than 400 days is not an admit");
		MKADMIT(&op, now + 3 * day, now + 30 * day);
		check(!admit_verify(n, a, kid), "one made days in the future is not (an operator's clock far ahead would make it last)");
		MKADMIT(&op, now - day, now - day);
		check(!admit_verify(n, a, kid), "one that ends when it begins is not");
		MKADMIT(&other, now - day, now + 30 * day);
		check(!admit_verify(n, a, kid), "one signed by some other key is not");
		MKADMIT(&op, now - 5 * day, now + 30 * day);
		lw_operator_roll(&op, ids, r);
		check(roll_apply(n, r), "the operator's roll is taken");
		check(!admit_verify(n, a, kid), "an admit made before the roll is no longer good");
		MKADMIT(&op, now + 1, now + 30 * day);
		check(admit_verify(n, a, kid), "one made after it is");
		check(!roll_apply(n, r), "the same roll again is not news");
		r[50] ^= 1;
		check(!roll_ok(n, r), "a roll that is not signed by the operator is refused");
		n->line.roll_t = 0;

		/* the founder's own admit is the root: no end holds it */
		{
			uint8_t fa[ADMIT_LEN];

			memcpy(fa, n->id.key_id, 32);
			fa[32] = 0;
			memcpy(fa + ADM_BY, n->id.key_id, 32);
			put64(fa + ADM_ISSUED, now - 800 * day);
			put64(fa + ADM_EXPIRES, now - 400 * day);
			check(admit_in_time(n, fa), "the founder's own admit is held to no end");
		}

		/* what a member showed is what it is held to */
		memcpy(pq->key_id, kid, 32);
		pq->used = pq->cur.valid = 1;
		n->now = 5000.0;
		mem_add(n, "444444444", kid, kid, 0, kid);
		MKADMIT(&op, now - day, now + 30 * day);
		handle_admit(n, pq, a, ADMIT_LEN);
		check(pq->admit_until == now + 30 * day && mem_find(n, kid), "a member's good admit is noted: it is held until then");
		pq->admit_until = now - 1;
		admit_sweep(n);
		check(!mem_find(n, kid), "past that, it is a member no longer");
		mem_add(n, "444444444", kid, kid, 0, kid);
		pq->used = pq->cur.valid = 1;
		memcpy(pq->key_id, kid, 32);
		MKADMIT(&op, now - 40 * day, now - 10 * day);
		handle_admit(n, pq, a, ADMIT_LEN);
		check(!mem_find(n, kid), "an ended admit shown by a member takes it off the line at once");
		mem_add(n, "444444444", kid, kid, 0, kid);
		pq->used = pq->cur.valid = 1;
		memcpy(pq->key_id, kid, 32);
		pq->admit_by = n->now - 1;
		admit_sweep(n);
		check(!mem_find(n, kid), "a member that shows no admit in time is off the line (it is not called again to try once more)");

		/* our own */
		MKADMIT(&op, now - 40 * day, now - 10 * day);
		memcpy(n->line.admit, a, ADMIT_LEN);
		memcpy(kid, n->id.key_id, 32);					/* the same admit, for this node: re-made with the key swapped in */
		MKADMIT(&op, now - 40 * day, now - 10 * day);
		memcpy(n->line.admit, a, ADMIT_LEN);
		memcpy(n->line.founder, kid, 32);			/* not the founder */
		check(admit_own_check(n) == 1 && n->line.dormant, "our own admit has ended: the node goes quiet");
		MKADMIT(&op, now - day, now + 10 * day);
		{
			char cmd[ADMIT_LEN * 2 + 20], out[300];

			memcpy(cmd, "admit ", 6);
			lw_hex(cmd + 6, a, ADMIT_LEN);
			check(line_cmd(n, cmd, out, sizeof out) > 0 && strstr(out, "renewed") && !n->line.dormant, "a renewed admit is taken and the node is back");
		}
		check(admit_own_check(n) == 0 && n->line.warned == 1, "ten days left: the warning for 14 days is given (and that for 3 is not yet)");
	}

	/* revocations are kept while they can matter and never dropped to make room */
	{
		lw_id op;
		uint8_t ev[EVICT_LEN], kid[32], first[32], msg[8 + 32 + 8 + 5];
		uint64_t now = (uint64_t)time(0), day = 86400;
		int k;

		memset(&op, 0, sizeof op);
		lw_rand(op.seed, 32);
		lw_id_derive(&op);
		memset(&n->line, 0, sizeof n->line);
		lw_rand(n->line.id, 8);
		memcpy(n->line.authority, op.bundle + ED_OFF, 32);
		n->line.managed = n->line.have = 1;
		for (k = 0; k < REV_MAX + 3; k++) {
			lw_rand(kid, 32);
			memcpy(ev, n->line.id, 8);
			memcpy(ev + 8, kid, 32);
			put64(ev + 40, now - 1);
			memcpy(ev + 48, op.bundle + ED_OFF, 32);
			memcpy(msg, ev, 48);
			memcpy(msg + 48, "evict", 5);
			lw_sign(&op, msg, sizeof msg, ev + 80);
			store_revoke(n, ev);
			record_eviction(n, kid, now - 1);
			if (k == 0)
				memcpy(first, kid, 32);		/* the first, to look for later */
		}
		check(n->line.nrev == REV_MAX && n->line.nev == REV_MAX && line_revs_full(n), "256 evictions that can still matter are held, and not one more");
		check(evicted_at(n, first) != 0, "the first of them is still there: none was dropped to make room");
		{
			uint8_t anyadmit[ADMIT_LEN];

			memset(anyadmit, 0, sizeof anyadmit);
			check(!line_admit_ok(n, anyadmit, kid), "a full list takes no new admit");
		}
		for (k = 0; k < n->line.nrev; k++)
			put64(n->line.rev[k] + 40, now - 450 * day);
		for (k = 0; k < n->line.nev; k++)
			n->line.ev[k].time = now - 450 * day;
		revs_prune(n);
		check(n->line.nrev == 0 && n->line.nev == 0 && !line_revs_full(n), "past the longest an admit can last, they go, and there is room again");
		{
			uint8_t r[ROLL_LEN];

			store_revoke(n, ev);
			put64(ev + 40, now - 2 * day);
			lw_operator_roll(&op, n->line.id, r);
			memcpy(n->line.rev[0], ev, EVICT_LEN);
			n->line.nrev = 1;
			n->line.ev[0].time = now - 2 * day;
			n->line.nev = 1;
			check(roll_apply(n, r) && n->line.nrev == 0 && n->line.nev == 0, "a roll covers every eviction older than it, and they go");
		}
	}


	/* a replayed eviction, a full list, evictions of strangers, a clock that jumps, an admit that is swapped (docs/adr/0066) */
	{
		lw_id op, m;
		uint8_t ev[EVICT_LEN], msg[8 + 32 + 8 + 5], kid[32], v[32], ids[8], a[ADMIT_LEN];
		uint64_t now = (uint64_t)time(0), day = 86400;
		lw_peer *pq = &n->peers[5];
		int k, nev;

		memset(&op, 0, sizeof op);
		lw_rand(op.seed, 32);
		lw_id_derive(&op);
		memset(&m, 0, sizeof m);
		lw_rand(m.seed, 32);
		lw_id_derive(&m);
		for (k = 0; k < n->maxpeers; k++)
			memset(&n->peers[k], 0, sizeof n->peers[k]);
		memset(&n->line, 0, sizeof n->line);
		lw_rand(ids, 8);
		memcpy(n->line.id, ids, 8);
		memcpy(n->line.authority, op.bundle + ED_OFF, 32);
		n->line.managed = n->line.have = 1;
		lw_rand(kid, 32);
		lw_rand(v, 32);
		memcpy(pq->key_id, kid, 32);
		pq->used = pq->cur.valid = 1;
		mem_add(n, "555555555", kid, kid, 0, kid);

		/* the operator evicted K a week ago and gave it a new admit two days ago: the old eviction, said again, does not put it off */
		pq->admit_issued = now - 2 * day;
		check(!evict_matters(n, kid, now - 7 * day), "an eviction older than the admit its key now holds does not bar it");
		check(evict_matters(n, kid, now - 1 * day), "one newer than that admit does");
		check(!evict_matters(n, v, now - 450 * day), "one too old to matter is not kept");
		memcpy(v, n->id.key_id, 32);
		MKADMIT_FOR(&op, v, now - 2 * day, now + 30 * day);
		memcpy(n->line.admit, a, ADMIT_LEN);
		check(!evict_matters(n, n->id.key_id, now - 7 * day) && evict_matters(n, n->id.key_id, now - 1 * day), "and the same for this node itself: an old eviction does not make it leave the line");
		check(line_evicted(n, kid) == 0, "on a line with an operator a key is in or out by the admit it shows, not by the eviction it once had");

		/* a full list still lets the members already on the line call each other; it is only a newcomer that is refused */
		{
			uint8_t k2[32];

			for (k = 0; k < REV_MAX; k++) {
				lw_rand(k2, 32);
				record_eviction(n, k2, now - 1);
			}
			check(line_revs_full(n), "256 evictions: the list is full");
			memcpy(pq->key_id, kid, 32);
			MKADMIT_FOR(&op, kid, now - day, now + 30 * day);
			check(line_admit_ok(n, a, kid), "a member with an admit that holds is still admitted by a node whose list is full");
		}

		/* members evict members: on a line without an operator an eviction of a key that is not on it is not recorded */
		memset(&n->line, 0, sizeof n->line);
		memcpy(n->line.id, ids, 8);
		n->line.have = 1;
		memcpy(pq->key_id, m.key_id, 32);
		memcpy(pq->bundle, m.bundle, LW_BUNDLE);
		pq->have_bundle = 1;
		mem_add(n, "666666666", m.key_id, m.bundle + ED_OFF, 0, v);
		lw_rand(kid, 32);
		memcpy(ev, ids, 8);
		memcpy(ev + 8, kid, 32);
		put64(ev + 40, now);
		memcpy(ev + 48, m.key_id, 32);
		memcpy(msg, ev, 48);
		memcpy(msg + 48, "evict", 5);
		lw_sign(&m, msg, sizeof msg, ev + 80);
		nev = n->line.nev;
		handle_evict(n, pq, ev, EVICT_LEN);
		check(n->line.nev == nev, "a member's eviction of a key that is not on the line records nothing");

		/* a clock that jumps a year forward does not make the node forget its evictions */
		memset(&n->line, 0, sizeof n->line);
		memcpy(n->line.id, ids, 8);
		memcpy(n->line.authority, op.bundle + ED_OFF, 32);
		n->line.managed = n->line.have = 1;
		n->line.wall_off = (double)time(0) - n->now - 400.0 * 86400.0;	/* what the clock said when the node began: a year ago */
		n->line.ev[0].time = now - 1;
		lw_rand(n->line.ev[0].key_id, 32);
		n->line.nev = 1;
		n->line.roll_t = now - 2;
		revs_prune(n);
		check(n->line.nev == 1, "a wall clock that has jumped by more than a day since the node began: nothing is pruned");
		n->line.wall_off = (double)time(0) - n->now;
		n->line.ev[0].time = now - 5;
		revs_prune(n);
		check(n->line.nev == 0, "a clock that has not jumped: pruned as before");
	}

	printf(bad ? "FAILED\n" : "all switch checks passed\n");
	return bad;
}
