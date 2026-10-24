/* SPDX-License-Identifier: GPL-3.0-or-later
 * The switchboard worker (docs/SWITCHBOARD.md): numbers, presence, call
 * introductions. It never sees a frame and forgets a call the moment it has
 * sent GO. Decisions that are policy (may this key register? may this call
 * ring?) go to the script as events when --policy script is on. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "int.h"

#define PRESENT 80.0			/* a node is online if it was heard this recently */
#define MAXPEND 32
#define MAXCALLS 32

typedef struct reg {
	char    number[NUMLEN + 1];
	uint8_t bundle[LW_BUNDLE], key_id[32];
	double  seen;
	long    wall, wall_logged;		/* unix time last heard, and the last time that was written to the log */
	char    name[LW_NAME_MAX + 1];		/* in memory only: sent again whenever the node registers */
	int     listed;
	double  look_tok, look_t;		/* a budget for directory lookups */
} reg;

typedef struct bcall {
	int      used;
	uint8_t  id[8], caller[32], callee[32];
	lw_cand  cc[MAXCANDS];
	int      ncc, strict, callee_strict;	/* strict: the caller said its NAT makes a mapping per destination */
	uint8_t  admit[ADMIT_LEN];	/* the caller's admit to a line, carried untouched: the switchboard does not know lines */
	int      alen;
	double   deadline;
} bcall;

enum { P_REG, P_DIAL, P_CODE };

typedef struct pend {
	int      used, kind, id;
	uint8_t  key[32], callee[32];	/* key = who asked */
	lw_cand  cc[MAXCANDS];
	int      ncc, strict;
	uint8_t  admit[ADMIT_LEN];
	int      alen;
	char     name[LW_NAME_MAX + 1];	/* (a registration) */
	int      listed;
	double   deadline;
} pend;

struct nbind {				/* a listed name belongs to the first key that listed it, like a number */
	uint8_t key[32];
	char    name[LW_NAME_MAX + 1];
};

struct codeent {				/* the registry's book: which switchboard key holds which code */
	uint8_t key[32];
	char    code[10];
};

struct held {				/* a released number: kept back for as long as it was unused, so a stale pin cannot meet a stranger */
	char    number[NUMLEN + 1];
	uint8_t bundle[LW_BUNDLE], key_id[32];
	long    released;
};

struct board {
	struct held *held;
	int    nheld, capheld;
	double sweep_at;
	struct codeent *codes;
	int    ncodes, capcodes, seq;		/* seq: how many codes have been handed out in order (not counting the reserved 1) */
	char   clog[320];
	uint8_t pubkey[32];
	int    have_pub;
	struct nbind *bind;
	int    nbind, capbind;
	char   nlog[320];
	reg   *regs;
	int    n, cap, next_id;
	bcall  calls[MAXCALLS];
	pend   pend[MAXPEND];
	char   log[320];
};

/* Names compare without regard to ASCII case; beyond ASCII they compare byte for byte (no Unicode folding). */
static int name_eq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;

		if (x != y)
			return 0;
	}
	return *a == *b;
}

static struct nbind *bind_of(struct board *b, const uint8_t key[32])
{
	int i;

	for (i = 0; i < b->nbind; i++)
		if (!crypto_verify32(b->bind[i].key, key))
			return &b->bind[i];
	return 0;
}

/* Does another key hold this name? */
static int name_taken(struct board *b, const uint8_t key[32], const char *name)
{
	int i;

	for (i = 0; i < b->nbind; i++)
		if (crypto_verify32(b->bind[i].key, key) && name_eq(b->bind[i].name, name))
			return 1;
	return 0;
}

static void bind_log(struct board *b, const uint8_t key[32], const char *name)
{
	FILE *f = fopen(b->nlog, "a");
	char kh[65];
	size_t i;

	if (!f)
		return;
	chmod(b->nlog, 0600);
	lw_hex(kh, key, 32);
	fprintf(f, "%s ", kh);
	if (!name[0])
		fputc('-', f);
	for (i = 0; name[i]; i++)
		fprintf(f, "%02x", (unsigned char)name[i]);
	fputc('\n', f);
	fclose(f);
}

static void bind_set(struct board *b, const uint8_t key[32], const char *name, int log)
{
	struct nbind *e = bind_of(b, key);

	if (!name[0]) {				/* released */
		if (e) {
			*e = b->bind[--b->nbind];
			if (log)
				bind_log(b, key, "");
		}
		return;
	}
	if (e && !strcmp(e->name, name))
		return;
	if (!e) {
		if (b->nbind == b->capbind) {
			b->capbind = b->capbind ? b->capbind * 2 : 32;
			b->bind = realloc(b->bind, (size_t)b->capbind * sizeof *b->bind);
		}
		e = &b->bind[b->nbind++];
		memcpy(e->key, key, 32);
	}
	snprintf(e->name, sizeof e->name, "%s", name);
	if (log)
		bind_log(b, key, name);
}

/* The rule for a name given at registration: a name another key has listed is not available to anyone else (listed or
 * not), a listed name is bound to its key from then on, and giving no name, or not listing it, releases the binding. */
