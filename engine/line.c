/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lines of more than two nodes (docs/PROTOCOL.md §6, docs/adr/0022). A line is a founder, a line key and a list of
 * members. A node joins by calling a member, which vouches for it with a signed admit; the newcomer then calls every
 * other member and, holding an admit, is let through without a prompt. Evictions are signed, recorded and persisted.
 * The switchboard knows none of this: it carries an admit as an opaque blob. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "int.h"
#include "../vendor/monocypher/monocypher.h"

typedef char cfg_admit_is_an_admit[sizeof(((lw_cfg *)0)->admit) == ADMIT_LEN ? 1 : -1];	/* lw.h spells the size out: the two must agree */
#define EVICT_SKEW 86400		/* how far ahead of this clock an eviction may be dated: clocks differ, a day is far more than they do */

static void put64(uint8_t *p, uint64_t v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (56 - 8 * i));
}

static uint64_t get64(const uint8_t *p)
{
	uint64_t v = 0;
	int i;

	for (i = 0; i < 8; i++)
		v = v << 8 | p[i];
	return v;
}

const uint8_t *peer_ed_pub(const lw_peer *p) { return p->bundle + ED_OFF; }
static const uint8_t *my_ed(lw_node *n) { return n->id.bundle + ED_OFF; }
int line_have(const lw_node *n) { return n->line.have; }

/* ---- members and evictions ---------------------------------------------------- */

static lw_member *mem_find(lw_node *n, const uint8_t kid[32])
{
	int i;

	for (i = 0; i < n->line.nm; i++)
		if (!memcmp(n->line.m[i].key_id, kid, 32))
			return &n->line.m[i];
	return 0;
}

static lw_member *mem_add(lw_node *n, const char *number, const uint8_t kid[32], const uint8_t ed[32], int role, const uint8_t sb[32])
{
	lw_member *m = mem_find(n, kid);
	const lw_peer *q = peer_by_key(n, kid);

	if (q && q->have_bundle)
		ed = peer_ed_pub(q);			/* a key we hold the bundle of is the bundle's, not what a member's record says it is (docs/adr/0064) */

	if (!m) {
		if (n->line.nm >= LINE_MAX)
			return 0;
		m = &n->line.m[n->line.nm++];
		memset(m, 0, sizeof *m);
		memcpy(m->key_id, kid, 32);
	}
	memcpy(m->ed_pub, ed, 32);
	memcpy(m->sb, sb, 32);
	snprintf(m->number, sizeof m->number, "%s", number);
	m->role = (uint8_t)role;
	return m;
}

static void mem_remove(lw_node *n, const uint8_t kid[32])
{
	int i;

	for (i = 0; i < n->line.nm; i++)
		if (!memcmp(n->line.m[i].key_id, kid, 32)) {
			n->line.m[i] = n->line.m[--n->line.nm];
			return;
		}
}

static uint64_t evicted_at(lw_node *n, const uint8_t kid[32])
{
	uint64_t t = 0;
	int i;

	for (i = 0; i < n->line.nev; i++)
		if (!memcmp(n->line.ev[i].key_id, kid, 32) && n->line.ev[i].time > t)
			t = n->line.ev[i].time;
	return t;
}

/* Put off the line for good. On a line with an operator an eviction bars the admits made before it and nothing else: whether a key is in is decided by the admit it shows. */
int line_evicted(lw_node *n, const uint8_t kid[32])
{
	return n->line.have && !n->line.managed && evicted_at(n, kid) != 0;
}

/* An eviction matters until the latest admit that could have been made for the key has ended (ADMIT_MAX_LIFE after the eviction, and the skew the clocks may have),
 * or until a roll newer than it has made every older admit void. Before that it is never dropped, and never to make room (docs/adr/0065). A line whose members
 * run it among themselves has no ends to count on: nothing there is dropped. */
static uint64_t ev_horizon(lw_node *n)
{
	uint64_t now = (uint64_t)time(0), h = n->line.roll_t;

	if (n->line.managed && now > ADMIT_MAX_LIFE + EVICT_SKEW && now - ADMIT_MAX_LIFE - EVICT_SKEW > h)
		h = now - ADMIT_MAX_LIFE - EVICT_SKEW;
	return h;
}

/* Too old to matter: not for the founder. Its own admit is held to no end and no roll, so a revocation of it is the only thing that keeps a revoked founder off the line (a member that
 * missed it would otherwise take the founder back once a roll had made it too old to be kept): it is kept for good (docs/adr/0069). */
static int ev_old(lw_node *n, const uint8_t kid[32], uint64_t t)
{
	return t < ev_horizon(n) && memcmp(kid, n->line.founder, 32);
}

/* The wall clock is not the node's own: a jump of more than the skew since this node started (a time server that lies, a clock set by hand) makes the pruning wait, because
 * a clock moved forward a year would otherwise forget every eviction at once, and the loss is saved (docs/adr/0066). Restarted, a node takes the clock it finds. */
static int clock_jumped(lw_node *n)
{
	double off = (double)time(0) - n->now;

	if (n->line.wall_off == 0.0)
		n->line.wall_off = off;
	return off - n->line.wall_off > (double)EVICT_SKEW || n->line.wall_off - off > (double)EVICT_SKEW;
}

static void revs_prune(lw_node *n)
{
	uint64_t h = ev_horizon(n);
	int i, j;

	if (!h || clock_jumped(n))
		return;
	for (i = j = 0; i < n->line.nrev; i++)
		if (get64(n->line.rev[i] + 40) >= h || !ev_old(n, n->line.rev[i] + 8, get64(n->line.rev[i] + 40))) {
			if (i != j)
				memcpy(n->line.rev[j], n->line.rev[i], EVICT_LEN);
			j++;
		}
	n->line.nrev = j;
	for (i = j = 0; i < n->line.nev; i++)
		if (n->line.ev[i].time >= h || !ev_old(n, n->line.ev[i].key_id, n->line.ev[i].time))
			n->line.ev[j++] = n->line.ev[i];
	n->line.nev = j;
}

/* Room for no more evictions: no new admit is taken from anyone until time or a roll makes room, and the status says so. */
int line_revs_full(lw_node *n)
{
	return n->line.nrev >= REV_MAX || n->line.nev >= REV_MAX;
}

static void record_eviction(lw_node *n, const uint8_t kid[32], uint64_t t)
{
	int i;

	for (i = 0; i < n->line.nev; i++)
		if (!memcmp(n->line.ev[i].key_id, kid, 32)) {
			if (t > n->line.ev[i].time)
				n->line.ev[i].time = t;
			return;
		}
	if (ev_old(n, kid, t))
		return;				/* it cannot matter any more */
	if (n->line.nev >= REV_MAX) {
		ev_emit(n, "{\"event\":\"revocations-full\"}");
		return;
	}
	i = n->line.nev++;
	memcpy(n->line.ev[i].key_id, kid, 32);
	n->line.ev[i].time = t;
}

/* ---- admits ------------------------------------------------------------------- */

