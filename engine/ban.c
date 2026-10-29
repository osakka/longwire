/* SPDX-License-Identifier: GPL-3.0-or-later
 * Strikes and bans on a switchboard (docs/adr/0043).
 *
 * The rule that keeps this from being a weapon against the switchboard's own users: only what an attacker cannot fake is ever punished. Nothing here is
 * counted for a packet before the handshake: its source address is spoofable and the handshake is where the switchboard proves, by a keyed check and
 * a cookie it sent to that very address, that the sender both knows the switchboard and can receive at the address it claims. A failure counts only on a
 * session that got that far, and what is punished is that session's key. A network is refused only for new identities, and only after several
 * different keys from it earned bans, so a shared address does not lock out the people already registered behind it. Everything is in fixed tables,
 * everything expires, there is a ceiling on bans per hour, and the operator can exempt addresses and keys.
 */

#define BAN_SLOTS 2048
#define NET_SLOTS 256

typedef struct bn_ent {
	uint8_t  used, kid[32];
	double   score, t;		/* strikes, leaking away at strikes/window a second */
	double   until, level_t;	/* banned until; when the escalation of repeat bans is forgotten */
	int      level;
} bn_ent;

typedef struct bn_net {
	uint8_t  used, fam, net[8];
	double   until, t, level_t;
	int      keys, level;
} bn_net;

struct banset {
	bn_ent   e[BAN_SLOTS];
	bn_net   nt[NET_SLOTS];
	double   hour_t;
	int      hour_n;
};

static struct banset *bs(lw_node *n)
{
	if (!n->bn)
		n->bn = calloc(1, sizeof *n->bn);
	return n->bn;
}

void ban_free(lw_node *n)
{
	free(n->bn);
	n->bn = 0;
}

static unsigned kid_slot(const lw_node *n, const uint8_t kid[32])
{
	uint8_t h[8];
	uint64_t v;

	crypto_blake2b_keyed(h, 8, (const uint8_t *)&n->hsalt, 8, kid, 32);
	memcpy(&v, h, 8);
	return (unsigned)(v % BAN_SLOTS);
}

/* The entry for a key; a new one takes a free slot among four, or the least valuable: an idle one before one with strikes, one with strikes before a live ban. */
static bn_ent *ent_find(lw_node *n, const uint8_t kid[32], int create)
{
	struct banset *b = bs(n);
	unsigned i0, k;
	bn_ent *worst = 0;
	double wv = 1e18;

	if (!b)
		return 0;
	i0 = kid_slot(n, kid);
	for (k = 0; k < 4; k++) {
		bn_ent *e = &b->e[(i0 + k) % BAN_SLOTS];
		double v;

		if (e->used && !memcmp(e->kid, kid, 32))
			return e;
		if (!e->used) {
			if (!create)
				continue;
			worst = e;
			wv = -1;
			continue;
		}
		v = e->until > n->now ? 1e9 + (e->until - n->now) : e->score;
		if (v < wv) {
			wv = v;
			worst = e;
		}
	}
	if (!create || !worst)
		return 0;
	memset(worst, 0, sizeof *worst);
	worst->used = 1;
	memcpy(worst->kid, kid, 32);
	return worst;
}

static int net_id(const struct sockaddr_storage *ss, uint8_t *fam, uint8_t net[8])
{
	memset(net, 0, 8);
	if (ss->ss_family == AF_INET) {
		memcpy(net, &((const struct sockaddr_in *)(const void *)ss)->sin_addr, 3);	/* a /24 */
		*fam = 4;
		return 1;
	}
	if (ss->ss_family == AF_INET6) {
		memcpy(net, &((const struct sockaddr_in6 *)(const void *)ss)->sin6_addr, 8);	/* a /64 */
		*fam = 6;
		return 1;
	}
	return 0;
}

static bn_net *net_find(lw_node *n, const struct sockaddr_storage *ss, int create)
{
	struct banset *b = bs(n);
	uint8_t fam, net[8], h[8];
	uint64_t v;
	unsigned i0, k;
	bn_net *worst = 0;
	double wv = 1e18;

	if (!b || !net_id(ss, &fam, net))
		return 0;
	crypto_blake2b_keyed(h, 8, (const uint8_t *)&n->hsalt, 8, net, 8);
	memcpy(&v, h, 8);
	i0 = (unsigned)(v % NET_SLOTS);
	for (k = 0; k < 4; k++) {
		bn_net *e = &b->nt[(i0 + k) % NET_SLOTS];
		double val;

		if (e->used && e->fam == fam && !memcmp(e->net, net, 8))
			return e;
		if (!e->used) {
			if (!create)
				continue;
			worst = e;
			wv = -1;
			continue;
		}
		val = e->until > n->now ? 1e9 + (e->until - n->now) : (double)e->keys;
		if (val < wv) {
			wv = val;
			worst = e;
		}
	}
	if (!create || !worst)
		return 0;
	memset(worst, 0, sizeof *worst);
	worst->used = 1;
	worst->fam = fam;
	memcpy(worst->net, net, 8);
	return worst;
}