static const char *name_apply(struct board *b, const uint8_t key[32], const char *name, int listed, int commit)
{
	if (name[0] && name_taken(b, key, name))
		return "that name is listed by another node";
	if (commit)
		bind_set(b, key, name[0] && listed ? name : "", 1);
	return 0;
}

/* ---- the registry of switchboard codes (ADR 0024) ------------------------------------------ */

/* The k-th code (from 0) of the plan: prefix-free, shortest first. At each length only the lower half of the free prefixes is
 * given out and the upper half is kept to open the next length: 20..49, then 500..649, 6500..7249, and so on. 555 is skipped. */
void lw_nth_code(unsigned long k, char out[10])
{
	unsigned long long lo = 20, hi = 80;
	int len = 2;

	for (;; len++, lo = (lo + (hi - lo) / 2) * 10, hi *= 10) {
		unsigned long long cap = (hi - lo) / 2, use = cap, v;

		if (len == 3 && 555 >= lo && 555 < lo + cap)
			use--;
		if (len >= 9 || k < use) {
			v = lo + k;
			if (len == 3 && v >= 555)
				v++;
			snprintf(out, 10, "%llu", v);
			return;
		}
		k -= (unsigned long)use;
	}
}

static const char *code_of(struct board *b, const uint8_t key[32])
{
	int i;

	for (i = 0; i < b->ncodes; i++)
		if (!crypto_verify32(b->codes[i].key, key))
			return b->codes[i].code;
	return 0;
}

static void code_put(struct board *b, const uint8_t key[32], const char *code, int log)
{
	if (b->ncodes == b->capcodes) {
		b->capcodes = b->capcodes ? b->capcodes * 2 : 16;
		b->codes = realloc(b->codes, (size_t)b->capcodes * sizeof *b->codes);
	}
	memcpy(b->codes[b->ncodes].key, key, 32);
	snprintf(b->codes[b->ncodes].code, 10, "%s", code);
	b->ncodes++;
	if (strcmp(code, "1"))
		b->seq++;
	if (log) {
		FILE *f = fopen(b->clog, "a");
		char kh[65];

		if (f) {
			chmod(b->clog, 0600);
			lw_hex(kh, key, 32);
			fprintf(f, "%s %s\n", kh, code);
			fclose(f);
		}
	}
}

/* The code of a switchboard: the one it was given before, else "1" if it is the public switchboard, else the next in the plan. */
static const char *code_issue(struct board *b, const uint8_t key[32])
{
	const char *have = code_of(b, key);
	char code[10];

	if (have)
		return have;
	if (b->have_pub && !crypto_verify32(b->pubkey, key))
		strcpy(code, "1");
	else
		lw_nth_code((unsigned long)b->seq, code);
	code_put(b, key, code, 1);
	return code_of(b, key);
}

static reg *reg_by_key(struct board *b, const uint8_t k[32])
{
	int i;

	for (i = 0; i < b->n; i++)
		if (!crypto_verify32(b->regs[i].key_id, k))
			return &b->regs[i];
	return 0;
}

static reg *reg_by_number(struct board *b, const char *num)
{
	int i;

	for (i = 0; i < b->n; i++)
		if (!strcmp(b->regs[i].number, num))
			return &b->regs[i];
	return 0;
}

static struct held *held_by_number(struct board *b, const char *num)
{
	int i;

	for (i = 0; i < b->nheld; i++)
		if (!strcmp(b->held[i].number, num))
			return &b->held[i];
	return 0;
}

static struct held *held_by_key(struct board *b, const uint8_t k[32])
{
	int i;

	for (i = 0; i < b->nheld; i++)
		if (!crypto_verify32(b->held[i].key_id, k))
			return &b->held[i];
	return 0;
}

static void held_drop(struct board *b, struct held *h)
{
	*h = b->held[--b->nheld];
}

static void held_add(struct board *b, const reg *r, long when)
{
	struct held *h;

	if (b->nheld == b->capheld) {
		b->capheld = b->capheld ? b->capheld * 2 : 16;
		b->held = realloc(b->held, (size_t)b->capheld * sizeof *b->held);
	}
	h = &b->held[b->nheld++];
	memcpy(h->number, r->number, sizeof h->number);
	memcpy(h->bundle, r->bundle, LW_BUNDLE);
	memcpy(h->key_id, r->key_id, 32);
	h->released = when;
}

/* The seconds a number may go unheard before it is released, and for which it is then held back (docs/SWITCHBOARD.md). */
static long release_after(const lw_node *n)
{
	return n->cfg.release_after > 0 ? n->cfg.release_after : 365L * 86400;
}

static void log_line(struct board *b, const char *fmt, const char *num, long when, const char *extra)
{
	FILE *f = fopen(b->log, "a");

	if (!f)
		return;
	chmod(b->log, 0600);
	fprintf(f, fmt, num, when, extra);
	fclose(f);
}