/* key_id 32 | role 1 | admitted_by 32 | issued 8 | expires 8 | sig 64: signed by admitted_by over the line id and the 81 bytes before the signature. The line id is under the signature
 * and not on the wire: an admit is checked by a node that holds the line, with its line id, so one made for another line does not verify, and it is 8 bytes the RING need not carry
 * (it is a datagram of about 1500 bytes already: the caller's whole bundle rides in it). A member's admit (a line without an operator) and the founder's own carry the longest
 * end there is and it is not held to: only an operator's admits end (docs/adr/0065). */
static void admit_msg(const uint8_t line_id[8], const uint8_t *a, uint8_t m[8 + ADM_SIGNED])
{
	memcpy(m, line_id, 8);
	memcpy(m + 8, a, ADM_SIGNED);
}

static void admit_make(lw_node *n, const uint8_t kid[32], int role, uint8_t out[ADMIT_LEN])
{
	uint64_t t = (uint64_t)time(0);
	uint8_t m[8 + ADM_SIGNED];

	memcpy(out, kid, 32);
	out[32] = (uint8_t)role;
	memcpy(out + ADM_BY, n->id.key_id, 32);
	put64(out + ADM_ISSUED, t);
	put64(out + ADM_EXPIRES, t + ADMIT_MAX_LIFE);
	admit_msg(n->line.id, out, m);
	lw_sign(&n->id, m, sizeof m, out + ADM_SIGNED);
}

/* The operator signs on a machine that runs no node: the same bytes as a member's admit or eviction, with the
 * operator's public key in the signer field. */
void lw_operator_pub(const lw_id *op, uint8_t out[32])
{
	memcpy(out, op->bundle + ED_OFF, 32);
}

void lw_operator_admit(const lw_id *op, const uint8_t line_id[8], const uint8_t kid[32], unsigned days, uint8_t out[ADMIT_LEN])
{
	uint64_t t = (uint64_t)time(0);
	uint8_t m[8 + ADM_SIGNED];

	if (days < 1 || days > ADMIT_MAX_LIFE / 86400)
		days = ADMIT_DEFAULT_DAYS;
	memcpy(out, kid, 32);
	out[32] = 0;
	lw_operator_pub(op, out + ADM_BY);
	put64(out + ADM_ISSUED, t);
	put64(out + ADM_EXPIRES, t + (uint64_t)days * 86400);
	admit_msg(line_id, out, m);
	lw_sign(op, m, sizeof m, out + ADM_SIGNED);
}

/* "Nothing made before now is good": line_id 8 | time 8 | signer 32 | sig 64 over line_id, time and "roll". */
void lw_operator_roll(const lw_id *op, const uint8_t line_id[8], uint8_t out[ROLL_LEN])
{
	uint8_t msg[8 + 8 + 4];

	memcpy(out, line_id, 8);
	put64(out + 8, (uint64_t)time(0));
	lw_operator_pub(op, out + 16);
	memcpy(msg, out, 16);
	memcpy(msg + 16, "roll", 4);
	lw_sign(op, msg, sizeof msg, out + 48);
}

void lw_operator_evict(const lw_id *op, const uint8_t line_id[8], const uint8_t kid[32], uint8_t out[EVICT_LEN])
{
	uint8_t msg[8 + 32 + 8 + 5];

	memcpy(out, line_id, 8);
	memcpy(out + 8, kid, 32);
	put64(out + 40, (uint64_t)time(0));
	lw_operator_pub(op, out + 48);
	memcpy(msg, out, 48);
	memcpy(msg + 48, "evict", 5);
	lw_sign(op, msg, sizeof msg, out + 80);
}

/* The founder's own admit is the root of the line: it is held to no end and to no roll. */
static int admit_founders(lw_node *n, const uint8_t *a)
{
	return !memcmp(a + ADM_BY, a, 32) && !memcmp(a + ADM_BY, n->line.founder, 32);
}

/* An admit says when it was made and when it ends, and the end is at most ADMIT_MAX_LIFE after the start. On a line with an operator it is held to its end, to the
 * operator's last roll, and to not being made in the future (an operator's clock far ahead would make it last longer): docs/adr/0065. */
static int admit_in_time(lw_node *n, const uint8_t *a)
{
	uint64_t i = get64(a + ADM_ISSUED), e = get64(a + ADM_EXPIRES), now = (uint64_t)time(0);

	if (e <= i || e - i > ADMIT_MAX_LIFE)
		return 0;
	if (!n->line.managed || admit_founders(n, a))
		return 1;
	return now < e && i <= now + EVICT_SKEW && i >= n->line.roll_t;
}

/* Is `a` a valid admit, for the node with key id `kid`, to the line this node holds? */
static int admit_verify(lw_node *n, const uint8_t *a, const uint8_t kid[32])
{
	const lw_member *by;
	uint8_t m[8 + ADM_SIGNED];

	if (!n->line.have || memcmp(a, kid, 32) || !admit_in_time(n, a))
		return 0;
	admit_msg(n->line.id, a, m);			/* the line is ours: an admit made for another does not verify */
	if (n->line.managed && !memcmp(a + ADM_BY, n->line.authority, 32)) {	/* the operator's word: no member needed */
		if (!lw_verify(n->line.authority, m, sizeof m, a + ADM_SIGNED))
			return 0;
		return get64(a + ADM_ISSUED) > evicted_at(n, kid);
	}
	if (n->line.managed && (memcmp(a + ADM_BY, n->line.founder, 32) || memcmp(a + ADM_BY, a, 32)))	/* a managed line takes the operator's, and its founder's own: no member's word for another */
		return 0;
	if (!(by = mem_find(n, a + ADM_BY)))		/* the signer must be a member we have on record, connected or not */
		return 0;
	if (!memcmp(a + ADM_BY, a, 32) && memcmp(a + ADM_BY, n->line.founder, 32))
		return 0;				/* a self-admit is good only for the founder */
	if (!lw_verify(by->ed_pub, m, sizeof m, a + ADM_SIGNED))
		return 0;
	if (get64(a + ADM_ISSUED) <= evicted_at(n, kid))	/* an admit older than the eviction cannot bring it back */
		return 0;
	return 1;
}

int line_admit_ok(lw_node *n, const uint8_t *admit, const uint8_t caller_kid[32])
{
	return admit && admit_verify(n, admit, caller_kid);
}

int line_admit_for(lw_node *n, uint8_t out[ADMIT_LEN])
{
	if (!n->line.have && n->cfg.has_admit) {	/* not on a line yet: what the operator gave us */
		memcpy(out, n->cfg.admit, ADMIT_LEN);
		return ADMIT_LEN;
	}
	if (!n->line.have)
		return 0;
	memcpy(out, n->line.admit, ADMIT_LEN);
	return ADMIT_LEN;
}

/* ---- persistence ------------------------------------------------------------- */

static void path_of(lw_node *n, char *out, size_t cap)
{
	snprintf(out, cap, "%s/lines", n->cfg.state_dir);
}

