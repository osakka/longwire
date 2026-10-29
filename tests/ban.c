/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Strikes and bans (docs/adr/0043), at the level of the tables: what earns a ban, what lets it go, what a flood of identities cannot do to the table, and what
 * is never struck. The engine is included whole and a switchboard is made in this process; the sessions are made by hand.
 */
#include "../engine/all.c"

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

static lw_node *mknode(const char *dir)
{
	lw_cfg c;
	char err[200];
	lw_node *n;

	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", dir);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", dir);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", dir);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	snprintf(c.registry, sizeof c.registry, "none");
	c.role = 1;
	c.maxpeers = 64;
	c.no_nat_pmp = 1;
	c.strikes = 3;			/* three strikes in 100 seconds ban for 10, doubling to at most 80 */
	c.strike_window = 100;
	c.ban_for = 10;
	c.ban_max = 80;
	c.net_keys = 2;
	c.net_for = 50;
	c.ban_ceiling = 100;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		exit(2);
	}
	n->now = 1000.0;
	return n;
}

/* a session from the address a.b.c.d with a key of its own */
static lw_peer *mkpeer(lw_node *n, int slot, int a, int b, int c, int d)
{
	lw_peer *p = &n->peers[slot];
	struct sockaddr_in *e = (struct sockaddr_in *)&p->ep;

	memset(p, 0, sizeof *p);
	p->used = 1;
	lw_rand(p->key_id, 32);
	p->cur.valid = 1;
	e->sin_family = AF_INET;
	e->sin_addr.s_addr = htonl((uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)c << 8 | (uint32_t)d);
	e->sin_port = htons(4000);
	p->eplen = sizeof *e;
	return p;
}

static struct sockaddr_storage ss_of(int a, int b, int c, int d)
{
	struct sockaddr_storage ss;
	struct sockaddr_in *e = (struct sockaddr_in *)&ss;

	memset(&ss, 0, sizeof ss);
	e->sin_family = AF_INET;
	e->sin_addr.s_addr = htonl((uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)c << 8 | (uint32_t)d);
	return ss;
}