/* A node was heard: remember when, in the log at most once in a while (a tenth of the release time, an hour at most). */
static void heard(lw_node *n, reg *r)
{
	struct board *b = n->bd;
	long w = (long)time(0), gap = release_after(n) / 10;

	r->seen = n->now;
	r->wall = w;
	if (gap > 3600)
		gap = 3600;
	if (gap < 1)
		gap = 1;
	if (w - r->wall_logged >= gap) {
		r->wall_logged = w;
		log_line(b, "seen %s %ld%s\n", r->number, w, "");
	}
}

/* Release numbers unheard for a year, and forget those that have been held back for another. */
static void sweep(lw_node *n)
{
	struct board *b = n->bd;
	long w = (long)time(0), ra = release_after(n);
	int i;

	for (i = 0; i < b->n;) {
		reg *r = &b->regs[i];

		if (w - r->wall > ra) {
			char kh[65];

			lw_hex(kh, r->key_id, 32);
			log_line(b, "released %s %ld %s\n", r->number, w, kh);
			held_add(b, r, w);
			ev_emit(n, "{\"event\":\"number-released\",\"number\":\"%s\"}", r->number);
			*r = b->regs[--b->n];
		} else
			i++;
	}
	for (i = 0; i < b->nheld;)
		if (w - b->held[i].released > ra)
			held_drop(b, &b->held[i]);
		else
			i++;
}

static int cands_read(const uint8_t *p, size_t len, lw_cand *c, int *nc, int *strict)
{
	int i, k, o = 1, w, known;

	if (len < 1)
		return -1;
	k = p[0] & ~CANDS_STRICT;
	*strict = p[0] & CANDS_STRICT ? 1 : 0;
	if (k > MAXCANDS - 1)
		return -1;
	*nc = 0;
	for (i = 0; i < k; i++) {
		if ((w = cand_get(p + o, len - o, &c[*nc], &known)) < 0)
			return -1;
		o += w;
		*nc += known;
	}
	return o;
}

static int cands_write(uint8_t *p, const lw_cand *c, int nc, int strict)
{
	int i, o = 1;

	p[0] = (uint8_t)nc | (strict ? CANDS_STRICT : 0);
	for (i = 0; i < nc; i++)
		o += cand_put(p + o, &c[i]);
	return o;
}

static void observed(const lw_peer *p, lw_cand *c)
{
	sa_cand((const struct sockaddr *)&p->ep, c);
	c->type = CAND_REFL;
}

static int online(lw_node *n, const reg *r)
{
	lw_peer *p = peer_by_key(n, r->key_id);

	return p && p->cur.valid && n->now - r->seen < PRESENT;
}

int board_open(lw_node *n, char *err, size_t errn)
{
	struct board *b = calloc(1, sizeof *b);
	char path[330], line[LW_BUNDLE * 2 + 120];
	FILE *f;

	if (!b) {
		snprintf(err, errn, "out of memory");
		return -1;
	}
	n->bd = b;
	n->sb = -1;
	b->next_id = 1;
	snprintf(b->log, sizeof b->log, "%s/numbers.log", n->cfg.state_dir);
	snprintf(b->nlog, sizeof b->nlog, "%s/names.log", n->cfg.state_dir);
	snprintf(b->clog, sizeof b->clog, "%s/codes.log", n->cfg.state_dir);
	if (n->cfg.public_switchboard[0] && !lw_address_parse(n->cfg.public_switchboard, b->pubkey))
		b->have_pub = 1;
	if ((f = fopen(b->clog, "r"))) {		/* the codes already handed out */
		while (fgets(line, sizeof line, f)) {
			char kh[80], code[16];
			uint8_t key[32];

			if (sscanf(line, "%79s %15s", kh, code) == 2 && strlen(code) < 10 && !lw_address_parse(kh, key) && !code_of(b, key))
				code_put(b, key, code, 0);
		}
		fclose(f);
	}
	if ((f = fopen(b->nlog, "r"))) {		/* the listed names, last line for a key wins */
		while (fgets(line, sizeof line, f)) {
			char kh[80], nh[2 * LW_NAME_MAX + 8], nm[LW_NAME_MAX + 1];
			uint8_t key[32];
			unsigned v, i;

			if (sscanf(line, "%79s %70s", kh, nh) != 2 || strlen(kh) != 64)
				continue;
			for (i = 0; i < 32; i++) {
				if (sscanf(kh + 2 * i, "%2x", &v) != 1)
					break;
				key[i] = (uint8_t)v;
			}
			if (i != 32)
				continue;
			nm[0] = 0;
			if (strcmp(nh, "-"))
				for (i = 0; nh[2 * i] && nh[2 * i + 1] && i < LW_NAME_MAX; i++) {
					if (sscanf(nh + 2 * i, "%2x", &v) != 1)
						break;
					nm[i] = (char)v;
					nm[i + 1] = 0;
				}
			bind_set(b, key, nm, 0);
		}
		fclose(f);
	}
	snprintf(path, sizeof path, "%s", b->log);
	if ((f = fopen(path, "r"))) {
		while (fgets(line, sizeof line, f)) {
			char num[16], kh[80], *bh;
			reg r;
			unsigned i;

			if (!strncmp(line, "seen ", 5) || !strncmp(line, "released ", 9)) {	/* later lines about a number above */
				char what[16], khex[80] = "";
				long when;
				reg *q;
				int m = sscanf(line, "%15s %15s %ld %79s", what, num, &when, khex);

				if (m < 3 || !(q = reg_by_number(b, num)))
					continue;
				if (what[0] == 's') {
					q->wall = q->wall_logged = when;
				} else {
					held_add(b, q, when);
					*q = b->regs[--b->n];
				}
				continue;
			}
			if (sscanf(line, "%15s %79s", num, kh) != 2 || strlen(num) != NUMLEN || !(bh = strrchr(line, ' ')))
				continue;
			bh++;
			if (strlen(bh) < LW_BUNDLE * 2)
				continue;
			memset(&r, 0, sizeof r);
			memcpy(r.number, num, NUMLEN + 1);
			for (i = 0; i < LW_BUNDLE; i++) {
				unsigned v;

				if (sscanf(bh + 2 * i, "%2x", &v) != 1)
					break;
				r.bundle[i] = (uint8_t)v;
			}
			if (i != LW_BUNDLE || r.bundle[0] != 1)
				continue;
			lw_h(r.key_id, r.bundle, LW_BUNDLE);
			r.wall = (long)time(0);		/* no record of when it was heard: counts from now */
			{
				struct held *h = held_by_number(b, r.number);

				if (h)				/* its owner came back while it was held */
					held_drop(b, h);
			}
			if (b->n == b->cap) {
				b->cap = b->cap ? b->cap * 2 : 64;
				b->regs = realloc(b->regs, (size_t)b->cap * sizeof *b->regs);
			}
			b->regs[b->n++] = r;
		}
		fclose(f);
	}
	if (n->cfg.registry[0] && strcmp(n->cfg.registry, "none") && sb_connect(n, n->cfg.registry, err, errn))
		return -1;
	return 0;
}