static void line_save(lw_node *n)
{
	char path[320], hex[ADMIT_LEN * 2 + 1], kh[65], eh[65], sh[65];
	uint8_t sk[32], blob[24 + 32 + 16];
	FILE *f;
	int i;

	path_of(n, path, sizeof path);
	if (!n->line.have) {
		unlink(path);
		return;
	}
	crypto_blake2b_keyed(sk, 32, n->id.seed, 32, (const uint8_t *)"longwire lines v1", 17);
	lw_rand(blob, 24);
	crypto_aead_lock(blob + 24, blob + 24 + 32, sk, blob, 0, 0, n->line.key, 32);
	crypto_wipe(sk, 32);
	if (!(f = fopen(path, "w")))
		return;
	chmod(path, 0600);
	fprintf(f, "longwire-line 1\n");
	lw_hex(hex, n->line.id, 8);
	fprintf(f, "id %s\n", hex);
	lw_hex(kh, n->line.founder, 32);
	fprintf(f, "founder %s\n", kh);
	{
		char bh[sizeof blob * 2 + 1];

		lw_hex(bh, blob, sizeof blob);
		fprintf(f, "key %s\n", bh);
	}
	lw_hex(hex, n->line.admit, ADMIT_LEN);
	fprintf(f, "admit %s\n", hex);
	if (n->line.managed) {
		lw_hex(kh, n->line.authority, 32);
		fprintf(f, "authority %s\n", kh);
		for (i = 0; i < n->line.nrev; i++) {
			char rh[EVICT_LEN * 2 + 1];

			lw_hex(rh, n->line.rev[i], EVICT_LEN);
			fprintf(f, "revoke %s\n", rh);
		}
		if (n->line.roll_t) {
			char rh[ROLL_LEN * 2 + 1];

			lw_hex(rh, n->line.roll, ROLL_LEN);
			fprintf(f, "roll %s\n", rh);
		}
	}
	for (i = 0; i < n->line.nm; i++) {
		lw_hex(kh, n->line.m[i].key_id, 32);
		lw_hex(eh, n->line.m[i].ed_pub, 32);
		lw_hex(sh, n->line.m[i].sb, 32);
		fprintf(f, "member %s %s %s %d %s\n", n->line.m[i].number, kh, eh, n->line.m[i].role, sh);
	}
	for (i = 0; i < n->line.nev; i++) {
		lw_hex(kh, n->line.ev[i].key_id, 32);
		fprintf(f, "evict %s %llu\n", kh, (unsigned long long)n->line.ev[i].time);
	}
	fclose(f);
}

static int unhexn(const char *s, uint8_t *out, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		unsigned v;

		if (sscanf(s + 2 * i, "%2x", &v) != 1)
			return -1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

static void line_load(lw_node *n)
{
	char path[320], l[1024], a[400], b[200], c[200], sbh[200];
	uint8_t blob[24 + 32 + 16], sk[32];
	FILE *f;
	int have_key = 0, have_admit = 0, d, got;
	unsigned long long t;

	path_of(n, path, sizeof path);
	if (!(f = fopen(path, "r")))
		return;
	memset(&n->line, 0, sizeof n->line);
	while (fgets(l, sizeof l, f)) {
		if (sscanf(l, "id %199s", a) == 1)
			unhexn(a, n->line.id, 8);
		else if (sscanf(l, "founder %199s", a) == 1)
			unhexn(a, n->line.founder, 32);
		else if (sscanf(l, "authority %199s", a) == 1 && strlen(a) == 64) {
			unhexn(a, n->line.authority, 32);
			n->line.managed = 1;
		} else if (sscanf(l, "revoke %399s", a) == 1 && strlen(a) == EVICT_LEN * 2 && n->line.nrev < REV_MAX)
			unhexn(a, n->line.rev[n->line.nrev++], EVICT_LEN);
		else if (sscanf(l, "roll %399s", a) == 1 && strlen(a) == ROLL_LEN * 2 && !unhexn(a, n->line.roll, ROLL_LEN))
			n->line.roll_t = get64(n->line.roll + 8);
		else if (sscanf(l, "key %399s", a) == 1 && strlen(a) == sizeof blob * 2 && !unhexn(a, blob, sizeof blob)) {
			crypto_blake2b_keyed(sk, 32, n->id.seed, 32, (const uint8_t *)"longwire lines v1", 17);
			have_key = !crypto_aead_unlock(n->line.key, blob + 24 + 32, sk, blob, 0, 0, blob + 24, 32);
			crypto_wipe(sk, 32);
		} else if (sscanf(l, "admit %399s", a) == 1 && strlen(a) == ADMIT_LEN * 2) {
			unhexn(a, n->line.admit, ADMIT_LEN);
			have_admit = 1;			/* a file from before admits had an end holds one the size of the old layout: not read (no legacy) */
		}
		else if ((got = sscanf(l, "member %9s %199s %199s %d %199s", a, b, c, &d, sbh)) >= 4 && strlen(b) == 64 && strlen(c) == 64 &&
			 strlen(a) == NUMLEN && num_wire_ok((const uint8_t *)a) && n->line.nm < LINE_MAX) {
			lw_member *m = &n->line.m[n->line.nm++];

			memset(m, 0, sizeof *m);
			snprintf(m->number, sizeof m->number, "%s", a);
			unhexn(b, m->key_id, 32);
			unhexn(c, m->ed_pub, 32);
			m->role = (uint8_t)d;
			if (got == 5 && strlen(sbh) == 64)
				unhexn(sbh, m->sb, 32);
			else
				home_kid(n, m->sb);		/* saved before members named their switchboard */
		} else if (sscanf(l, "evict %199s %llu", a, &t) == 2 && strlen(a) == 64 && n->line.nev < REV_MAX) {
			unhexn(a, n->line.ev[n->line.nev].key_id, 32);
			n->line.ev[n->line.nev++].time = t;
		}
	}
	fclose(f);
	if (have_key && have_admit && n->line.nm > 0) {
		n->line.have = 1;
		n->line.joined = n->now;
	} else
		memset(&n->line, 0, sizeof n->line);
}

void line_init(lw_node *n)
{
	line_load(n);
	site_derive(n);
}

/* ---- joining ----------------------------------------------------------------- */

static void send_bye(lw_node *n, lw_peer *p, int leave)
{
	uint8_t b = (uint8_t)leave;

	ctl_msg_send(n, p, M_BYE, &b, 1);
}

static void found(lw_node *n)
{
	lw_member *m;
	uint8_t hk[32];

	memset(&n->line, 0, sizeof n->line);
	lw_rand(n->line.id, 8);
	lw_rand(n->line.key, 32);
	memcpy(n->line.founder, n->id.key_id, 32);
	if (n->cfg.has_authority) {
		memcpy(n->line.authority, n->cfg.authority, 32);
		n->line.managed = 1;
	}
	home_kid(n, hk);
	m = mem_add(n, n->number, n->id.key_id, my_ed(n), 0, hk);
	(void)m;
	admit_make(n, n->id.key_id, 0, n->line.admit);	/* the founder admits itself */
	n->line.have = 1;
	n->line.joined = n->now;
	line_save(n);
	site_derive(n);
}

static void member_json(lw_node *n, const char *event, const char *number)
{
	char f[16];

	fmt_number(f, number);
	ev_emit(n, "{\"event\":\"%s\",\"number\":\"%s\"}", event, f);
}

/* What a peer's admit says about how long it stays: held until then (docs/adr/0065). The founder's own admit has no end. */
static void peer_note_admit(lw_node *n, lw_peer *p, const uint8_t *a)
{
	p->admit_by = 0;
	p->admit_until = admit_founders(n, a) ? 0 : get64(a + ADM_EXPIRES);
	p->admit_issued = get64(a + ADM_ISSUED);
}

/* Vouch for the node at the other end of `p`: record it, admit it, tell it the line, tell the members. */
static void admit_peer(lw_node *n, lw_peer *p)
{
	uint8_t admit[ADMIT_LEN], b[8 + 32 + 32 + 32 + ADMIT_LEN + 1 + LINE_MAX * MEMBER_LEN];
	uint8_t mb[MEMBER_LEN + ADMIT_LEN], hk[32];
	size_t o = 0;
	int i;

	if (!p->number[0] || !n->number[0])
		return;
	if (line_evicted(n, p->key_id)) {
		send_bye(n, p, 0);
		ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"that node was evicted from the line\"}");
		return;
	}
	if (line_revs_full(n)) {		/* no room to remember one more revocation: no one more comes in (docs/adr/0065) */
		send_bye(n, p, 0);
		ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"this line holds as many revocations as it can: its operator must roll it\"}");
		return;
	}
	if (n->line.managed) {			/* the operator decides: we only check what the caller brought */
		if (!line_presented(n, p->number, admit) || !admit_verify(n, admit, p->key_id)) {
			send_bye(n, p, 0);
			if (evicted_at(n, p->key_id))
				ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"that node was evicted from the line (the operator's new admit brings it back)\"}");
			else
				ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"this line is managed: ask its operator for an admit (or a new one, if yours has ended)\"}");
			return;
		}
		peer_note_admit(n, p, admit);
	}
	home_kid(n, hk);
	if (!mem_add(n, p->number, p->key_id, peer_ed_pub(p), 0, hk)) {
		send_bye(n, p, 0);
		ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"the line is full (%d)\"}", LINE_MAX);
		return;
	}
	if (!n->line.managed)
		admit_make(n, p->key_id, 0, admit);
	memcpy(b, n->line.id, 8);
	memcpy(b + 8, n->line.founder, 32);
	memcpy(b + 40, n->line.key, 32);
	memcpy(b + 72, n->line.authority, 32);
	memcpy(b + 104, admit, ADMIT_LEN);
	o = 104 + ADMIT_LEN;
	b[o++] = (uint8_t)n->line.nm;
	for (i = 0; i < n->line.nm; i++) {
		memcpy(b + o, n->line.m[i].number, NUMLEN);
		memcpy(b + o + 9, n->line.m[i].key_id, 32);
		memcpy(b + o + 41, n->line.m[i].ed_pub, 32);
		b[o + 73] = n->line.m[i].role;
		memcpy(b + o + 74, n->line.m[i].sb, 32);
		o += MEMBER_LEN;
	}
	ctl_msg_send(n, p, M_LINE, b, o);
	if (n->line.managed)
		ctl_msg_send(n, p, M_ADMIT, n->line.admit, ADMIT_LEN);	/* the newcomer holds us to our admit as well (docs/adr/0069) */
	p->linked = 1;
	memcpy(mb, p->number, NUMLEN);			/* the others learn of the newcomer, and hold its admit */
	memcpy(mb + 9, p->key_id, 32);
	memcpy(mb + 41, peer_ed_pub(p), 32);
	mb[73] = 0;
	memcpy(mb + 74, hk, 32);
	memcpy(mb + MEMBER_LEN, admit, ADMIT_LEN);
	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *q = &n->peers[i];

		if (q->used && !q->is_sb && q != p && q->linked && q->cur.valid)
			ctl_msg_send(n, q, M_MEMBER, mb, sizeof mb);
	}
	line_save(n);
	member_json(n, "member-joined", p->number);
}