int main(int argc, char **argv)
{
	lw_node *n;
	lw_peer *p, *q;
	uint8_t kx[32];
	char js[8192];
	struct sockaddr_storage s1, s2;
	int i, held;

	if (argc < 2)
		return 2;
	n = mknode(argv[1]);

	/* three strikes ban; two do not */
	p = mkpeer(n, 0, 198, 51, 100, 7);
	strike(n, p, 1, "t"); strike(n, p, 1, "t");
	check(!ban_active(n, p->key_id), "two strikes of three do not ban");
	strike(n, p, 1, "t");
	check(ban_active(n, p->key_id) && n->bans_issued == 1, "the third bans");
	n->now += 5.0;
	check(ban_active(n, p->key_id), "and the ban holds");
	n->now += 6.0;
	check(!ban_active(n, p->key_id), "and ends after ten seconds");

	/* a repeat doubles, up to the cap */
	strike(n, p, 3, "t");
	ban_json(n, js, sizeof js);
	check(strstr(js, "\"seconds\":20") != 0, "the second ban is twenty seconds");
	n->now += 21.0;
	strike(n, p, 3, "t");
	n->now += 1.0;
	ban_json(n, js, sizeof js);
	check(strstr(js, "\"seconds\":39") != 0 || strstr(js, "\"seconds\":40") != 0, "the third is forty");
	n->now += 41.0;
	strike(n, p, 3, "t"); n->now += 1.0; ban_json(n, js, sizeof js);
	check(strstr(js, "\"seconds\":79") != 0 || strstr(js, "\"seconds\":80") != 0, "the fourth is eighty");
	n->now += 81.0;
	strike(n, p, 3, "t"); n->now += 1.0; ban_json(n, js, sizeof js);
	check(strstr(js, "\"seconds\":79") != 0 || strstr(js, "\"seconds\":80") != 0, "and no more than the cap (--ban-max)");
	n->now += 82.0;

	/* strikes leak away: two now, two a window later, no ban */
	p = mkpeer(n, 1, 198, 51, 101, 7);
	strike(n, p, 1, "t"); strike(n, p, 1, "t");
	n->now += 150.0;
	strike(n, p, 1, "t"); strike(n, p, 1, "t");
	check(!ban_active(n, p->key_id), "strikes leak away with time: slow trickles never add up");

	/* a weight counts: a refusal is two */
	p = mkpeer(n, 2, 198, 51, 102, 7);
	strike(n, p, 2, "t");
	check(!ban_active(n, p->key_id), "one heavy strike is not a ban");
	strike(n, p, 2, "t");
	check(ban_active(n, p->key_id), "two heavy ones are");

	/* a banned key's session is dropped by the timer, and a lifted ban lets it back */
	ban_timers(n);
	check(!p->used, "the banned key's live session is dropped");
	memcpy(kx, p->key_id, 32);
	check(ban_clear(n, "all") >= 1 && !ban_active(n, kx), "lifting the bans lets it back");

	/* never struck: a key, and an address with a prefix */
	n->cfg.n_never = 2;
	n->cfg.never[0].kind = 1;
	p = mkpeer(n, 3, 203, 0, 113, 9);
	memcpy(n->cfg.never[0].kid, p->key_id, 32);
	n->cfg.never[1].kind = 2; n->cfg.never[1].fam = 4; n->cfg.never[1].bits = 24;
	n->cfg.never[1].net[0] = 192; n->cfg.never[1].net[1] = 0; n->cfg.never[1].net[2] = 2;
	strike(n, p, 50, "t");
	check(!ban_active(n, p->key_id), "a key on the never-ban list is never struck");
	q = mkpeer(n, 4, 192, 0, 2, 77);
	strike(n, q, 50, "t");
	check(!ban_active(n, q->key_id), "and neither is an address inside a listed network");
	q = mkpeer(n, 5, 192, 0, 3, 77);
	strike(n, q, 50, "t");
	check(ban_active(n, q->key_id), "but the next network over is");
	n->cfg.n_never = 0;

	/* off */
	n->cfg.strikes = 0;
	q = mkpeer(n, 6, 192, 0, 4, 77);
	strike(n, q, 50, "t");
	check(!ban_active(n, q->key_id), "with strikes 0 nothing is ever banned");
	n->cfg.strikes = 3;

	/* several keys banned from one network: new identities from it are refused, other networks are not */
	ban_clear(n, "all");
	for (i = 0; i < 2; i++) {
		q = mkpeer(n, 7 + i, 100, 64, 9, 10 + i);
		strike(n, q, 5, "t");
	}
	s1 = ss_of(100, 64, 9, 200);
	s2 = ss_of(100, 64, 10, 200);
	check(ban_net_blocked(n, &s1), "two banned keys from one /24 block that network for new identities");
	check(!ban_net_blocked(n, &s2), "and not the next one");
	n->now += 51.0;
	check(!ban_net_blocked(n, &s1), "the network ban ends too");

	/* the ceiling: a flood of bans does not become a refusal of everyone */
	ban_clear(n, "all");
	n->cfg.ban_ceiling = 5;
	n->bans_issued = 0;
	n->now += 4000.0;
	for (i = 0; i < 12; i++) {
		q = mkpeer(n, 20 + i, 10, 1, (i % 200), 5);
		strike(n, q, 9, "t");
	}
	check(n->bans_issued == 5 && n->bans_suppressed == 7, "no more than the ceiling of bans in an hour; the rest are counted, not issued");
	n->cfg.ban_ceiling = 1000;

	/* a flood of identities cannot grow the table, and cannot push out a live ban */
	ban_clear(n, "all");
	q = mkpeer(n, 40, 172, 16, 0, 1);
	strike(n, q, 9, "t");
	memcpy(kx, q->key_id, 32);
	for (i = 0; i < 200000; i++) {
		lw_peer fake;

		memset(&fake, 0, sizeof fake);
		fake.used = 1;
		lw_rand(fake.key_id, 32);
		fake.ep = q->ep;
		fake.eplen = q->eplen;
		strike(n, &fake, 1, "t");
	}
	check(ban_active(n, kx), "200000 different keys with one strike each do not push a live ban out of the table");
	for (i = 0, held = 0; i < BAN_SLOTS; i++)
		held += n->bn->e[i].used;
	check(held <= BAN_SLOTS, "and the table stays its fixed size");

	if (bad)
		printf("FAILED\n");
	else
		printf("all ban checks passed\n");
	return bad;
}