static void persist(struct board *b, const reg *r)
{
	FILE *f = fopen(b->log, "a");
	char *hex;
	char kh[65];

	if (!f)
		return;
	chmod(b->log, 0600);
	hex = malloc(LW_BUNDLE * 2 + 1);
	lw_hex(hex, r->bundle, LW_BUNDLE);
	lw_hex(kh, r->key_id, 32);
	fprintf(f, "%s %s %s\n", r->number, kh, hex);
	free(hex);
	fclose(f);
}

/* NUMBER: the number, what we saw, what we offer, and a code (the registry's, for a node: its switchboard's; for a switchboard: its own). */
static void send_number(lw_node *n, lw_peer *p, const reg *r, const char *code)
{
	uint8_t b[NUMLEN + 7 + 2 + 10];
	size_t o = NUMLEN + 7;
	lw_cand c;

	memcpy(b, r->number, NUMLEN);
	observed(p, &c);
	memset(b + NUMLEN, 0, 6);			/* a node reached over IPv6 has no IPv4 address to be told of */
	if (c.fam == 4) {
		memcpy(b + NUMLEN, c.ip, 4);
		put16(b + NUMLEN + 4, c.port);
	}
	b[NUMLEN + 6] = (uint8_t)((n->cfg.no_caller_id ? 0 : CAPS_CALLERID) | (n->cfg.no_directory ? 0 : CAPS_DIRECTORY));
	if (code[0])
		o += (size_t)tlv_put(b + o, TAG_CODE, code, strlen(code));
	ctl_msg_send(n, p, M_NUMBER, b, o);
}

/* A switchboard that registered with us, a registry: its code, in the message a node gets its number in. */
static void send_code(lw_node *n, lw_peer *p, const char *code)
{
	reg r;

	memset(&r, 0, sizeof r);
	memset(r.number, '0', NUMLEN);
	send_number(n, p, &r, code);
}

static void refuse(lw_node *n, lw_peer *p, const char *why)
{
	ctl_msg_send(n, p, M_REFUSED, (const uint8_t *)why, strlen(why));
}

static void do_register(lw_node *n, lw_peer *p, const char *reserved, const char *name, int listed)
{
	struct board *b = n->bd;
	reg r;
	const char *why;
	struct held *h;
	int tries;

	memset(&r, 0, sizeof r);
	memcpy(r.bundle, p->bundle, LW_BUNDLE);
	memcpy(r.key_id, p->key_id, 32);
	if (reserved) {				/* a token reserved this number: it is theirs, or the answer is no */
		if (strlen(reserved) != NUMLEN || !damm_ok(reserved, NUMLEN) || reg_by_number(b, reserved) ||
		    ((h = held_by_number(b, reserved)) && crypto_verify32(h->key_id, p->key_id))) {
			refuse(n, p, "the number reserved for this token is not available");
			return;
		}
		memcpy(r.number, reserved, NUMLEN + 1);
	} else if ((h = held_by_key(b, p->key_id))) {	/* released while away, and now back: the same number, if still held */
		memcpy(r.number, h->number, sizeof r.number);
		held_drop(b, h);
	} else
		for (tries = 0; tries < 20; tries++) {
			mint_number(r.number);
			if (!reg_by_number(b, r.number) && !held_by_number(b, r.number))
				break;
		}
	if (reg_by_number(b, r.number)) {
		refuse(n, p, "no number available");
		return;
	}
	if ((why = name_apply(b, p->key_id, name, listed, 1))) {	/* the script took a while: the name may have gone */
		refuse(n, p, why);
		return;
	}
	if ((h = held_by_number(b, r.number)))		/* the same key, restored (a stranger was refused or skipped above) */
		held_drop(b, h);
	r.seen = n->now;
	r.wall = r.wall_logged = (long)time(0);
	snprintf(r.name, sizeof r.name, "%s", name);
	r.listed = listed;
	if (b->n == b->cap) {
		b->cap = b->cap ? b->cap * 2 : 64;
		b->regs = realloc(b->regs, (size_t)b->cap * sizeof *b->regs);
	}
	b->regs[b->n++] = r;
	persist(b, &r);
	send_number(n, p, &r, n->home_code);
}