/* The admit a caller put in its dial, kept with the call. */
int line_presented(lw_node *n, const char *number, uint8_t out[ADMIT_LEN])
{
	int i;

	for (i = 0; i < n->ncalls; i++)
		if (n->calls[i].state != CALL_IDLE && n->calls[i].has_admit && !strcmp(n->calls[i].number, number)) {
			memcpy(out, n->calls[i].admit, ADMIT_LEN);
			return 1;
		}
	return 0;
}

static void decide(lw_node *n, lw_peer *p)
{
	int mine = n->line.have, theirs = p->lh_has;

	if (!mine && !theirs) {
		if (!p->dialer) {			/* the callee founds the line */
			found(n);
			admit_peer(n, p);
		}
	} else if (mine && !theirs) {
		admit_peer(n, p);			/* we are the hub */
	} else if (!mine) {
		/* they are the hub: their LINE will come */
	} else if (!memcmp(n->line.id, p->lh_id, 8)) {
		if (mem_find(n, p->key_id)) {
			p->linked = 1;			/* a mesh edge between two members */
			n->line.joined = n->line.joined ? n->line.joined : n->now;
		} else if (!line_evicted(n, p->key_id))
			admit_peer(n, p);		/* it claims our line but we have no record of it: treat it as new */
		else
			send_bye(n, p, 0);
	} else {
		send_bye(n, p, 0);
		ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"that node is on another line\"}");
	}
}

/* Does this number belong to this key, as far as the line's records say? 1 yes, -1 the record for that number is another key's, 0 no record. A member's record is its
 * sender's word; what a mesh call reached is the key that answered (docs/adr/0064). */
int line_number_is(lw_node *n, const char *number, const uint8_t kid[32])
{
	int i;

	for (i = 0; i < n->line.nm; i++)
		if (!strcmp(n->line.m[i].number, number))
			return memcmp(n->line.m[i].key_id, kid, 32) ? -1 : 1;
	return 0;
}

void line_call_up(lw_node *n, lw_peer *p)
{
	uint8_t b[9];
	lw_member *rec = n->line.have && p->have_bundle ? mem_find(n, p->key_id) : 0;

	if (rec)
		memcpy(rec->ed_pub, peer_ed_pub(p), 32);	/* the record of a member we are in touch with takes the key from its own bundle: a record made by another member cannot lock it out later (docs/adr/0064) */

	p->rev_next = n->now + 2.0;		/* a new session: tell it what the operator revoked, once it is listening */
	if (n->line.have && n->line.managed && rec) {	/* and show it our admit, and wait for its: a member we cannot see the end of is not one we hold (docs/adr/0065) */
		ctl_msg_send(n, p, M_ADMIT, n->line.admit, ADMIT_LEN);
		p->admit_until = p->admit_issued = 0;
		p->admit_by = n->now + 20.0;
		p->admit_tx = n->now;
	}
	if (!n->registered || p->lh_sent)
		return;
	b[0] = (uint8_t)n->line.have;
	memcpy(b + 1, n->line.id, 8);
	ctl_msg_send(n, p, M_LINEHELLO, b, 9);
	p->lh_sent = 1;
	if (p->lh_got)
		decide(n, p);
}