/* The operator's list of those who are never struck: --never-ban, a key id or an address with a prefix. */
static int ban_exempt(const lw_node *n, const uint8_t kid[32], const struct sockaddr_storage *ep)
{
	int i;

	for (i = 0; i < n->cfg.n_never; i++) {
		const lw_never *w = &n->cfg.never[i];

		if (w->kind == 1) {
			if (!memcmp(w->kid, kid, 32))
				return 1;
		} else if (w->fam == 4 && ep->ss_family == AF_INET) {
			const uint8_t *a = (const uint8_t *)&((const struct sockaddr_in *)(const void *)ep)->sin_addr;
			int bits = w->bits, k;

			for (k = 0; bits > 0 && k < 4; k++, bits -= 8) {
				uint8_t m = bits >= 8 ? 0xff : (uint8_t)(0xff << (8 - bits));

				if ((a[k] & m) != (w->net[k] & m))
					break;
			}
			if (bits <= 0)
				return 1;
		} else if (w->fam == 6 && ep->ss_family == AF_INET6) {
			const uint8_t *a = (const uint8_t *)&((const struct sockaddr_in6 *)(const void *)ep)->sin6_addr;
			int bits = w->bits, k;

			for (k = 0; bits > 0 && k < 16; k++, bits -= 8) {
				uint8_t m = bits >= 8 ? 0xff : (uint8_t)(0xff << (8 - bits));

				if ((a[k] & m) != (w->net[k] & m))
					break;
			}
			if (bits <= 0)
				return 1;
		}
	}
	return 0;
}

static double ban_len(const lw_node *n, int level, int base)
{
	double d = (double)base;
	int i;

	for (i = 0; i < level && i < 20; i++)
		d *= 2.0;
	return d > (double)n->cfg.ban_max ? (double)n->cfg.ban_max : d;
}

/* A failure on an authenticated session (weight 1 for an ordinary one, 2 for a refusal, a fraction for a message over the session's budget). */
void strike(lw_node *n, lw_peer *p, double w, const char *why)
{
	bn_ent *e;
	bn_net *nt;
	double dur;
	char kh[65], ip[64];

	if (!n->role || n->cfg.strikes <= 0 || !p || p->is_sb || !p->used)
		return;
	if (ban_exempt(n, p->key_id, &p->ep))
		return;
	if (!(e = ent_find(n, p->key_id, 1)))
		return;
	if (e->until > n->now)
		return;					/* already banned: no more to give */
	e->score -= (n->now - e->t) * ((double)n->cfg.strikes / (double)(n->cfg.strike_window > 0 ? n->cfg.strike_window : 600));
	if (e->score < 0 || !e->t)
		e->score = 0;
	e->t = n->now;
	e->score += w;
	n->strikes_given++;
	if (e->score < (double)n->cfg.strikes)
		return;
	e->score = 0;
	{						/* a ceiling on bans an hour: a flood must not make the switchboard refuse everybody */
		struct banset *b = bs(n);

		if (n->now - b->hour_t > 3600.0) {
			b->hour_t = n->now;
			b->hour_n = 0;
		}
		if (n->cfg.ban_ceiling > 0 && b->hour_n >= n->cfg.ban_ceiling) {
			n->bans_suppressed++;
			return;
		}
		b->hour_n++;
	}
	if (e->level_t && n->now > e->level_t + (double)n->cfg.ban_max)
		e->level = 0;
	dur = ban_len(n, e->level, n->cfg.ban_for);
	e->until = n->now + dur;
	e->level_t = e->until;
	if (e->level < 20)
		e->level++;
	n->bans_issued++;
	lw_hex(kh, p->key_id, 32);
	addr_str(&p->ep, ip, sizeof ip);
	ev_emit_owner(n, "{\"event\":\"ban\",\"key_id\":\"%s\",\"ip\":\"%s\",\"seconds\":%d,\"level\":%d,\"reason\":\"%s\"}", kh, ip, (int)dur, e->level, why);
	if (n->cfg.net_keys > 0 && (nt = net_find(n, &p->ep, 1))) {	/* several different keys from one network earning bans: refuse new identities from it */
		if (n->now - nt->t > (double)(n->cfg.strike_window > 0 ? n->cfg.strike_window : 600))
			nt->keys = 0;
		nt->t = n->now;
		if (nt->level_t && n->now > nt->level_t + (double)n->cfg.ban_max)
			nt->level = 0;
		if (++nt->keys >= n->cfg.net_keys && nt->until <= n->now) {
			double nd = ban_len(n, nt->level, n->cfg.net_for);

			nt->until = n->now + nd;
			nt->level_t = nt->until;
			if (nt->level < 20)
				nt->level++;
			nt->keys = 0;
			n->net_bans_issued++;
			ev_emit_owner(n, "{\"event\":\"net-ban\",\"ip\":\"%s\",\"seconds\":%d,\"level\":%d,\"reason\":\"several keys from this network were banned\"}", ip, (int)nd, nt->level);
		}
	}
}