/* The number of whoever holds `key`, or NULL: a caller with several dials open is told which one was refused. */
static const char *num_of(struct board *b, const uint8_t key[32])
{
	reg *r = reg_by_key(b, key);

	return r ? r->number : 0;
}

static void rejected(lw_node *n, const uint8_t key[32], const uint8_t id[8], int why, const char *callee_number)
{
	lw_peer *p = peer_by_key(n, key);
	uint8_t b[9 + NUMLEN];

	if (!p || !p->cur.valid)
		return;
	if (id)
		memcpy(b, id, 8);
	else
		memset(b, 0, 8);
	b[8] = (uint8_t)why;
	if (callee_number)
		memcpy(b + 9, callee_number, NUMLEN);
	else
		memset(b + 9, '0', NUMLEN);
	ctl_msg_send(n, p, M_REJECTED, b, sizeof b);
}

static void route_dial(lw_node *n, const uint8_t caller_key[32], const uint8_t callee_key[32], lw_cand *cc, int ncc, int strict,
		       const uint8_t *admit, int alen)
{
	struct board *b = n->bd;
	reg *cr = reg_by_key(b, caller_key), *ce = reg_by_key(b, callee_key);
	lw_peer *cp = peer_by_key(n, caller_key), *ep = peer_by_key(n, callee_key);
	bcall *c = 0;
	uint8_t m[8 + NUMLEN + LW_BUNDLE + 1 + MAXCANDS * CANDWIRE + 2 + ADMIT_LEN + 2 + LW_NAME_MAX];
	int i, o;

	if (!cr || !ce || !cp || !ep || !online(n, ce)) {
		rejected(n, caller_key, 0, R_OFFLINE, num_of(b, callee_key));
		return;
	}
	for (i = 0; i < MAXCALLS; i++)
		if (!b->calls[i].used) {
			c = &b->calls[i];
			break;
		}
	if (!c) {
		rejected(n, caller_key, 0, R_BUSY, ce->number);
		return;
	}
	memset(c, 0, sizeof *c);
	c->used = 1;
	lw_rand(c->id, 8);
	memcpy(c->caller, caller_key, 32);
	memcpy(c->callee, callee_key, 32);
	memcpy(c->cc, cc, (size_t)ncc * sizeof *cc);
	c->ncc = ncc;
	c->strict = strict;
	observed(cp, &c->cc[c->ncc++]);			/* what we see is the best candidate there is */
	c->deadline = n->now + 65.0;
	memcpy(m, c->id, 8);
	memcpy(m + 8, cr->number, NUMLEN);
	memcpy(m + 8 + NUMLEN, cr->bundle, LW_BUNDLE);
	o = 8 + NUMLEN + LW_BUNDLE;
	o += cands_write(m + o, c->cc, c->ncc, c->strict);
	if (alen == ADMIT_LEN)				/* handed on exactly as it came */
		o += tlv_put(m + o, TAG_ADMIT, admit, ADMIT_LEN);
	if (cr->name[0] && !n->cfg.no_caller_id)
		o += tlv_put(m + o, TAG_NAME, cr->name, strlen(cr->name));
	ctl_msg_send(n, ep, M_RING, m, (size_t)o);
}

static pend *pend_new(struct board *b, int kind)
{
	int i;

	for (i = 0; i < MAXPEND; i++)
		if (!b->pend[i].used) {
			memset(&b->pend[i], 0, sizeof b->pend[i]);
			b->pend[i].used = 1;
			b->pend[i].kind = kind;
			b->pend[i].id = b->next_id++;
			return &b->pend[i];
		}
	return 0;
}