static void handle_line(lw_node *n, lw_peer *p, const uint8_t *pl, size_t len)
{
	size_t o = 104 + ADMIT_LEN;
	uint8_t hk[32], none[32] = {0};
	int nm, i;

	if (len < o + 1)
		return;
	nm = pl[o++];
	if (nm < 1 || nm > LINE_MAX || len < o + (size_t)nm * MEMBER_LEN)
		return;
	if (n->line.have)			/* already on a line: this is either ours again or a stranger's */
		return;
	if (n->cfg.has_admit && memcmp(pl + 72, n->cfg.admit + ADM_BY, 32))	/* given an operator's admit: that operator's line, and no other */
		return;
	if (n->cfg.has_admit && memcmp(pl + 104, n->cfg.admit, ADMIT_LEN))	/* the admit we were given is the one we hold, not one that came back by way of a switchboard */
		return;
	home_kid(n, hk);
	memset(&n->line, 0, sizeof n->line);
	memcpy(n->line.id, pl, 8);
	memcpy(n->line.founder, pl + 8, 32);
	memcpy(n->line.key, pl + 40, 32);
	memcpy(n->line.authority, pl + 72, 32);
	n->line.managed = memcmp(none, pl + 72, 32) != 0;
	memcpy(n->line.admit, pl + 104, ADMIT_LEN);
	for (i = 0; i < nm; i++) {
		const uint8_t *e = pl + o + (size_t)i * MEMBER_LEN;
		char num[NUMLEN + 1];

		memcpy(num, e, NUMLEN);
		num[NUMLEN] = 0;
		if (!num_wire_ok(e))			/* a number is nine digits: it goes into events and the line file */
			continue;
		if (!memcmp(e + 74, hk, 32))		/* members of other switchboards arrive with federation */
			mem_add(n, num, e + 9, e + 41, e[73], e + 74);
	}
	if (!mem_find(n, n->id.key_id) && n->number[0])
		mem_add(n, n->number, n->id.key_id, my_ed(n), 0, hk);
	n->line.have = 1;			/* provisionally, to run the admit through the same check as any other */
	if (!mem_find(n, p->key_id) || !admit_verify(n, n->line.admit, n->id.key_id)) {
		memset(&n->line, 0, sizeof n->line);	/* it does not hold together: not our line */
		return;
	}
	n->line.joined = n->now;
	p->linked = 1;
	if (n->line.managed) {
		ctl_msg_send(n, p, M_ADMIT, n->line.admit, ADMIT_LEN);	/* the member we joined through learns when ours ends, and we wait for its: it is held to an admit like any member (docs/adr/0069) */
		p->admit_until = p->admit_issued = 0;
		p->admit_by = n->now + 20.0;
		p->admit_tx = n->now;
	}
	line_save(n);
	site_derive(n);
	{
		char hx[17];

		lw_hex(hx, n->line.id, 8);
		ev_emit(n, "{\"event\":\"line-joined\",\"line\":\"%.6s\",\"members\":%d}", hx, n->line.nm);
	}
}

static void handle_member(lw_node *n, lw_peer *p, const uint8_t *pl, size_t len)
{
	char num[NUMLEN + 1];
	uint8_t hk[32];

	if (len < MEMBER_LEN + ADMIT_LEN || !n->line.have || !mem_find(n, p->key_id) || !num_wire_ok(pl))
		return;
	memcpy(num, pl, NUMLEN);
	num[NUMLEN] = 0;
	if (line_revs_full(n) || !admit_verify(n, pl + MEMBER_LEN, pl + 9))	/* the newcomer must carry a member's signature, and there must be room to remember its revocation */
		return;
	home_kid(n, hk);
	if (memcmp(pl + 74, hk, 32) || mem_find(n, pl + 9) || !mem_add(n, num, pl + 9, pl + 41, pl[73], pl + 74))
		return;
	line_save(n);
	member_json(n, "member-joined", num);
}

/* ---- leaving, eviction, key ---------------------------------------------------- */

void line_leave(lw_node *n)
{
	memset(&n->line, 0, sizeof n->line);
	line_save(n);				/* removes the file */
	site_derive(n);
}

void line_member_left(lw_node *n, const uint8_t kid[32])
{
	lw_member *m = mem_find(n, kid);

	if (m && memcmp(kid, n->id.key_id, 32)) {
		char num[NUMLEN + 1];

		snprintf(num, sizeof num, "%s", m->number);
		mem_remove(n, kid);
		line_save(n);
		member_json(n, "member-left", num);
	}
}

static void apply_evict(lw_node *n, const uint8_t kid[32], uint64_t t)
{
	lw_member *m = mem_find(n, kid);
	lw_peer *q;
	char num[NUMLEN + 1] = "";

	if (!memcmp(kid, n->id.key_id, 32)) {	/* it is us */
		char hx[17];

		lw_hex(hx, n->line.id, 8);
		ev_emit(n, "{\"event\":\"evicted\",\"line\":\"%.6s\"}", hx);
		call_drop_all(n, "you were evicted from the line");
		line_leave(n);
		return;
	}
	if (m)
		snprintf(num, sizeof num, "%s", m->number);
	mem_remove(n, kid);
	record_eviction(n, kid, t);
	if ((q = peer_by_key(n, kid)))
		call_drop_peer(n, q, "evicted from the line");
	line_save(n);
	if (num[0])
		member_json(n, "member-evicted", num);
}

static void store_revoke(lw_node *n, const uint8_t *b)
{
	int i;

	for (i = 0; i < n->line.nrev; i++)
		if (!memcmp(n->line.rev[i], b, 48))
			return;
	if (ev_old(n, b + 8, get64(b + 40)))
		return;				/* it cannot matter any more */
	if (n->line.nrev >= REV_MAX) {
		ev_emit(n, "{\"event\":\"revocations-full\"}");
		return;			/* never the oldest dropped to make room: a dropped eviction lets its key's old admit back */
	}
	memcpy(n->line.rev[n->line.nrev++], b, EVICT_LEN);
}

/* Does this eviction still bar its key? Not one that cannot matter any more, and not one older than the admit the key now holds: the operator evicted it and later signed it
 * a new admit, and a copy of the old eviction, said again by any member, must not put it off the line a second time (docs/adr/0066). */
static int evict_matters(lw_node *n, const uint8_t kid[32], uint64_t t)
{
	const lw_peer *q;

	if (ev_old(n, kid, t))
		return 0;
	if (n->line.managed && !memcmp(kid, n->id.key_id, 32))
		return !(get64(n->line.admit + ADM_ISSUED) > t && !admit_founders(n, n->line.admit));
	if (n->line.managed && (q = peer_by_key(n, kid)) && q->cur.valid && q->admit_issued > t)
		return 0;
	return 1;
}