int ban_active(lw_node *n, const uint8_t kid[32])
{
	bn_ent *e;

	if (!n->bn || n->cfg.strikes <= 0)
		return 0;
	e = ent_find(n, kid, 0);
	return e && e->until > n->now;
}

/* A network with a live ban refuses new identities (a key the switchboard has never registered); those already registered behind it are not affected. */
int ban_net_blocked(lw_node *n, const struct sockaddr_storage *ep)
{
	bn_net *nt;

	if (!n->bn || n->cfg.strikes <= 0)
		return 0;
	nt = net_find(n, ep, 0);
	return nt && nt->until > n->now;
}

/* A banned key's live session goes: it was told why, once. (Dropped here, not where the strike fell, because the code that struck is still using the peer.) */
void ban_timers(lw_node *n)
{
	int i;
	static const char why[] = "temporarily refused: too many failures";

	if (!n->bn || n->cfg.strikes <= 0)
		return;
	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *p = &n->peers[i];

		if (p->used && !p->is_sb && ban_active(n, p->key_id)) {
			ctl_msg_send(n, p, M_REFUSED, (const uint8_t *)why, sizeof why - 1);
			peer_drop(n, p);
		}
	}
}

/* For the operator: the live bans and the counters. */
int ban_json(lw_node *n, char *o, size_t cap)
{
	struct banset *b = n->bn;
	int i, len, any = 0;

	len = snprintf(o, cap, "{\"strikes\":%d,\"window\":%d,\"ban_for\":%d,\"ban_max\":%d,\"net_keys\":%d,\"net_for\":%d,\"ceiling\":%d,\"struck\":%llu,\"issued\":%llu,\"net_issued\":%llu,\"refused\":%llu,\"suppressed\":%llu,\"bans\":[",
		n->cfg.strikes, n->cfg.strike_window, n->cfg.ban_for, n->cfg.ban_max, n->cfg.net_keys, n->cfg.net_for, n->cfg.ban_ceiling,
		(unsigned long long)n->strikes_given, (unsigned long long)n->bans_issued, (unsigned long long)n->net_bans_issued,
		(unsigned long long)n->bans_refused, (unsigned long long)n->bans_suppressed);
	for (i = 0; b && i < BAN_SLOTS && len < (int)cap - 200; i++) {
		char kh[65];

		if (!b->e[i].used || b->e[i].until <= n->now)
			continue;
		lw_hex(kh, b->e[i].kid, 32);
		len += snprintf(o + len, cap - (size_t)len, "%s{\"key_id\":\"%s\",\"seconds\":%d,\"level\":%d}", any ? "," : "", kh, (int)(b->e[i].until - n->now), b->e[i].level);
		any = 1;
	}
	len += snprintf(o + len, cap - (size_t)len, "],\"nets\":[");
	any = 0;
	for (i = 0; b && i < NET_SLOTS && len < (int)cap - 120; i++) {
		if (!b->nt[i].used || b->nt[i].until <= n->now)
			continue;
		if (b->nt[i].fam == 4)
			len += snprintf(o + len, cap - (size_t)len, "%s{\"net\":\"%u.%u.%u.0/24\",\"seconds\":%d}", any ? "," : "", b->nt[i].net[0], b->nt[i].net[1], b->nt[i].net[2], (int)(b->nt[i].until - n->now));
		else
			len += snprintf(o + len, cap - (size_t)len, "%s{\"net\":\"%02x%02x:%02x%02x:%02x%02x:%02x%02x::/64\",\"seconds\":%d}", any ? "," : "",
				b->nt[i].net[0], b->nt[i].net[1], b->nt[i].net[2], b->nt[i].net[3], b->nt[i].net[4], b->nt[i].net[5], b->nt[i].net[6], b->nt[i].net[7], (int)(b->nt[i].until - n->now));
		any = 1;
	}
	len += snprintf(o + len, cap - (size_t)len, "]}\n");
	return len;
}

/* Lift bans: "all", a key id (64 hex digits or the first eight or more), or nothing else. Returns how many. */
int ban_clear(lw_node *n, const char *arg)
{
	struct banset *b = n->bn;
	size_t al = strlen(arg);
	int i, c = 0;

	if (!b)
		return 0;
	for (i = 0; i < BAN_SLOTS; i++) {
		char kh[65];

		if (!b->e[i].used)
			continue;
		lw_hex(kh, b->e[i].kid, 32);
		if (!strcmp(arg, "all") || (al >= 8 && !strncmp(kh, arg, al))) {
			if (b->e[i].until > n->now)
				c++;
			memset(&b->e[i], 0, sizeof b->e[i]);
		}
	}
	if (!strcmp(arg, "all"))
		for (i = 0; i < NET_SLOTS; i++) {
			if (b->nt[i].used && b->nt[i].until > n->now)
				c++;
			memset(&b->nt[i], 0, sizeof b->nt[i]);
		}
	return c;
}