void board_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len)
{
	struct board *b = n->bd;
	reg *r = reg_by_key(b, p->key_id);
	char ep[64], nm_[LW_NAME_MAX + 1];
	const char *why_;
	int listed_ = 0;

	addr_str(&p->ep, ep, sizeof ep);
	switch (msg) {
	case M_REGISTER:
		if (len < 1 || (size_t)pl[0] + 1 > len)
			return;
		{					/* a switchboard asking for its code */
			size_t zl;
			const char *have;

			if (tlv_find(pl + 1 + pl[0], len - 1 - pl[0], TAG_SWITCHBOARD, &zl)) {
				if (!n->cfg.issue_codes) {
					refuse(n, p, "this switchboard does not issue codes");
					return;
				}
				if ((have = code_of(b, p->key_id))) {	/* the same key always gets the same code */
					send_code(n, p, have);
					return;
				}
				if (n->cfg.policy_script) {
					pend *q = pend_new(b, P_CODE);
					char kh[65];

					if (!q) {
						refuse(n, p, "switchboard busy");
						return;
					}
					memcpy(q->key, p->key_id, 32);
					q->deadline = n->now + 2.0;
					lw_hex(kh, p->key_id, 32);
					ev_emit(n, "{\"event\":\"register\",\"id\":%d,\"key_id\":\"%s\",\"ip\":\"%s\",\"token\":\"\",\"name\":\"\",\"listed\":false,\"switchboard\":true}",
						q->id, kh, ep);
				} else
					send_code(n, p, code_issue(b, p->key_id));
				return;
			}
		}
		{					/* optional parts after the token: a name, and whether to list it */
			size_t nl = 0, ll = 0;
			const uint8_t *nm = tlv_find(pl + 1 + pl[0], len - 1 - pl[0], TAG_NAME, &nl);

			nm_[0] = 0;
			if (nm && !name_ok(nm, nl)) {
				refuse(n, p, "the name is not acceptable (32 bytes of text, no control characters)");
				return;
			}
			if (nm && !n->cfg.no_caller_id) {	/* a switchboard that forbids caller ID just does not keep it */
				memcpy(nm_, nm, nl);
				nm_[nl] = 0;
			}
			listed_ = tlv_find(pl + 1 + pl[0], len - 1 - pl[0], TAG_LISTED, &ll) && !n->cfg.no_directory;
		}
		if ((why_ = name_apply(b, p->key_id, nm_, listed_, r != 0))) {
			refuse(n, p, why_);
			return;
		}
		if (r) {				/* the same key always gets the same number */
			snprintf(r->name, sizeof r->name, "%s", nm_);
			r->listed = listed_;
			heard(n, r);
			send_number(n, p, r, n->home_code);
			return;
		}
		if (n->cfg.policy_script) {
			pend *q = pend_new(b, P_REG);
			char kh[65], tok[65];

			if (!q) {
				refuse(n, p, "switchboard busy");
				return;
			}
			memcpy(q->key, p->key_id, 32);
			q->deadline = n->now + 2.0;
			snprintf(q->name, sizeof q->name, "%s", nm_);
			q->listed = listed_;
			lw_hex(kh, p->key_id, 32);
			snprintf(tok, sizeof tok, "%.*s", (int)pl[0] > 63 ? 63 : (int)pl[0], (const char *)pl + 1);
			{
				char en[2 * LW_NAME_MAX + 8];

				json_esc(en, sizeof en, nm_);
				ev_emit(n, "{\"event\":\"register\",\"id\":%d,\"key_id\":\"%s\",\"ip\":\"%s\",\"token\":\"%s\",\"name\":\"%s\",\"listed\":%s,\"switchboard\":false}",
					q->id, kh, ep, tok, en, listed_ ? "true" : "false");
			}
		} else
			do_register(n, p, 0, nm_, listed_);
		return;
	case M_HEARTBEAT:
		if (!r)
			return;
		heard(n, r);
		{
			uint8_t a[6];
			lw_cand c;

			observed(p, &c);
			memset(a, 0, 6);
			if (c.fam == 4) {
				memcpy(a, c.ip, 4);
				put16(a + 4, c.port);
			}
			ctl_msg_send(n, p, M_HBACK, a, 6);
		}
		return;
	case M_LOOKUP: {				/* the directory: only nodes that chose to be listed are found */
		uint8_t o[4 + LW_NAME_MAX + 1 + 8 * (NUMLEN + 32 + 1 + LW_NAME_MAX)];
		size_t k = 3, ql;
		int hits = 0, i;
		char q[LW_NAME_MAX + 1];

		if (!r || len < 2)
			return;
		r->look_tok += (n->now - r->look_t) * 1.0;		/* 1 a second, burst 5 */
		r->look_t = n->now;
		if (r->look_tok > 5.0)
			r->look_tok = 5.0;
		ql = len - 1;
		if (r->look_tok < 1.0) {			/* say so, so the node can tell the person */
			uint8_t lim[4] = { 2, pl[0] > 1 ? 1 : pl[0], 0, 0 };

			ctl_msg_send(n, p, M_FOUND, lim, sizeof lim);
			return;
		}
		r->look_tok -= 1.0;
		if ((pl[0] == 0 && ql != 32) || (pl[0] == 1 && (ql < 3 || ql > LW_NAME_MAX)) || pl[0] > 1)
			return;
		o[0] = n->cfg.no_directory ? 1 : 0;
		o[1] = pl[0];
		o[2] = (uint8_t)ql;
		memcpy(o + 3, pl + 1, ql);
		k = 3 + ql;
		o[k++] = 0;				/* the count, filled in below */
		if (!n->cfg.no_directory) {
			if (pl[0] == 1) {
				memcpy(q, pl + 1, ql);
				q[ql] = 0;
			}
			for (i = 0; i < b->n && hits < 8; i++) {
				const reg *e = &b->regs[i];
				size_t nl = strlen(e->name);

				if (!e->listed || !online(n, (reg *)e))
					continue;
				if (pl[0] == 0 ? memcmp(e->key_id, pl + 1, 32) != 0 : !nl || !strcasestr(e->name, q))
					continue;
				memcpy(o + k, e->number, NUMLEN);
				memcpy(o + k + NUMLEN, e->key_id, 32);
				o[k + NUMLEN + 32] = (uint8_t)nl;
				memcpy(o + k + NUMLEN + 33, e->name, nl);
				k += NUMLEN + 33 + nl;
				hits++;
			}
			o[3 + ql] = (uint8_t)hits;
		}
		ctl_msg_send(n, p, M_FOUND, o, k);
		return;
	}
	case M_DIAL: {
		lw_cand cc[MAXCANDS];
		int nc = 0, cn, alen = 0, strict = 0;
		char num[NUMLEN + 1];
		const uint8_t *admit = 0;
		reg *ce;

		if (!r || len < NUMLEN + 1)
			return;
		heard(n, r);
		memcpy(num, pl, NUMLEN);
		num[NUMLEN] = 0;
		if ((cn = cands_read(pl + NUMLEN, len - NUMLEN, cc, &nc, &strict)) < 0)
			return;
		{
			size_t al;

			if ((admit = tlv_find(pl + NUMLEN + cn, len - NUMLEN - (size_t)cn, TAG_ADMIT, &al)) && al == ADMIT_LEN)
				alen = ADMIT_LEN;		/* opaque to us */
			else
				admit = 0;
		}
		if (!(ce = reg_by_number(b, num))) {
			rejected(n, p->key_id, 0, R_UNKNOWN, num);
			return;
		}
		if (ce == r) {
			rejected(n, p->key_id, 0, R_SELF, num);
			return;
		}
		if (!online(n, ce)) {
			rejected(n, p->key_id, 0, R_OFFLINE, num);
			return;
		}
		if (n->cfg.policy_script) {
			pend *q = pend_new(b, P_DIAL);
			char f1[16], f2[16];

			if (!q) {
				rejected(n, p->key_id, 0, R_BUSY, num);
				return;
			}
			memcpy(q->key, p->key_id, 32);
			memcpy(q->callee, ce->key_id, 32);
			memcpy(q->cc, cc, (size_t)nc * sizeof *cc);
			q->ncc = nc;
			q->strict = strict;
			if (alen) {
				memcpy(q->admit, admit, ADMIT_LEN);
				q->alen = alen;
			}
			q->deadline = n->now + 2.0;
			fmt_number(f1, r->number);
			fmt_number(f2, ce->number);
			ev_emit(n, "{\"event\":\"dial\",\"id\":%d,\"from\":\"%s\",\"to\":\"%s\",\"ip\":\"%s\"}", q->id, f1, f2, ep);
		} else
			route_dial(n, p->key_id, ce->key_id, cc, nc, strict, admit, alen);
		return;
	}
	case M_ANSWER: {
		bcall *c = 0;
		lw_cand cc[MAXCANDS];
		int nc = 0, i, o;
		reg *cr, *ce;
		lw_peer *callerp, *calleep = p;
		uint8_t m[8 + 1 + 2 + NUMLEN + 32 + LW_BUNDLE + 1 + MAXCANDS * CANDWIRE], tok[32];

		if (len < 9)
			return;
		for (i = 0; i < MAXCALLS; i++)
			if (b->calls[i].used && !memcmp(b->calls[i].id, pl, 8) && !crypto_verify32(b->calls[i].callee, p->key_id))
				c = &b->calls[i];
		if (!c)
			return;
		if (pl[8] != V_ACCEPT) {
			rejected(n, c->caller, c->id, pl[8] == V_BUSY ? R_BUSY : R_DECLINED, num_of(b, c->callee));
			c->used = 0;
			return;
		}
		if (cands_read(pl + 9, len - 9, cc, &nc, &c->callee_strict) < 0)
			return;
		observed(p, &cc[nc++]);
		cr = reg_by_key(b, c->caller);
		ce = reg_by_key(b, c->callee);
		callerp = peer_by_key(n, c->caller);
		if (!cr || !ce || !callerp || !callerp->cur.valid) {
			c->used = 0;
			return;
		}
		lw_rand(tok, 32);
		/* to the caller: the callee's number, bundle and candidates; the caller initiates */
		memcpy(m, c->id, 8);
		m[8] = 1;
		put16(m + 9, 150);
		memcpy(m + 11, ce->number, NUMLEN);
		memcpy(m + 11 + NUMLEN, tok, 32);
		memcpy(m + 11 + NUMLEN + 32, ce->bundle, LW_BUNDLE);
		o = 11 + NUMLEN + 32 + LW_BUNDLE;
		o += cands_write(m + o, cc, nc, c->callee_strict);
		ctl_msg_send(n, callerp, M_GO, m, (size_t)o);
		/* to the callee: the caller's */
		m[8] = 0;
		memcpy(m + 11, cr->number, NUMLEN);
		memcpy(m + 11 + NUMLEN + 32, cr->bundle, LW_BUNDLE);
		o = 11 + NUMLEN + 32 + LW_BUNDLE;
		o += cands_write(m + o, c->cc, c->ncc, c->strict);
		ctl_msg_send(n, calleep, M_GO, m, (size_t)o);
		memset(c, 0, sizeof *c);		/* and the call is forgotten: nothing keeps who called whom */
		return;
	}
	}
}