static void handle_evict(lw_node *n, lw_peer *p, const uint8_t *pl, size_t len)
{
	uint8_t msg[8 + 32 + 8 + 5];
	const lw_member *ev;

	if (len < 8 + 32 + 8 + 32 + 64 || !n->line.have || memcmp(pl, n->line.id, 8))
		return;
	if (!(ev = mem_find(n, p->key_id)))
		return;
	if (get64(pl + 40) > (uint64_t)time(0) + EVICT_SKEW)
		return;				/* an eviction dated far ahead would bar the key for as long as that date: the time is the signer's own word (docs/adr/0063) */
	memcpy(msg, pl, 48);
	memcpy(msg + 48, "evict", 5);
	if (n->line.managed) {			/* only the operator's word, and any member may carry it */
		if (memcmp(pl + 48, n->line.authority, 32) || !lw_verify(n->line.authority, msg, sizeof msg, pl + 80))
			return;
		if (!evict_matters(n, pl + 8, get64(pl + 40)))
			return;
		store_revoke(n, pl);
	} else if (memcmp(pl + 48, p->key_id, 32) || !lw_verify(peer_ed_pub(p), msg, sizeof msg, pl + 80))	/* from a member, in person: checked against the key in the bundle of the session it came on */
		return;
	else if (!mem_find(n, pl + 8))
		return;				/* a member evicts members: an eviction of a key that is not on the line would only fill the table (docs/adr/0066) */
	apply_evict(n, pl + 8, get64(pl + 40));
}

/* A member left the line because its admit ended (or never was good): forget it, end its sessions, say so. */
static void member_expire(lw_node *n, lw_peer *p)
{
	lw_member *m = mem_find(n, p->key_id);
	char num[NUMLEN + 1] = "";

	if (m)
		snprintf(num, sizeof num, "%s", m->number);
	mem_remove(n, p->key_id);
	call_drop_peer(n, p, "its admit has ended");
	line_save(n);
	if (num[0])
		member_json(n, "member-expired", num);
}

/* A member shows its admit: it is held to it. One that is not good (ended, rolled, made for another key) is a member no longer. */
static void handle_admit(lw_node *n, lw_peer *p, const uint8_t *pl, size_t len)
{
	if (len != ADMIT_LEN || !n->line.have || !n->line.managed || !mem_find(n, p->key_id))
		return;
	if (!admit_verify(n, pl, p->key_id)) {
		member_expire(n, p);
		return;
	}
	peer_note_admit(n, p, pl);
}

static void admit_sweep(lw_node *n);

static int roll_ok(lw_node *n, const uint8_t *r)
{
	uint8_t msg[8 + 8 + 4];

	if (!n->line.have || !n->line.managed || memcmp(r, n->line.id, 8) || memcmp(r + 16, n->line.authority, 32))
		return 0;
	memcpy(msg, r, 16);
	memcpy(msg + 16, "roll", 4);
	return lw_verify(n->line.authority, msg, sizeof msg, r + 48);
}

/* The operator's roll: no admit made before its time is good, so every eviction older than it is covered and can go. Passed on to every member we are linked to. */
static int roll_apply(lw_node *n, const uint8_t *r)
{
	uint64_t t = get64(r + 8);
	int i;

	if (!roll_ok(n, r) || t <= n->line.roll_t || t > (uint64_t)time(0) + EVICT_SKEW)
		return 0;
	memcpy(n->line.roll, r, ROLL_LEN);
	n->line.roll_t = t;
	revs_prune(n);
	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !n->peers[i].is_sb && n->peers[i].linked && n->peers[i].cur.valid)
			ctl_msg_send(n, &n->peers[i], M_ROLL, r, ROLL_LEN);
	for (i = n->line.nm - 1; i >= 0; i--) {		/* a roll voids every admit made before it: a member we cannot show holds a newer one is off the line, and returns by showing it */
		lw_member *m = &n->line.m[i];
		const lw_peer *q = peer_by_key(n, m->key_id);
		char num[NUMLEN + 1];

		if (!memcmp(m->key_id, n->id.key_id, 32) || !memcmp(m->key_id, n->line.founder, 32))
			continue;
		if (q && q->cur.valid && (q->admit_by || q->admit_issued >= t))
			continue;		/* it is in session and shows (or is about to show) an admit: the sweep judges it */
		snprintf(num, sizeof num, "%s", m->number);
		if (q)
			call_drop_peer(n, (lw_peer *)q, "its admit was voided by a roll");
		mem_remove(n, m->key_id);
		member_json(n, "member-expired", num);
	}
	admit_sweep(n);				/* after the roll is passed on, and at once, while the sessions are still up: a member that went quiet first would stay on our list, called in vain */
	line_save(n);
	ev_emit(n, "{\"event\":\"line-rolled\"}");
	return 1;
}

static void handle_linekey(lw_node *n, lw_peer *p, const uint8_t *pl, size_t len)
{
	if (len < 40 || !n->line.have || memcmp(pl, n->line.id, 8) || !mem_find(n, p->key_id) || !p->linked)
		return;
	memcpy(n->line.prev_key, n->line.key, 32);
	memcpy(n->line.key, pl + 8, 32);
	n->line.key_t = n->now;
	line_save(n);
	site_derive(n);
}

void line_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len)
{
	switch (msg) {
	case M_LINEHELLO:
		if (len < 9)
			return;
		p->lh_has = pl[0];
		memcpy(p->lh_id, pl + 1, 8);
		p->lh_got = 1;
		if (p->lh_sent)
			decide(n, p);
		return;
	case M_LINE:
		handle_line(n, p, pl, len);
		return;
	case M_MEMBER:
		handle_member(n, p, pl, len);
		return;
	case M_EVICT:
		handle_evict(n, p, pl, len);
		return;
	case M_ADMIT:
		handle_admit(n, p, pl, len);
		return;
	case M_ROLL:
		if (len == ROLL_LEN && mem_find(n, p->key_id))
			roll_apply(n, pl);
		return;
	case M_LINEKEY:
		handle_linekey(n, p, pl, len);
		return;
	}
}

/* ---- commands ------------------------------------------------------------------ */

/* Take `kid` off the line: tell every member (the evicted one too), forget it, and give everyone left a new line key. */
static void evict_now(lw_node *n, const uint8_t kid[32], uint64_t t, const uint8_t *blob, const char *num)
{
	uint8_t b[40], nk[32];
	lw_peer *ev = peer_by_key(n, kid);
	int i;

	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !n->peers[i].is_sb && n->peers[i].linked && n->peers[i].cur.valid)
			ctl_msg_send(n, &n->peers[i], M_EVICT, blob, EVICT_LEN);
	mem_remove(n, kid);
	record_eviction(n, kid, t);
	lw_rand(nk, 32);
	memcpy(n->line.prev_key, n->line.key, 32);
	memcpy(n->line.key, nk, 32);
	n->line.key_t = n->now;
	memcpy(b, n->line.id, 8);
	memcpy(b + 8, nk, 32);
	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !n->peers[i].is_sb && n->peers[i].linked && n->peers[i].cur.valid && &n->peers[i] != ev)
			ctl_msg_send(n, &n->peers[i], M_LINEKEY, b, 40);
	if (ev)
		call_drop_peer(n, ev, "evicted from the line");
	line_save(n);
	site_derive(n);
	if (num[0])
		member_json(n, "member-evicted", num);
}