void board_timers(lw_node *n)
{
	struct board *b = n->bd;
	int i;

	registration_timers(n);			/* with the registry of codes, if we have one */
	if (n->now >= b->sweep_at) {
		long iv = release_after(n) / 10;

		b->sweep_at = n->now + (iv < 1 ? 1 : iv > 3600 ? 3600 : iv);
		sweep(n);
	}

	for (i = 0; i < MAXCALLS; i++)
		if (b->calls[i].used && n->now > b->calls[i].deadline) {
			rejected(n, b->calls[i].caller, b->calls[i].id, R_TIMEOUT, num_of(b, b->calls[i].callee));
			memset(&b->calls[i], 0, sizeof b->calls[i]);
		}
	for (i = 0; i < MAXPEND; i++) {
		pend *q = &b->pend[i];

		if (!q->used || n->now < q->deadline)
			continue;			/* the script did not answer in time: refuse */
		if (q->kind == P_REG || q->kind == P_CODE) {
			lw_peer *p = peer_by_key(n, q->key);

			if (p && p->cur.valid)
				refuse(n, p, "no decision from the switchboard policy");
		} else
			rejected(n, q->key, 0, R_POLICY, num_of(b, q->callee));
		q->used = 0;
	}
}

/* reply ID accept [NUMBER]   |   reply ID refuse [REASON...] */
int board_cmd(lw_node *n, const char *line, char *out, size_t cap)
{
	struct board *b = n->bd;
	int id, i;
	char verdict[16] = "", arg[80] = "";

	if (sscanf(line, "reply %d %15s %79[^\n]", &id, verdict, arg) < 2)
		return snprintf(out, cap, "{\"error\":\"unknown request\"}\n");
	for (i = 0; i < MAXPEND; i++) {
		pend *q = &b->pend[i];

		if (!q->used || q->id != id)
			continue;
		q->used = 0;
		if (q->kind == P_CODE) {
			lw_peer *p = peer_by_key(n, q->key);

			if (!p || !p->cur.valid)
				return snprintf(out, cap, "{\"error\":\"the switchboard has gone\"}\n");
			if (!strcmp(verdict, "accept"))
				send_code(n, p, code_issue(b, q->key));
			else
				refuse(n, p, arg[0] ? arg : "code refused");
		} else if (q->kind == P_REG) {
			lw_peer *p = peer_by_key(n, q->key);

			if (!p || !p->cur.valid)
				return snprintf(out, cap, "{\"error\":\"the node has gone\"}\n");
			if (!strcmp(verdict, "accept"))
				do_register(n, p, arg[0] ? arg : 0, q->name, q->listed);
			else
				refuse(n, p, arg[0] ? arg : "registration refused");
		} else if (!strcmp(verdict, "accept"))
			route_dial(n, q->key, q->callee, q->cc, q->ncc, q->strict, q->admit, q->alen);
		else
			rejected(n, q->key, 0, R_POLICY, num_of(b, q->callee));
		return snprintf(out, cap, "{\"ok\":true}\n");
	}
	return snprintf(out, cap, "{\"error\":\"no such pending request (timed out?)\"}\n");
}

int board_status_json(lw_node *n, char *o, size_t cap)
{
	struct board *b = n->bd;
	char kid[65], addr[60];
	int i, online_n = 0, calls = 0;

	for (i = 0; i < b->n; i++)
		if (online(n, &b->regs[i]))
			online_n++;
	for (i = 0; i < MAXCALLS; i++)
		calls += b->calls[i].used;
	lw_hex(kid, n->id.key_id, 32);
	lw_address(addr, n->id.key_id);
	return snprintf(o, cap,
		"{\"node\":\"switchboard\",\"key_id\":\"%s\",\"address\":\"%s\",\"uptime\":%d,\"numbers\":%d,\"online\":%d,"
		"\"calls_ringing\":%d,\"policy\":\"%s\",\"drop_garbage\":%llu,\"code\":\"%s\",\"code_refused\":\"%s\",\"codes_issued\":%d}\n",
		kid, addr, (int)(n->now - n->started), b->n, online_n, calls,
		n->cfg.policy_script ? "script" : "open", (unsigned long long)n->drop_garbage, n->home_code, n->reg_why, b->ncodes);
}