int line_cmd(lw_node *n, const char *cmd, char *out, size_t cap)
{
	if (!strncmp(cmd, "admit ", 6)) {		/* a renewed admit from the operator, for this node */
		uint8_t a[ADMIT_LEN];
		int i;

		if (!n->line.have || !n->line.managed)
			return LW_SN(out, cap, "{\"error\":\"this node is not on a line with an operator\"}\n");
		if (strlen(cmd + 6) != ADMIT_LEN * 2 || unhexn(cmd + 6, a, ADMIT_LEN))
			return LW_SN(out, cap, "{\"error\":\"that is not an admit\"}\n");
		if (!admit_verify(n, a, n->id.key_id))
			return LW_SN(out, cap, "{\"error\":\"that admit is not good for this node on this line: it is for another key or line, has ended, or was made before the operator's last roll\"}\n");
		memcpy(n->line.admit, a, ADMIT_LEN);
		n->line.warned = 0;
		n->line.dormant = 0;
		for (i = 0; i < n->maxpeers; i++)		/* the members we are linked to hold us to the new end */
			if (n->peers[i].used && !n->peers[i].is_sb && n->peers[i].linked && n->peers[i].cur.valid)
				ctl_msg_send(n, &n->peers[i], M_ADMIT, a, ADMIT_LEN);
		line_save(n);
		ev_emit(n, "{\"event\":\"admit-renewed\",\"expires\":%llu}", (unsigned long long)get64(a + ADM_EXPIRES));
		return LW_SN(out, cap, "{\"admit\":\"renewed\",\"expires\":%llu}\n", (unsigned long long)get64(a + ADM_EXPIRES));
	}
	if (!strncmp(cmd, "roll ", 5)) {		/* the operator's signed roll, carried by this member */
		uint8_t r[ROLL_LEN];

		if (!n->line.have || !n->line.managed)
			return LW_SN(out, cap, "{\"error\":\"this node is not on a line with an operator\"}\n");
		if (strlen(cmd + 5) != ROLL_LEN * 2 || unhexn(cmd + 5, r, ROLL_LEN))
			return LW_SN(out, cap, "{\"error\":\"that is not a roll\"}\n");
		if (!roll_ok(n, r))
			return LW_SN(out, cap, "{\"error\":\"that roll is not signed by this line's operator\"}\n");
		if (!roll_apply(n, r))
			return LW_SN(out, cap, "{\"error\":\"that roll is not newer than the one this node holds\"}\n");
		return LW_SN(out, cap, "{\"rolled\":true}\n");
	}
	if (!strncmp(cmd, "revoke ", 7)) {		/* the operator's signed eviction, carried by this member */
		uint8_t b[EVICT_LEN], msg[53];
		const lw_member *m;
		char num[NUMLEN + 1] = "";

		if (!n->line.have || !n->line.managed)
			return LW_SN(out, cap, "{\"error\":\"this node is not on a line with an operator\"}\n");
		if (strlen(cmd + 7) != EVICT_LEN * 2 || unhexn(cmd + 7, b, EVICT_LEN))
			return LW_SN(out, cap, "{\"error\":\"that is not an eviction\"}\n");
		memcpy(msg, b, 48);
		memcpy(msg + 48, "evict", 5);
		if (memcmp(b, n->line.id, 8) || memcmp(b + 48, n->line.authority, 32) ||
		    !lw_verify(n->line.authority, msg, sizeof msg, b + 80))
			return LW_SN(out, cap, "{\"error\":\"that eviction is not signed by this line's operator\"}\n");
		if (!evict_matters(n, b + 8, get64(b + 40)))
			return LW_SN(out, cap, "{\"error\":\"that eviction is older than the admit that node now holds, or too old to matter\"}\n");
		store_revoke(n, b);
		if (!memcmp(b + 8, n->id.key_id, 32)) {
			apply_evict(n, b + 8, get64(b + 40));	/* it is about us */
			return LW_SN(out, cap, "{\"revoked\":\"you\"}\n");
		}
		if ((m = mem_find(n, b + 8)))
			snprintf(num, sizeof num, "%s", m->number);
		evict_now(n, b + 8, get64(b + 40), b, num);
		return LW_SN(out, cap, "{\"revoked\":\"%s\"}\n", num[0] ? "member" : "not a member");
	}
	if (!strncmp(cmd, "evict ", 6)) {
		char num[NUMLEN + 1], f[16];
		uint8_t b[EVICT_LEN], sigmsg[8 + 32 + 8 + 5], kid[32];
		uint64_t t = (uint64_t)time(0);
		lw_member *m = 0;
		char perr[160];
		int i;

		if (parse_full(n, cmd + 6, num, perr, sizeof perr))
			return LW_SN(out, cap, "{\"error\":\"%s\"}\n", perr);
		if (!n->line.have)
			return LW_SN(out, cap, "{\"error\":\"you are not on a line\"}\n");
		if (n->line.managed)
			return LW_SN(out, cap, "{\"error\":\"this line has an operator: only the operator evicts (longwire revoke)\"}\n");
		for (i = 0; i < n->line.nm; i++)
			if (!strcmp(n->line.m[i].number, num))
				m = &n->line.m[i];
		fmt_number(f, num);
		if (!m)
			return LW_SN(out, cap, "{\"error\":\"%s is not on this line\"}\n", f);
		if (!memcmp(m->key_id, n->id.key_id, 32))
			return LW_SN(out, cap, "{\"error\":\"that is you: use hangup to leave\"}\n");
		memcpy(kid, m->key_id, 32);
		memcpy(b, n->line.id, 8);
		memcpy(b + 8, kid, 32);
		put64(b + 40, t);
		memcpy(b + 48, n->id.key_id, 32);
		memcpy(sigmsg, b, 48);
		memcpy(sigmsg + 48, "evict", 5);
		lw_sign(&n->id, sigmsg, sizeof sigmsg, b + 80);
		evict_now(n, kid, t, b, num);
		return LW_SN(out, cap, "{\"evicted\":\"%s\"}\n", f);
	}
	if (!strcmp(cmd, "peers"))
		return line_status_json(n, out, cap);
	return -1;
}

/* ---- the mesh maintainer and status --------------------------------------------- */

/* Our own admit on a line with an operator: say when it is ending (14, 3 and 1 days before), and go quiet when it has ended or a roll has voided it: every session ends and
 * none is made until a renewed admit comes (`longwire renew`). 1 if quiet. */
static int admit_own_check(lw_node *n)
{
	const uint8_t *a = n->line.admit;
	uint64_t now = (uint64_t)time(0), e = get64(a + ADM_EXPIRES), d;
	int lvl;

	if (admit_founders(n, a))
		return 0;
	if (now >= e || get64(a + ADM_ISSUED) < n->line.roll_t) {
		if (!n->line.dormant) {
			n->line.dormant = 1;
			ev_emit(n, "{\"event\":\"admit-ended\"}");
			call_drop_all(n, "your admit has ended");
		}
		return 1;
	}
	n->line.dormant = 0;
	d = (e - now + 86399) / 86400;
	lvl = d <= 1 ? 3 : d <= 3 ? 2 : d <= 14 ? 1 : 0;
	if (lvl > n->line.warned) {
		n->line.warned = lvl;
		ev_emit(n, "{\"event\":\"admit-expiring\",\"days\":%llu}", (unsigned long long)d);
	}
	return 0;
}

/* The members we hold sessions with are held to the admit they showed: one that has ended, was rolled, or was never shown in time is a member no longer. */
static void admit_sweep(lw_node *n)
{
	uint64_t now = (uint64_t)time(0);
	int i;

	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *q = &n->peers[i];

		if (!q->used || q->is_sb || !q->cur.valid || !mem_find(n, q->key_id) || !memcmp(q->key_id, n->id.key_id, 32))
			continue;
		if (q->admit_by && n->now > q->admit_by)
			member_expire(n, q);			/* it showed none in time: it is not a member (docs/adr/0066) */
		else if (q->admit_by && n->now - q->admit_tx >= 4.0) {
			q->admit_tx = n->now;			/* datagrams are not acknowledged: ours is said again while theirs is awaited */
			ctl_msg_send(n, q, M_ADMIT, n->line.admit, ADMIT_LEN);
		} else if (q->admit_until && (now >= q->admit_until || q->admit_issued < n->line.roll_t))
			member_expire(n, q);
	}
}

void line_timers(lw_node *n)
{
	int i, up = 0, need = 0;

	if (!n->line.have && n->cfg.has_authority && n->registered && n->number[0])
		found(n);			/* a managed line exists from the start: the operator needs its id to sign for */
	if (n->line.have && n->line.managed && n->now - n->line.chk >= 1.0) {
		n->line.chk = n->now;
		revs_prune(n);
		if (admit_own_check(n))		/* our own admit ended */
			return;
		admit_sweep(n);
	}
	if (n->line.dormant)
		return;
	if (n->line.have && n->line.managed && (n->line.nrev || n->line.roll_t))
		for (i = 0; i < n->maxpeers; i++) {	/* tell whoever links to us what the operator has revoked */
			lw_peer *q = &n->peers[i];
			int k;

			if (!q->used || q->is_sb || !q->linked || !q->cur.valid || !q->rev_next || n->now < q->rev_next)
				continue;
			for (k = 0; k < n->line.nrev; k++)	/* datagrams are not acknowledged: say it again now and then */
				ctl_msg_send(n, q, M_EVICT, n->line.rev[k], EVICT_LEN);
			if (n->line.roll_t)
				ctl_msg_send(n, q, M_ROLL, n->line.roll, ROLL_LEN);
			q->rev_next = n->now + 30.0;
		}
	if (!n->line.have || !n->registered || n->now - n->line.tick < 1.0)
		return;
	n->line.tick = n->now;
	for (i = 0; i < n->line.nm; i++) {
		lw_member *m = &n->line.m[i];
		lw_peer *q;
		char err[80];

		if (!memcmp(m->key_id, n->id.key_id, 32))
			continue;
		need++;
		q = peer_by_key(n, m->key_id);
		if (q && q->cur.valid) {
			m->tries = 0;
			m->next_try = 0;
			up++;
			continue;
		}
		if (m->tries >= 2 && !call_for_number(n, m->number) && n->now >= m->next_relay) {
			relay_request(n, m->key_id, &n->line.rr);	/* two direct tries came to nothing: have a member carry the call */
			m->next_relay = n->now + 6.0;
		}
		if ((q && q->relayed) || call_for_number(n, m->number) || n->now < m->next_try)
			continue;
		if (!call_dial(n, m->number, 1, err, sizeof err)) {	/* dial everyone we lack a session with: it is how the mesh heals */
			m->tries++;
			m->next_try = n->now + (m->tries > 3 ? 60.0 : 8.0 * (double)(1 << (m->tries - 1)));
		}
	}
	if (need == up) {
		if (!n->line.said_complete && need > 0) {
			n->line.said_complete = 1;
			ev_emit(n, "{\"event\":\"line-complete\",\"members\":%d}", n->line.nm);
		}
		n->line.said_partial = 0;
	} else if (n->now - n->line.joined > 30.0 && !n->line.said_partial) {
		n->line.said_partial = 1;
		n->line.said_complete = 0;
		ev_emit(n, "{\"event\":\"line-partial\",\"members\":%d,\"up\":%d}", n->line.nm, up + 1);
	}
}

/* The line as a JSON value: null, or an object. */
int line_value(lw_node *n, char *o, size_t cap)
{
	char hx[17], kh[65], f[16];
	int len, i, up = 0;

	if (!n->line.have)
		return LW_SN(o, cap, "null");
	lw_hex(hx, n->line.id, 8);
	{
		char ah[65] = "";

		if (n->line.managed)
			lw_hex(ah, n->line.authority, 32);
		len = LW_SN(o, cap, "{\"id\":\"%.6s\",\"id_full\":\"%s\",\"managed\":%s,\"authority\":\"%s\",\"founder\":%s,\"members\":[",
			       hx, hx, n->line.managed ? "true" : "false", ah, memcmp(n->line.founder, n->id.key_id, 32) ? "false" : "true");
	}
	for (i = 0; i < n->line.nm && len < (int)cap - 200; i++) {
		lw_member *m = &n->line.m[i];
		lw_peer *q = peer_by_key(n, m->key_id);
		int me = !memcmp(m->key_id, n->id.key_id, 32), ok = me || (q && q->cur.valid);

		up += ok;
		fmt_number(f, m->number);
		lw_hex(kh, m->key_id, 32);
		len = lw_app(o, cap, len, "%s{\"number\":\"%s\",\"key_id\":\"%s\",\"state\":\"%s\"}",
				i ? "," : "", f, kh, me ? "you" : ok ? "up" : "down");
	}
	len = lw_app(o, cap, len, "],\"up\":%d,\"partial\":%s,\"admit_expires\":%llu,\"admit_days\":%lld,\"admit_ended\":%s,\"revocations\":%d,\"revocations_full\":%s,\"rolled\":%llu}", up, up < n->line.nm ? "true" : "false",
			(unsigned long long)(n->line.managed && !admit_founders(n, n->line.admit) ? get64(n->line.admit + ADM_EXPIRES) : 0),
			n->line.managed && !admit_founders(n, n->line.admit) ? ((long long)get64(n->line.admit + ADM_EXPIRES) - (long long)time(0) + 86399) / 86400 : -1LL,
			n->line.dormant ? "true" : "false",
			n->line.nrev > n->line.nev ? n->line.nrev : n->line.nev, line_revs_full(n) ? "true" : "false", (unsigned long long)n->line.roll_t);
	return len;
}

int line_status_json(lw_node *n, char *o, size_t cap)
{
	int len = LW_SN(o, cap, "{\"line\":");

	len += line_value(n, o + len, cap - (size_t)len);
	return lw_app(o, cap, len, "}\n");
}

/* What a new event subscriber missed about the line. */
int line_replay(lw_node *n, char *o, size_t cap)
{
	char hx[17];

	if (!n->line.have)
		return 0;
	lw_hex(hx, n->line.id, 8);
	return LW_SN(o, cap, "{\"event\":\"line-joined\",\"line\":\"%.6s\",\"members\":%d}\n", hx, n->line.nm);
}
