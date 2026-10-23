/* SPDX-License-Identifier: GPL-3.0-or-later
 * The node's side of M1: numbers, HELLO, candidates, pins, calls, punching.
 * See docs/SWITCHBOARD.md and docs/PROTOCOL.md §4. Policy (who may call me,
 * pick up or not) is the node script's; this is mechanism, plus the pin store,
 * which stays in C because it is a security check. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "int.h"
#include "words.h"

/* ---- numbers: 8 random digits + a Damm check digit ------------------------ */

static const uint8_t DAMM[10][10] = {
	{0,3,1,7,5,9,8,6,4,2}, {7,0,9,2,1,5,4,8,6,3}, {4,2,0,6,8,7,1,3,5,9}, {1,7,5,0,9,8,3,4,2,6},
	{6,1,2,3,0,4,5,9,7,8}, {3,6,7,4,2,0,9,5,8,1}, {5,8,6,9,7,2,0,1,3,4}, {8,9,4,5,3,6,2,0,1,7},
	{9,4,3,8,6,1,7,2,0,5}, {2,5,8,1,4,3,6,7,9,0}
};

int damm_ok(const char *d, int n)
{
	int i, x = 0;

	for (i = 0; i < n; i++) {
		if (d[i] < '0' || d[i] > '9')
			return 0;
		x = DAMM[x][d[i] - '0'];
	}
	return x == 0;
}

void mint_number(char out[NUMLEN + 1])
{
	int i, x = 0;
	uint8_t b;

	for (i = 0; i < NUMLEN - 1; i++) {
		do
			lw_rand(&b, 1);
		while (b >= 250);
		out[i] = (char)('0' + b % 10);
		if (i == 0 && out[0] == '0')
			out[0] = '1' + (char)(b % 9);
		x = DAMM[x][out[i] - '0'];
	}
	out[NUMLEN - 1] = (char)('0' + x);	/* DAMM[x][x] == 0, so the whole number checks to 0 */
	out[NUMLEN] = 0;
}

void fmt_number(char out[16], const char *n9)
{
	snprintf(out, 16, "%.3s-%.3s-%.3s", n9, n9 + 3, n9 + 6);
}

int parse_number(const char *in, char out[NUMLEN + 1])
{
	int n = 0;

	for (; *in; in++) {
		if (*in == '-' || *in == ' ')
			continue;
		if (*in < '0' || *in > '9' || n >= NUMLEN)
			return -1;
		out[n++] = *in;
	}
	out[n] = 0;
	return n == NUMLEN && damm_ok(out, NUMLEN) ? 0 : -1;
}

/* ---- full numbers: a switchboard's code, then a local number (ADR 0024) ----------------- */

void home_kid(const lw_node *n, uint8_t out[32])
{
	const char *slash = n->cfg.switchboard[0] ? strrchr(n->cfg.switchboard, '/') : 0;

	memset(out, 0, 32);
	if (slash)
		lw_address_parse(slash + 1, out);
}

void sb_code(const uint8_t kid[32], char out[9])
{
	uint8_t h[32];
	uint32_t v;

	lw_h2(h, "sbcode--", 8, kid, 32);
	v = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
	snprintf(out, 8, "0%06u", v % 1000000u);	/* a leading 0 is a derived code: no assigned code ever begins with 0 */
}

void full_number(const lw_node *n, const char *local, char *out, size_t cap)
{
	uint8_t k[32];
	char code[9], f[16];

	home_kid(n, k);
	fmt_number(f, local);
	if (n->cfg.switchboard[0]) {
		if (n->home_code[0])			/* the registry's short code is the one to show */
			snprintf(code, sizeof code, "%s", n->home_code);
		else
			sb_code(k, code);
		snprintf(out, cap, "+%s %s", code, f);
	} else
		snprintf(out, cap, "%s", f);
}

/* A number as a person types it: the local part alone (our own switchboard's), or "+CODE local". Codes form a
 * prefix-free plan (ADR 0024), so the separator is optional: our own code is matched as a prefix of the digits. */
int parse_full(const lw_node *n, const char *in, char out[NUMLEN + 1], char *err, size_t errn)
{
	while (*in == ' ')
		in++;
	if (*in == '+') {
		char mine[9], d[24];
		uint8_t k[32];
		size_t l = 0, ml;

		for (in++; *in && l < sizeof d - 1; in++)
			if (*in >= '0' && *in <= '9')
				d[l++] = *in;
			else if (*in != ' ' && *in != '-') {
				snprintf(err, errn, "not a valid number: a full number is + code, then 9 digits");
				return -1;
			}
		d[l] = 0;
		home_kid(n, k);
		sb_code(k, mine);
		ml = strlen(mine);
		if (n->home_code[0] && l >= strlen(n->home_code) && !strncmp(d, n->home_code, strlen(n->home_code)))
			ml = strlen(n->home_code);		/* our assigned code: it cannot also start with 0, so it is not the derived one */
		else if (l >= ml && !strncmp(d, mine, ml))
			;
		else
			ml = 0;
		if (!n->cfg.switchboard[0] || !ml) {
			snprintf(err, errn, "that number is on another switchboard, or its code is mistyped (yours is +%s); calling across switchboards is planned (M9)", mine);
			return -1;
		}
		if (parse_number(d + ml, out)) {
			snprintf(err, errn, "not a valid number (9 digits with a check digit)");
			return -1;
		}
		return 0;
	}
	if (parse_number(in, out)) {
		snprintf(err, errn, "not a valid number (9 digits with a check digit)");
		return -1;
	}
	return 0;
}

/* Six words from the two key ids, the same on both screens (docs/CLI.md). */
void phrase_of(char *out, size_t cap, const uint8_t a[32], const uint8_t b[32])
{
	uint8_t h[32];
	int i, o = 0;

	lw_h2(h, a, 32, b, 32);
	out[0] = 0;
	for (i = 0; i < 6; i++)
		o += snprintf(out + o, cap - (size_t)o, "%s%s", i ? " " : "", LW_WORDS[h[i]]);
}

void addr_str(const struct sockaddr_storage *ss, char *out, size_t n)
{
	const struct sockaddr_in *a = (const void *)ss;
	const uint8_t *b = (const uint8_t *)&a->sin_addr;
	const uint8_t *pt = (const uint8_t *)&a->sin_port;

	if (ss->ss_family != AF_INET) {
		snprintf(out, n, "-");
		return;
	}
	snprintf(out, n, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], (unsigned)(pt[0] << 8 | pt[1]));
}

int lw_resolve(const char *hp, struct sockaddr_storage *ss, socklen_t *sl)
{
	char host[96], *c = strrchr(hp, ':');
	struct addrinfo hint, *res;

	if (!c || c == hp || (size_t)(c - hp) >= sizeof host)
		return -1;
	snprintf(host, sizeof host, "%.*s", (int)(c - hp), hp);
	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host, c + 1, &hint, &res))
		return -1;
	memcpy(ss, res->ai_addr, res->ai_addrlen);
	*sl = res->ai_addrlen;
	freeaddrinfo(res);
	return 0;
}

/* ---- candidates: where the other side may reach us --------------------------- */

static int gather(lw_node *n, lw_cand *out, int max)
{
	struct ifaddrs *ifa, *i;
	int c = 0;

	if (getifaddrs(&ifa))
		return 0;
	for (i = ifa; i && c < max - 1; i = i->ifa_next) {
		const struct sockaddr_in *a;
		const uint8_t *ip;

		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP))
			continue;
		if ((i->ifa_flags & IFF_LOOPBACK) && !n->cfg.cand_loopback)
			continue;
		a = (const void *)i->ifa_addr;
		ip = (const uint8_t *)&a->sin_addr;
		if (ip[0] == 169 && ip[1] == 254)
			continue;
		memcpy(out[c].ip, ip, 4);
		out[c].port = n->udp_port;
		out[c].type = CAND_HOST;
		c++;
	}
	freeifaddrs(ifa);
	if (n->have_refl && c < max)
		out[c++] = n->refl;
	return c;
}

static int cands_put(uint8_t *p, const lw_cand *c, int nc)
{
	int i, o = 1;

	p[0] = (uint8_t)nc;
	for (i = 0; i < nc; i++)
		o += cand_put(p + o, &c[i]);
	return o;
}

/* returns bytes consumed, or -1 */
static int cands_get(const uint8_t *p, size_t len, lw_cand *c, int *nc)
{
	int i, k;

	if (len < 1)
		return -1;
	k = p[0];
	if (k > MAXCANDS || len < 1 + (size_t)k * 7)
		return -1;
	for (i = 0; i < k; i++)
		cand_get(p + 1 + i * 7, &c[i]);
	*nc = k;
	return 1 + k * 7;
}

/* ---- HELLO: fetch a bundle when only a key id is known ----------------------- */

void hello_request(lw_node *n, lw_peer *p)
{
	uint8_t b[10];

	b[0] = T_HELLO;
	b[1] = 0;
	memcpy(b + 2, p->key_id, 8);
	sendto(n->udp, b, sizeof b, 0, (struct sockaddr *)&p->ep, p->eplen);
}

void hello_offer(lw_node *n, const struct sockaddr *dst, socklen_t dl)
{
	uint8_t b[2 + LW_BUNDLE];

	b[0] = T_HELLO;
	b[1] = 2;
	memcpy(b + 2, n->id.bundle, LW_BUNDLE);
	sendto(n->udp, b, sizeof b, 0, dst, dl);
}

void hello_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl)
{
	uint8_t kid[32];
	int i;

	if (len < 2)
		return;
	if (pkt[1] == 0 && len == 10) {			/* somebody wants our bundle */
		uint8_t b[2 + LW_BUNDLE];

		n->hello_tokens += (n->now - n->hello_t) * 20.0;	/* 20/s, burst 20, for everyone */
		if (n->hello_tokens > 20.0)
			n->hello_tokens = 20.0;
		n->hello_t = n->now;
		if (n->hello_tokens < 1.0 || memcmp(pkt + 2, n->id.key_id, 8))
			return;
		{						/* 2/s, burst 4, for each source: one host cannot use up everyone's share */
			int k, free_k = 0;

			for (k = 0; k < 16; k++) {
				if (n->hello_src[k].l == sl && !memcmp(&n->hello_src[k].a, src, sl))
					break;
				if (n->hello_src[k].t < n->hello_src[free_k].t)
					free_k = k;
			}
			if (k == 16) {			/* new asker: take the stalest slot */
				k = free_k;
				memset(&n->hello_src[k], 0, sizeof n->hello_src[k]);
				memcpy(&n->hello_src[k].a, src, sl);
				n->hello_src[k].l = sl;
				n->hello_src[k].tok = 4.0;
			} else {
				n->hello_src[k].tok += (n->now - n->hello_src[k].t) * 2.0;
				if (n->hello_src[k].tok > 4.0)
					n->hello_src[k].tok = 4.0;
			}
			n->hello_src[k].t = n->now;
			if (n->hello_src[k].tok < 1.0)
				return;
			n->hello_src[k].tok -= 1.0;
		}
		n->hello_tokens -= 1.0;
		b[0] = T_HELLO;
		b[1] = 1;
		memcpy(b + 2, n->id.bundle, LW_BUNDLE);
		sendto(n->udp, b, sizeof b, 0, src, sl);
	} else if (pkt[1] == 1 && len == 2 + LW_BUNDLE && pkt[2] == 0x01) {	/* a bundle: believe it only if it hashes to the key id we hold */
		lw_h(kid, pkt + 2, LW_BUNDLE);
		for (i = 0; i < n->maxpeers; i++) {
			lw_peer *p = &n->peers[i];

			if (p->used && !p->have_bundle && !crypto_verify32(p->key_id, kid)) {
				memcpy(p->bundle, pkt + 2, LW_BUNDLE);
				p->have_bundle = 1;
				return;
			}
		}
	} else if (pkt[1] == 2 && len == 2 + LW_BUNDLE && pkt[2] == 0x01 && n->role) {	/* an introduction */
		lw_offer *o = &n->offers[0];

		lw_h(kid, pkt + 2, LW_BUNDLE);
		for (i = 0; i < OFFERS; i++) {
			if (n->offers[i].used && !crypto_verify32(n->offers[i].key_id, kid)) {
				o = &n->offers[i];
				break;
			}
			if (!n->offers[i].used || n->offers[i].t < o->t)
				o = &n->offers[i];
		}
		memcpy(o->bundle, pkt + 2, LW_BUNDLE);
		memcpy(o->key_id, kid, 32);
		o->t = n->now;
		o->used = 1;
	}
}

/* ---- pins: a number is bound to the first key that answered to it ------------- */

static void pins_load(lw_node *n)
{
	char path[300], line[160];
	FILE *f;

	snprintf(path, sizeof path, "%s/known", n->cfg.state_dir);
	if (!(f = fopen(path, "r")))
		return;
	while (fgets(line, sizeof line, f)) {
		char t1[80], t2[80], t3[80], *num, *hex;
		int got;
		pin p;

		memset(&p, 0, sizeof p);
		got = sscanf(line, "%79s %79s %79s", t1, t2, t3);
		if (got == 3) {						/* switchboard key id, number, key id */
			if (lw_address_parse(t1, p.sb))
				continue;
			num = t2;
			hex = t3;
		} else if (got == 2) {					/* the first format: no switchboard named */
			home_kid(n, p.sb);
			num = t1;
			hex = t2;
		} else
			continue;
		if (strlen(num) != NUMLEN || lw_address_parse(hex, p.key_id))
			continue;
		memcpy(p.number, num, NUMLEN + 1);
		if (n->npins == n->cappins) {
			n->cappins = n->cappins ? n->cappins * 2 : 16;
			n->pins = realloc(n->pins, (size_t)n->cappins * sizeof *n->pins);
		}
		n->pins[n->npins++] = p;
	}
	fclose(f);
}

/* Is pin i for a number on our own switchboard? (Pins for other switchboards wait for federation.) */
static int pin_home(const lw_node *n, int i)
{
	uint8_t h[32];

	home_kid(n, h);
	return !memcmp(n->pins[i].sb, h, 32);
}

static int pin_check(lw_node *n, const char *num, const uint8_t kid[32])
{
	int i;

	for (i = 0; i < n->npins; i++)
		if (pin_home(n, i) && !strcmp(n->pins[i].number, num))
			return crypto_verify32(n->pins[i].key_id, kid) ? -1 : 1;
	return 0;
}

static void pin_add(lw_node *n, const char *num, const uint8_t kid[32])
{
	char path[300], hex[65];
	FILE *f;

	if (pin_check(n, num, kid))
		return;
	if (n->npins == n->cappins) {
		n->cappins = n->cappins ? n->cappins * 2 : 16;
		n->pins = realloc(n->pins, (size_t)n->cappins * sizeof *n->pins);
	}
	memcpy(n->pins[n->npins].number, num, NUMLEN + 1);
	memcpy(n->pins[n->npins].key_id, kid, 32);
	home_kid(n, n->pins[n->npins].sb);
	n->npins++;
	snprintf(path, sizeof path, "%s/known", n->cfg.state_dir);
	lw_hex(hex, kid, 32);
	if ((f = fopen(path, "a"))) {
		char sbh[65];

		lw_hex(sbh, n->pins[n->npins - 1].sb, 32);
		chmod(path, 0600);
		fprintf(f, "%s %s %s\n", sbh, num, hex);
		fclose(f);
	}
}

static int pin_remove(lw_node *n, const char *num)
{
	char path[300], hex[65];
	FILE *f;
	int i, j, found = 0;

	for (i = j = 0; i < n->npins; i++) {
		if (pin_home(n, i) && !strcmp(n->pins[i].number, num)) {
			found = 1;
			continue;
		}
		n->pins[j++] = n->pins[i];
	}
	n->npins = j;
	if (!found)
		return 0;
	snprintf(path, sizeof path, "%s/known", n->cfg.state_dir);
	if ((f = fopen(path, "w"))) {
		chmod(path, 0600);
		for (i = 0; i < n->npins; i++) {
			char sbh[65];

			lw_hex(hex, n->pins[i].key_id, 32);
			lw_hex(sbh, n->pins[i].sb, 32);
			fprintf(f, "%s %s %s\n", sbh, n->pins[i].number, hex);
		}
		fclose(f);
	}
	return 1;
}

/* Register with a switchboard given as HOST:PORT/KEY (for a switchboard worker: the registry of codes). */
int sb_connect(lw_node *n, const char *spec, char *err, size_t errn)
{
	char hp[200], *slash = strrchr(spec, '/');
	uint8_t kid[32];
	lw_peer *p;

	if (!slash || (size_t)(slash - spec) >= sizeof hp) {
		snprintf(err, errn, "%s wants HOST:PORT/KEYID (64 hex or lw1...)", n->role ? "--registry" : "--switchboard");
		return -1;
	}
	snprintf(hp, sizeof hp, "%.*s", (int)(slash - spec), spec);
	if (lw_address_parse(slash + 1, kid)) {
		snprintf(err, errn, "bad switchboard key id '%s'", slash + 1);
		return -1;
	}
	if (!(p = peer_add_keyid(n, kid))) {
		snprintf(err, errn, "peer table full");
		return -1;
	}
	if (lw_resolve(hp, &p->ep, &p->eplen)) {
		snprintf(err, errn, "cannot resolve %s", hp);
		return -1;
	}
	p->dial = 1;
	p->is_sb = 1;
	n->sb = (int)(p - n->peers);
	return 0;
}

/* ---- setup ------------------------------------------------------------------- */

int node_init(lw_node *n, char *err, size_t errn)
{
	if (n->cfg.name[0] && !name_ok((const uint8_t *)n->cfg.name, strlen(n->cfg.name))) {
		snprintf(err, errn, "--name must be at most %d bytes of text with no control characters", LW_NAME_MAX);
		return -1;
	}
	n->sb = -1;
	n->ncalls = n->maxpeers;
	n->calls = calloc((size_t)n->ncalls, sizeof *n->calls);
	if (!n->calls) {
		snprintf(err, errn, "out of memory");
		return -1;
	}
	line_init(n);
	pins_load(n);
	if (n->cfg.switchboard[0] && sb_connect(n, n->cfg.switchboard, err, errn))
		return -1;
	return 0;
}

/* ---- events and calls -------------------------------------------------------- */

static const char *reason_str(int r)
{
	switch (r) {
	case R_UNKNOWN: return "unknown number";
	case R_OFFLINE: return "not online";
	case R_DECLINED: return "declined";
	case R_BUSY: return "busy";
	case R_POLICY: return "refused by the switchboard";
	case R_TIMEOUT: return "no answer";
	case R_SELF: return "that is your own number";
	}
	return "failed";
}

static lw_peer *sbp(lw_node *n)
{
	return n->sb >= 0 && n->peers[n->sb].cur.valid ? &n->peers[n->sb] : 0;
}

/* ---- the call table: a line is many calls at once ------------------------------ */

static lw_call *call_by_id(lw_node *n, const uint8_t id[8])
{
	int i;

	for (i = 0; i < n->ncalls; i++)
		if (n->calls[i].state != CALL_IDLE && !memcmp(n->calls[i].id, id, 8))
			return &n->calls[i];
	return 0;
}

static lw_call *call_out_for(lw_node *n, const char *num)
{
	int i;

	for (i = 0; i < n->ncalls; i++)
		if (n->calls[i].state == CALL_OUT && !strcmp(n->calls[i].number, num))
			return &n->calls[i];
	return 0;
}

static lw_call *call_slot(lw_node *n)
{
	int i;

	for (i = 0; i < n->ncalls; i++)
		if (n->calls[i].state == CALL_IDLE) {
			memset(&n->calls[i], 0, sizeof n->calls[i]);
			n->calls[i].peer = -1;
			return &n->calls[i];
		}
	return 0;
}

int call_for_number(lw_node *n, const char *num)
{
	int i;

	for (i = 0; i < n->ncalls; i++)
		if (n->calls[i].state != CALL_IDLE && !strcmp(n->calls[i].number, num))
			return 1;
	return 0;
}

static void call_end(lw_call *c)
{
	memset(c, 0, sizeof *c);
	c->peer = -1;
}

/* How many calls a person is being asked about or waiting on: an admitted mesh call never counts. */
static int interactive(lw_node *n)
{
	int i, c = 0;

	for (i = 0; i < n->ncalls; i++) {
		const lw_call *k = &n->calls[i];

		if ((k->state == CALL_OUT && !k->auto_) || (k->state == CALL_RINGING && !k->admitted))
			c++;
	}
	return c;
}

static void send_answer(lw_node *n, lw_call *c, int verdict)
{
	lw_peer *s = sbp(n);
	uint8_t b[8 + 1 + 1 + MAXCANDS * 7];
	lw_cand cc[MAXCANDS];
	int nc;

	if (!s)
		return;
	memcpy(b, c->id, 8);
	b[8] = (uint8_t)verdict;
	nc = verdict == V_ACCEPT ? gather(n, cc, MAXCANDS) : 0;
	ctl_msg_send(n, s, M_ANSWER, b, 9 + (size_t)cands_put(b + 9, cc, nc));
}

/* Start a call: the interactive kind a person asked for, or a mesh call to a fellow member. */
int call_dial(lw_node *n, const char *num, int auto_, char *err, size_t errn)
{
	lw_peer *s = sbp(n);
	uint8_t b[NUMLEN + 1 + MAXCANDS * 7 + 2 + ADMIT_LEN];
	lw_cand cc[MAXCANDS];
	uint8_t admit[ADMIT_LEN];
	lw_call *c;
	size_t o;
	int nc;

	if (!s || !n->registered) {
		snprintf(err, errn, "no dial tone: not registered with a switchboard");
		return -1;
	}
	if (!strcmp(num, n->number)) {
		snprintf(err, errn, "%s", reason_str(R_SELF));
		return -1;
	}
	if (call_for_number(n, num)) {
		snprintf(err, errn, "already connected to, or calling, that number");
		return -1;
	}
	if (!auto_ && interactive(n)) {
		snprintf(err, errn, "already dialing or ringing: answer or hang up first");
		return -1;
	}
	if (!(c = call_slot(n))) {
		snprintf(err, errn, "too many calls");
		return -1;
	}
	c->state = CALL_OUT;
	c->we_caller = 1;
	c->auto_ = auto_;
	memcpy(c->number, num, NUMLEN + 1);
	c->deadline = n->now + 65.0;			/* outlast the callee's 60 s of ringing */
	memcpy(b, num, NUMLEN);
	nc = gather(n, cc, MAXCANDS);
	o = NUMLEN + (size_t)cands_put(b + NUMLEN, cc, nc);
	if (line_admit_for(n, admit))			/* present our admit: a member we dial will not ask about us */
		o += (size_t)tlv_put(b + o, TAG_ADMIT, admit, ADMIT_LEN);
	ctl_msg_send(n, s, M_DIAL, b, o);
	return 0;
}

static void punch_send(lw_node *n, lw_call *c)
{
	uint8_t pk[33];
	int i;

	pk[0] = T_PUNCH;
	memcpy(pk + 1, c->id, 8);
	memcpy(pk + 9, n->id.key_id, 8);
	crypto_blake2b_keyed(pk + 17, 16, c->token, 32, pk, 17);
	for (i = 0; i < c->npc; i++) {
		struct sockaddr_in a;

		memset(&a, 0, sizeof a);
		a.sin_family = AF_INET;
		memcpy(&a.sin_addr, c->pc[i].ip, 4);
		a.sin_port = htons(c->pc[i].port);
		sendto(n->udp, pk, sizeof pk, 0, (struct sockaddr *)&a, sizeof a);
	}
	if (c->have_verified)		/* answer from where it really came from */
		sendto(n->udp, pk, sizeof pk, 0, (struct sockaddr *)&c->verified, sizeof c->verified);
}

void punch_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl)
{
	lw_call *c;
	uint8_t mac[16];
	lw_peer *p;

	if (len != 33 || sl != sizeof c->verified || !(c = call_by_id(n, pkt + 1)) || c->state != CALL_PUNCH || c->peer < 0)
		return;
	crypto_blake2b_keyed(mac, 16, c->token, 32, pkt, 17);
	if (crypto_verify16(mac, pkt + 17))
		return;
	p = &n->peers[c->peer];
	if (memcmp(pkt + 9, p->key_id, 8))		/* must be the other end, not an echo of our own */
		return;
	if (c->have_verified)
		return;
	memcpy(&c->verified, src, sl);
	c->have_verified = 1;
	if (p->relayed) {			/* a try for a better path: the session is up, so just aim some copies at this address */
		memcpy(&p->probe_ep, src, sl);
		p->probe_len = sl;
		p->probe_until = n->now + 10.0;
		p->probe_next = 0;
		punch_send(n, c);
		call_end(c);
		return;
	}
	memcpy(&p->ep, src, sl);
	p->eplen = sl;
	punch_send(n, c);			/* open the way back at once */
	if (c->we_caller) {			/* the caller makes the handshake */
		p->dial = 1;
		p->last_hs_try = 0;
		c->state = CALL_SETUP;
	}
}

/* End every call on a peer and drop it. */
void call_drop_peer(lw_node *n, lw_peer *p, const char *why)
{
	int i, idx = (int)(p - n->peers);

	(void)why;
	for (i = 0; i < n->ncalls; i++)
		if (n->calls[i].state != CALL_IDLE && n->calls[i].peer == idx)
			call_end(&n->calls[i]);
	peer_drop(n, p);
}

void call_drop_all(lw_node *n, const char *why)
{
	int i;

	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !n->peers[i].is_sb)
			call_drop_peer(n, &n->peers[i], why);
	for (i = 0; i < n->ncalls; i++)
		call_end(&n->calls[i]);
}

lw_peer *peer_by_number(lw_node *n, const char *num)
{
	int i;

	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !n->peers[i].is_sb && !strcmp(n->peers[i].number, num))
			return &n->peers[i];
	return 0;
}

/* A call is up: the two ends find out whether either holds a line. */
void call_peer_up(lw_node *n, lw_peer *p, int dialer)
{
	p->dialer = dialer;
	line_call_up(n, p);
}

void node_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len)
{
	if (n->sb >= 0 && p == &n->peers[n->sb]) {
		switch (msg) {
		case M_NUMBER:
			if (len >= NUMLEN + 6) {
				int news = !n->registered || memcmp(n->number, pl, NUMLEN);	/* re-registering on a rekey isn't news */

				memcpy(n->number, pl, NUMLEN);
				n->number[NUMLEN] = 0;
				memcpy(n->refl.ip, pl + NUMLEN, 4);
				n->refl.port = (uint16_t)get16(pl + NUMLEN + 4);
				n->refl.type = CAND_REFL;
				n->have_refl = 1;
				n->registered = 1;
				n->reg_why[0] = 0;
				n->reg_born = p->cur.born;
				n->reg_count++;
				n->sb_caps = len >= NUMLEN + 7 ? pl[NUMLEN + 6] : 0;
				{
					size_t cl;
					const uint8_t *cd = len > NUMLEN + 7 ? tlv_find(pl + NUMLEN + 7, len - NUMLEN - 7, TAG_CODE, &cl) : 0;
					char code[10];

					if (cd && cl >= 1 && cl <= 9) {
						size_t k;

						for (k = 0; k < cl && cd[k] >= '0' && cd[k] <= '9'; k++)
							code[k] = (char)cd[k];
						code[k] = 0;
						if (k == cl && strcmp(code, n->home_code)) {
							memcpy(n->home_code, code, sizeof n->home_code);
							if (n->role)
								ev_emit(n, "{\"event\":\"code-assigned\",\"code\":\"%s\"}", code);
						}
					}
				}
				if (news && !n->role) {
					char f[16];

					fmt_number(f, n->number);
					char fl[40];

					full_number(n, n->number, fl, sizeof fl);
					ev_emit(n, "{\"event\":\"registered\",\"number\":\"%s\",\"full\":\"%s\"}", f, fl);
				}
			}
			return;
		case M_FOUND: {				/* the directory's answer to a LOOKUP */
			uint8_t kind;
			size_t ql, o;
			int cnt, i, rl = 0, dir, limited;
			char q[2 * LW_NAME_MAX + 80], res[2200], first[NUMLEN + 1] = "";

			if (len < 4)
				return;
			dir = pl[0] != 1;
			limited = pl[0] == 2;
			kind = pl[1];
			ql = pl[2];
			if (len < 4 + ql || kind > 1 || ql > 64)
				return;
			cnt = pl[3 + ql];
			o = 4 + ql;
			if (kind == 1) {
				char raw[LW_NAME_MAX + 1] = "";

				memcpy(raw, pl + 3, ql > LW_NAME_MAX ? LW_NAME_MAX : ql);
				json_esc(q, sizeof q, raw);
			} else
				lw_hex(q, pl + 3, 32);
			res[0] = 0;
			for (i = 0; i < cnt && i < 8; i++) {
				char num[NUMLEN + 1], f[16], kh[65], nm[LW_NAME_MAX + 1] = "", en[2 * LW_NAME_MAX + 8];
				size_t nl;

				if (o + NUMLEN + 33 > len)
					break;
				nl = pl[o + NUMLEN + 32];
				if (o + NUMLEN + 33 + nl > len || !name_ok(pl + o + NUMLEN + 33, nl))
					break;
				memcpy(nm, pl + o + NUMLEN + 33, nl);
				memcpy(num, pl + o, NUMLEN);
				num[NUMLEN] = 0;
				if (!i)
					memcpy(first, num, sizeof first);
				fmt_number(f, num);
				lw_hex(kh, pl + o + NUMLEN, 32);
				json_esc(en, sizeof en, nm);
				rl += snprintf(res + rl, sizeof res - (size_t)rl, "%s{\"number\":\"%s\",\"key_id\":\"%s\",\"name\":\"%s\"}",
					       i ? "," : "", f, kh, en);
				o += NUMLEN + 33 + nl;
			}
			ev_emit(n, "{\"event\":\"found\",\"query\":\"%s\",\"directory\":%s,\"limited\":%s,\"results\":[%s]}", q, dir ? "true" : "false",
				limited ? "true" : "false", res);
			if (limited && n->look.active) {
				n->look.active = 0;
				ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"too many directory lookups: wait a moment\"}");
			} else if (n->look.active && kind == 0 && n->look.kind == 0 && ql == 32 && !memcmp(n->look.key, pl + 3, 32)) {
				char err[100];

				n->look.active = 0;
				if (!dir)
					ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"this switchboard has no directory\"}");
				else if (!first[0])
					ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"that address is not listed in the directory\"}");
				else if (call_dial(n, first, 0, err, sizeof err))
					ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"%s\"}", err);
			}
			return;
		}
		case M_HBACK:
			if (len >= 6) {
				memcpy(n->refl.ip, pl, 4);
				n->refl.port = (uint16_t)get16(pl + 4);
				n->refl.type = CAND_REFL;
				n->have_refl = 1;
			}
			return;
		case M_REFUSED: {
			char r[80];

			snprintf(r, sizeof r, "%.*s", (int)(len > 70 ? 70 : len), (const char *)pl);
			n->reg_refused = p->cur.born;	/* don't hammer: try again on the next session */
			snprintf(n->reg_why, sizeof n->reg_why, "%s", r);
			ev_emit(n, "{\"event\":\"register-refused\",\"reason\":\"%s\"}", r);
			return;
		}
		case M_RING: {
			int nc = 0, pc, cn, admitted = 0, i;
			char f[16], ph[120];
			uint8_t kid[32], reply[9];
			lw_cand cc[MAXCANDS];
			char num[NUMLEN + 1];
			const uint8_t *admit = 0;
			size_t base = 8 + NUMLEN + LW_BUNDLE;
			lw_call *c, *mine;

			if (len < base + 1)
				return;
			memcpy(num, pl + 8, NUMLEN);
			num[NUMLEN] = 0;
			memcpy(reply, pl, 8);
			if ((cn = cands_get(pl + base, len - base, cc, &nc)) < 0)
				return;
			{
				size_t al;

				if ((admit = tlv_find(pl + base + cn, len - base - (size_t)cn, TAG_ADMIT, &al)) && al != ADMIT_LEN)
					admit = 0;
			}
			lw_h(kid, pl + 8 + NUMLEN, LW_BUNDLE);
			fmt_number(f, num);
			if (line_evicted(n, kid)) {		/* it was put off the line: it stays off */
				reply[8] = V_REJECT;
				ctl_msg_send(n, p, M_ANSWER, reply, 9);
				ev_emit(n, "{\"event\":\"call-failed\",\"number\":\"%s\",\"reason\":\"refused a call from a node evicted from the line\"}", f);
				return;
			}
			pc = pin_check(n, num, kid);
			if (pc < 0) {			/* the number answered with a different key than before */
				reply[8] = V_REJECT;
				ctl_msg_send(n, p, M_ANSWER, reply, 9);
				ev_emit(n, "{\"event\":\"key-changed\",\"number\":\"%s\",\"action\":\"refused the call\"}", f);
				return;
			}
			admitted = line_admit_ok(n, admit, kid);
			if ((mine = call_out_for(n, num))) {	/* glare: we are dialing each other. The lower key id's call stands */
				if (memcmp(n->id.key_id, kid, 32) < 0) {
					reply[8] = V_BUSY;
					ctl_msg_send(n, p, M_ANSWER, reply, 9);
					return;
				}
				call_end(mine);
			}
			for (i = 0; i < n->ncalls; i++)		/* the same call twice */
				if (n->calls[i].state != CALL_IDLE && !memcmp(n->calls[i].id, pl, 8))
					return;
			if ((!admitted && interactive(n)) || !(c = call_slot(n))) {
				reply[8] = V_BUSY;		/* busy: answer without disturbing what is in progress */
				ctl_msg_send(n, p, M_ANSWER, reply, 9);
				return;
			}
			c->state = CALL_RINGING;
			c->name[0] = 0;
			{
				size_t nl;
				const uint8_t *nm = tlv_find(pl + base + cn, len - base - (size_t)cn, TAG_NAME, &nl);

				if (nm && name_ok(nm, nl)) {	/* a claim: shown as one, and never trusted for anything */
					memcpy(c->name, nm, nl);
					c->name[nl] = 0;
				}
			}
			c->admitted = admitted;
			c->auto_ = admitted;
			memcpy(c->id, pl, 8);
			memcpy(c->number, num, NUMLEN + 1);
			memcpy(c->bundle, pl + 8 + NUMLEN, LW_BUNDLE);
			memcpy(c->pc, cc, sizeof cc);
			c->npc = nc;
			c->deadline = n->now + 60.0;
			phrase_of(ph, sizeof ph, kid, n->id.key_id);
			{
				char hx[17], kh[65];

				lw_hex(hx, c->id, 8);
				lw_hex(kh, kid, 32);
				char en[2 * LW_NAME_MAX + 8];

				json_esc(en, sizeof en, c->name);
				ev_emit(n, "{\"event\":\"ring\",\"call\":\"%s\",\"from\":\"%s\",\"name\":\"%s\",\"key_id\":\"%s\",\"phrase\":\"%s\",\"known\":%s,\"admit\":%s}",
					hx, f, en, kh, ph, pc ? "true" : "false", admitted ? "true" : "false");
			}
			return;
		}
		case M_GO: {
			int nc = 0, role, delay;
			char num[NUMLEN + 1], f[16];
			uint8_t kid[32];
			lw_cand cc[MAXCANDS];
			lw_peer *q;
			lw_call *c;
			size_t o = 8 + 1 + 2 + NUMLEN + 32;

			if (len < o + LW_BUNDLE + 1)
				return;
			role = pl[8];
			delay = (int)get16(pl + 9);
			memcpy(num, pl + 11, NUMLEN);
			num[NUMLEN] = 0;
			if (!(c = call_by_id(n, pl))) {		/* the caller never saw the id: the number it dialed identifies the call */
				if (!(c = call_out_for(n, num)))
					return;
				memcpy(c->id, pl, 8);
			}
			if (c->state != CALL_OUT && c->state != CALL_RINGING)
				return;
			if (cands_get(pl + o + LW_BUNDLE, len - o - LW_BUNDLE, cc, &nc) < 0)
				return;
			lw_h(kid, pl + o, LW_BUNDLE);
			fmt_number(f, num);
			if (pin_check(n, num, kid) < 0) {
				ev_emit(n, "{\"event\":\"key-changed\",\"number\":\"%s\",\"action\":\"call dropped\"}", f);
				call_end(c);
				return;
			}
			if (!(q = peer_by_key(n, kid)))
				q = peer_add(n, pl + o);
			else if (q->cur.valid && !q->relayed) {	/* already connected: this call has nothing to add */
				call_end(c);
				return;
			}
			if (!q) {
				ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"peer table full\"}");
				call_end(c);
				return;
			}
			memcpy(q->number, num, NUMLEN + 1);
			memcpy(c->number, num, NUMLEN + 1);
			memcpy(c->token, pl + 11 + NUMLEN, 32);
			memcpy(c->pc, cc, sizeof cc);
			c->npc = nc;
			c->peer = (int)(q - n->peers);
			c->we_caller = role == 1;
			c->state = CALL_PUNCH;
			c->start_at = n->now + delay / 1000.0;
			c->next_punch = c->start_at;
			c->deadline = c->start_at + 8.0;
			c->have_verified = 0;
			return;
		}
		case M_REJECTED:
			if (len >= 9 + NUMLEN) {
				char num[NUMLEN + 1];
				lw_call *c;

				memcpy(num, pl + 9, NUMLEN);
				num[NUMLEN] = 0;
				c = call_by_id(n, pl);
				if (!c || c->state != CALL_OUT)
					c = call_out_for(n, num);
				if (c && c->state == CALL_OUT) {
					char f[16];

					fmt_number(f, c->number);
					if (!c->auto_)	/* a mesh call that fails is retried quietly */
						ev_emit(n, "{\"event\":\"call-failed\",\"number\":\"%s\",\"reason\":\"%s\"}", f, reason_str(pl[8]));
					call_end(c);
				}
			}
			return;
		}
		return;
	}
	if (msg == M_BYE) {				/* the other end hung up (leave = it also left the line) */
		char f[16];
		int i, idx = (int)(p - n->peers);

		fmt_number(f, p->number);
		ev_emit(n, "{\"event\":\"hangup\",\"number\":\"%s\",\"by\":\"them\"}", f);
		if (len >= 1 && pl[0])
			line_member_left(n, p->key_id);
		for (i = 0; i < n->ncalls; i++)
			if (n->calls[i].state != CALL_IDLE && n->calls[i].peer == idx)
				call_end(&n->calls[i]);
		peer_drop(n, p);
		return;
	}
	if (msg >= M_LINEHELLO && msg <= M_LINEKEY && msg != M_RELAY_REQ && msg != M_RELAY_OK)
		line_msg(n, p, msg, pl, len);
	else if (msg == M_RELAY_REQ || msg == M_RELAY_OK || msg == M_RELAY_NO)
		relay_msg(n, p, msg, pl, len);
}

/* Registering with our switchboard (a switchboard registering with its registry of codes does the same, once). */
void registration_timers(lw_node *n)
{
	lw_peer *s = sbp(n);

	if (s) {
		int need = n->registered ? n->reg_born != s->cur.born : n->reg_refused != s->cur.born;

		if (need && n->now >= n->next_reg) {
			uint8_t b[1 + 64 + 2 + LW_NAME_MAX + 2 + 2];
			size_t tl = strlen(n->cfg.token), o = 1 + tl;

			b[0] = (uint8_t)tl;
			memcpy(b + 1, n->cfg.token, tl);
			if (n->role)				/* a switchboard asking the registry for its code */
				o += (size_t)tlv_put(b + o, TAG_SWITCHBOARD, 0, 0);
			if (n->cfg.name[0])
				o += (size_t)tlv_put(b + o, TAG_NAME, n->cfg.name, strlen(n->cfg.name));
			if (n->cfg.listed)
				o += (size_t)tlv_put(b + o, TAG_LISTED, 0, 0);
			ctl_msg_send(n, s, M_REGISTER, b, o);
			n->next_reg = n->now + 2.0;
		} else if (n->registered && !n->role && n->now >= n->next_hb) {
			uint8_t b[1 + MAXCANDS * 7];
			lw_cand cc[MAXCANDS];
			int nc = gather(n, cc, MAXCANDS);

			ctl_msg_send(n, s, M_HEARTBEAT, b, (size_t)cands_put(b, cc, nc));
			n->next_hb = n->now + n->keepalive;
		}
	}
}

void node_timers(lw_node *n)
{
	int i;

	registration_timers(n);
	if (n->look.active && n->now - n->look.t > 5.0) {
		n->look.active = 0;
		ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"the directory did not answer\"}");
	}
	for (i = 0; i < n->ncalls; i++) {
		lw_call *c = &n->calls[i];

		switch (c->state) {
		case CALL_OUT:
			if (n->now > c->deadline) {
				char f[16];

				fmt_number(f, c->number);
				if (!c->auto_)
					ev_emit(n, "{\"event\":\"call-failed\",\"number\":\"%s\",\"reason\":\"no answer\"}", f);
				call_end(c);
			}
			break;
		case CALL_RINGING:
			if (n->now > c->deadline) {		/* nobody picked up: refuse */
				send_answer(n, c, V_REJECT);
				ev_emit(n, "{\"event\":\"call-failed\",\"reason\":\"you did not pick up\"}");
				call_end(c);
			}
			break;
		case CALL_UP:
			if (c->peer >= 0 && !n->peers[c->peer].cur.valid) {
				/* The session is gone (the dead-peer timer, or keys past their limit) and nothing has replaced it
				 * for a grace period: the other end has died or the path has. Say so; the script decides whether
				 * to dial again, and a line's mesh maintainer does so by itself. */
				if (!c->lost_t)
					c->lost_t = n->now;
				else if (n->now - c->lost_t > 2.0 * n->keepalive) {
					char f[16];

					fmt_number(f, c->number);
					ev_emit(n, "{\"event\":\"call-lost\",\"number\":\"%s\",\"we_called\":%s,\"reason\":\"no answer from the other end\"}",
						f, c->we_caller ? "true" : "false");
					peer_drop(n, &n->peers[c->peer]);
					call_end(c);
				}
			} else
				c->lost_t = 0;
			break;
		case CALL_PUNCH:
		case CALL_SETUP:
			if (c->peer >= 0 && n->peers[c->peer].cur.valid && !n->peers[c->peer].relayed) {
				char f[16], ep[64], ph[120];
				lw_peer *pe = &n->peers[c->peer];
				const uint8_t *pk = pe->key_id;

				fmt_number(f, c->number);
				addr_str(&pe->ep, ep, sizeof ep);
				if (c->we_caller)			/* (caller, callee) on both screens: the same six words */
					phrase_of(ph, sizeof ph, n->id.key_id, pk);
				else
					phrase_of(ph, sizeof ph, pk, n->id.key_id);
				pin_add(n, c->number, pk);		/* the session is proven; the number now belongs to this key */
				c->state = CALL_UP;
				ev_emit(n, "{\"event\":\"connected\",\"number\":\"%s\",\"path\":\"direct\",\"ep\":\"%s\",\"phrase\":\"%s\"}", f, ep, ph);
				call_peer_up(n, pe, c->we_caller);
				break;
			}
			if (c->state == CALL_PUNCH && n->now >= c->start_at && n->now >= c->next_punch) {
				punch_send(n, c);
				c->next_punch = n->now + 0.1;
			}
			if (n->now > c->deadline) {
				char f[16];

				fmt_number(f, c->number);
				if (!c->auto_)
					ev_emit(n, "{\"event\":\"call-failed\",\"number\":\"%s\",\"reason\":\"no direct path (both sides behind strict NAT?)\"}", f);
				if (c->peer >= 0 && !n->peers[c->peer].relayed)
					peer_drop(n, &n->peers[c->peer]);
				call_end(c);
			}
			break;
		}
	}
	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *p = &n->peers[i];
		const lw_peer *v;
		char f[16];

		if (!p->used || !p->relayed)
			continue;
		v = &n->peers[p->via_peer];
		fmt_number(f, p->number);
		if (!v->used || memcmp(v->key_id, p->via_kid, 32) || !v->cur.valid) {	/* the relay is gone: so is this path */
			if (p->announced)
				ev_emit(n, "{\"event\":\"call-lost\",\"number\":\"%s\",\"we_called\":%s,\"reason\":\"the relay went away\"}", f, p->dial ? "true" : "false");
			peer_drop(n, p);
			continue;
		}
		if (p->cur.valid && !p->announced) {
			char fv[16], ph[120];

			p->announced = 1;
			fmt_number(fv, v->number);
			phrase_of(ph, sizeof ph, p->dial ? n->id.key_id : p->key_id, p->dial ? p->key_id : n->id.key_id);
			ev_emit(n, "{\"event\":\"connected\",\"number\":\"%s\",\"path\":\"relay\",\"via\":\"%s\",\"phrase\":\"%s\"}", f, fv, ph);
			call_peer_up(n, p, p->dial);
		}
		if (p->announced && !p->cur.valid) {
			if (!p->lost_t)
				p->lost_t = n->now;
			else if (n->now - p->lost_t > 2.0 * n->keepalive) {
				ev_emit(n, "{\"event\":\"call-lost\",\"number\":\"%s\",\"we_called\":%s,\"reason\":\"no answer from the other end\"}", f, p->dial ? "true" : "false");
				peer_drop(n, p);
				continue;
			}
		} else
			p->lost_t = 0;
		if (!p->announced && !p->cur.valid && n->now - p->touched > 40.0) {	/* the session never came up */
			peer_drop(n, p);
			continue;
		}
		/* keep trying for a direct path, less often each time */
		if (p->cur.valid && n->now >= p->next_direct && !call_for_number(n, p->number) && n->registered) {
			char err[80];
			double wait = (double)n->cfg.direct_retry * (double)(1 << (p->direct_tries < 4 ? p->direct_tries : 4));

			if (wait > 600.0 && n->cfg.direct_retry <= 600)
				wait = 600.0;
			if (!call_dial(n, p->number, 1, err, sizeof err))
				p->direct_tries++;
			p->next_direct = n->now + wait;
		}
	}
	relay_timers(n);
	line_timers(n);
}

/* ---- commands from the control socket ---------------------------------------- */

/* The ringing call a command means: the one named by a call id prefix, else the only one (a person's, if several). */
static lw_call *ringing_for(lw_node *n, const char *prefix, const char **why)
{
	lw_call *found = 0, *person = 0;
	int i, matches = 0, persons = 0;

	for (i = 0; i < n->ncalls; i++) {
		lw_call *c = &n->calls[i];
		char hx[17];

		if (c->state != CALL_RINGING)
			continue;
		lw_hex(hx, c->id, 8);
		if (prefix[0] && strncmp(hx, prefix, strlen(prefix)))
			continue;
		matches++;
		found = c;
		if (!c->admitted) {
			persons++;
			person = c;
		}
	}
	if (!matches) {
		*why = "nothing is ringing";
		return 0;
	}
	if (matches == 1)
		return found;
	if (!prefix[0] && persons == 1)
		return person;
	*why = "several calls are ringing: name one by its call id";
	return 0;
}

int node_cmd(lw_node *n, const char *line, char *out, size_t cap)
{
	lw_peer *s = sbp(n);
	int r;

	if ((r = line_cmd(n, line, out, cap)) >= 0)		/* evict, peers */
		return r;
	if (!strncmp(line, "lookup ", 7) || (!strncmp(line, "dial lw1", 8))) {	/* the directory, by full address or by name; dialing waits on it */
		int dial = line[0] == 'd';
		const char *q = line + (dial ? 5 : 7);
		uint8_t b[1 + 64];
		size_t ql;

		if (!s || !n->registered)
			return snprintf(out, cap, "{\"error\":\"no dial tone: not registered with a switchboard\"}\n");
		if (!(n->sb_caps & CAPS_DIRECTORY))
			return snprintf(out, cap, "{\"error\":\"this switchboard has no directory\"}\n");
		if (dial && n->look.active && n->now - n->look.t < 5.0)
			return snprintf(out, cap, "{\"error\":\"already looking an address up\"}\n");
		if (!lw_address_parse(q, b + 1)) {
			b[0] = 0;
			ql = 32;
		} else if (dial) {
			return snprintf(out, cap, "{\"error\":\"not a full address (lw1 and 52 characters)\"}\n");
		} else {
			ql = strlen(q);
			if (ql < 3 || ql > LW_NAME_MAX || !name_ok((const uint8_t *)q, ql))
				return snprintf(out, cap, "{\"error\":\"a name to look for is 3 to %d characters\"}\n", LW_NAME_MAX);
			b[0] = 1;
			memcpy(b + 1, q, ql);
		}
		if (dial) {
			n->look.active = 1;
			n->look.kind = 0;
			n->look.t = n->now;
			memcpy(n->look.key, b + 1, 32);
		}
		ctl_msg_send(n, s, M_LOOKUP, b, 1 + ql);
		return snprintf(out, cap, "{\"%s\":\"asked\"}\n", dial ? "call" : "lookup");
	}
	if (!strncmp(line, "dial ", 5)) {
		char num[NUMLEN + 1], f[16], err[160];

		if (parse_full(n, line + 5, num, err, sizeof err))
			return snprintf(out, cap, "{\"error\":\"%s\"}\n", err);
		if (call_dial(n, num, 0, err, sizeof err))
			return snprintf(out, cap, "{\"error\":\"%s\"}\n", err);
		fmt_number(f, num);
		return snprintf(out, cap, "{\"call\":\"dialing\",\"number\":\"%s\"}\n", f);
	}
	if (!strncmp(line, "pickup", 6) || !strncmp(line, "reject", 6)) {
		int accept = line[0] == 'p';
		const char *why = "";
		lw_call *c = ringing_for(n, line[6] == ' ' ? line + 7 : "", &why);

		if (!c || !s)
			return snprintf(out, cap, "{\"error\":\"%s\"}\n", c ? "no switchboard" : why);
		send_answer(n, c, accept ? V_ACCEPT : V_REJECT);
		if (!accept)
			call_end(c);
		return snprintf(out, cap, "{\"call\":\"%s\"}\n", accept ? "answered" : "rejected");
	}
	if (!strncmp(line, "who ", 4)) {		/* everything this node knows about a number: nothing is asked of the switchboard */
		char num[NUMLEN + 1], f[16], addr[60], kh[65], ph[120] = "";
		const uint8_t *kid = 0;
		const lw_peer *p;
		const lw_member *m = 0;
		char perr[160];
		int i, pinned = 0, len;

		if (parse_full(n, line + 4, num, perr, sizeof perr))
			return snprintf(out, cap, "{\"error\":\"%s\"}\n", perr);
		fmt_number(f, num);
		if (!strcmp(num, n->number))
			kid = n->id.key_id;
		for (i = 0; i < n->npins && !kid; i++)
			if (pin_home(n, i) && !strcmp(n->pins[i].number, num)) {
				kid = n->pins[i].key_id;
				pinned = 1;
			}
		p = peer_by_number(n, num);
		if (!kid && p)
			kid = p->key_id;
		for (i = 0; i < n->line.nm && !kid; i++)
			if (!strcmp(n->line.m[i].number, num))
				kid = n->line.m[i].key_id;
		if (!kid)
			return snprintf(out, cap, "{\"error\":\"%s is not known here: nothing has been called, pinned or listed on your line\"}\n", f);
		if (n->line.have)
			m = line_member(n, kid);
		lw_address(addr, kid);
		lw_hex(kh, kid, 32);
		if (p && p->cur.valid)
			phrase_of(ph, sizeof ph, p->dial ? n->id.key_id : p->key_id, p->dial ? p->key_id : n->id.key_id);
		len = snprintf(out, cap, "{\"number\":\"%s\",\"you\":%s,\"key_id\":\"%s\",\"address\":\"%s\",\"pinned\":%s,\"line_member\":%s,"
			"\"connected\":%s,\"path\":\"%s\",\"phrase\":\"%s\"}\n", f, kid == n->id.key_id ? "true" : "false", kh, addr,
			pinned ? "true" : "false", m ? "true" : "false", p && p->cur.valid ? "true" : "false",
			p && p->cur.valid ? (p->relayed ? "relay" : "direct") : "", ph);
		return len;
	}
	if (!strncmp(line, "unpin ", 6)) {
		char num[NUMLEN + 1], f[16], perr[160];

		if (parse_full(n, line + 6, num, perr, sizeof perr))
			return snprintf(out, cap, "{\"error\":\"%s\"}\n", perr);
		fmt_number(f, num);
		if (!pin_remove(n, num))
			return snprintf(out, cap, "{\"error\":\"%s was not pinned\"}\n", f);
		return snprintf(out, cap, "{\"unpinned\":\"%s\"}\n", f);
	}
	if (!strncmp(line, "hangup", 6)) {
		char num[NUMLEN + 1] = "", perr[160];
		int i, any = 0, leave;

		if (line[6] == ' ' && parse_full(n, line + 7, num, perr, sizeof perr))
			return snprintf(out, cap, "{\"error\":\"%s\"}\n", perr);
		leave = !num[0] && n->line.have;		/* no number: hang up on everyone, and leave the line */
		for (i = 0; i < n->ncalls; i++) {
			lw_call *c = &n->calls[i];

			if (c->state == CALL_IDLE || (num[0] && strcmp(c->number, num)))
				continue;
			any = 1;
			if (c->state == CALL_RINGING)
				send_answer(n, c, V_REJECT);
			if (c->peer >= 0) {
				lw_peer *p = &n->peers[c->peer];
				char f[16];
				uint8_t lv = (uint8_t)leave;

				if (p->cur.valid)
					ctl_msg_send(n, p, M_BYE, &lv, 1);
				fmt_number(f, p->number);
				ev_emit(n, "{\"event\":\"hangup\",\"number\":\"%s\",\"by\":\"us\"}", f);
				peer_drop(n, p);
			}
			call_end(c);
		}
		if (leave)
			line_leave(n);
		if (!any)
			return snprintf(out, cap, "{\"error\":\"no call\"}\n");
		return snprintf(out, cap, "{\"call\":\"hung up\"}\n");
	}
	return snprintf(out, cap, "{\"error\":\"unknown request\"}\n");
}

/* Registration usually completes within milliseconds of the worker starting, before a script has subscribed
 * to the events: so a new subscriber is told where things stand, as the events it would have seen. */
int node_replay(lw_node *n, char *o, size_t cap)
{
	char f[16], fl[40];
	int len = 0;

	if (n->registered) {
		fmt_number(f, n->number);
		full_number(n, n->number, fl, sizeof fl);
		len = snprintf(o, cap, "{\"event\":\"registered\",\"number\":\"%s\",\"full\":\"%s\"}\n", f, fl);
	} else if (n->reg_why[0])
		len = snprintf(o, cap, "{\"event\":\"register-refused\",\"reason\":\"%s\"}\n", n->reg_why);
	return len + line_replay(n, o + len, cap - (size_t)len);
}

int node_status_json(lw_node *n, char *o, size_t cap)
{
	char codes_json[48] = "", kid[65], ep[64], num[16] = "", fullnum[40] = "", sbs[24], refl[64] = "";
	const char *cs = "idle";
	int len, i, any = 0;

	for (i = 0; i < n->ncalls; i++) {		/* one word for the calls as a whole: the most urgent first */
		const lw_call *k = &n->calls[i];

		if (k->state == CALL_OUT && !k->auto_)
			cs = "dialing";
		else if (k->state == CALL_RINGING && !k->admitted && strcmp(cs, "dialing"))
			cs = "ringing";
		else if ((k->state == CALL_PUNCH || k->state == CALL_SETUP) && !strcmp(cs, "idle"))
			cs = "connecting";
		else if (k->state == CALL_UP && !strcmp(cs, "idle"))
			cs = "up";
	}
	lw_hex(kid, n->id.key_id, 32);
	if (n->number[0]) {
		fmt_number(num, n->number);
		full_number(n, n->number, fullnum, sizeof fullnum);
	}
	if (n->sb >= 0) {			/* the codes our switchboard answers to: the registry's, if it has one, and the derived one */
		char dc[9];

		sb_code(n->peers[n->sb].key_id, dc);
		snprintf(codes_json, sizeof codes_json, "%s%s%s\"%s\"", n->home_code[0] ? "\"" : "", n->home_code, n->home_code[0] ? "\"," : "", dc);
	}
	snprintf(sbs, sizeof sbs, "%s", n->sb < 0 ? "none" : sbp(n) ? (n->registered ? "dial tone" : "registering") : "no switchboard");
	if (n->have_refl)
		snprintf(refl, sizeof refl, "%u.%u.%u.%u:%u", n->refl.ip[0], n->refl.ip[1], n->refl.ip[2], n->refl.ip[3], n->refl.port);
	len = snprintf(o, cap,
		"{\"node\":\"up\",\"key_id\":\"%s\",\"number\":\"%s\",\"full\":\"%s\",\"switchboard\":\"%s\",\"reflexive\":\"%s\",\"call\":\"%s\","
		"\"uptime\":%d,\"port\":\"%s:%s\",\"macs\":%d,"
		"\"local_rx\":%llu,\"local_tx\":%llu,\"flood\":%llu,\"drop_garbage\":%llu,\"drop_noroute\":%llu,\"frames_split\":%llu,\"frames_joined\":%llu,\"drop_flood\":%llu,\"arp_answered\":%llu,\"drop_standby\":%llu,\"resegmented\":%llu,\"drop_oversize\":%llu,\"site\":\"%s\",\"site_members\":%d,\"site_synced\":%d,\"hs_shed\":%llu,\"cookies_sent\":%llu,\"hs_nocookie\":%llu,\"hs_badmac2\":%llu,\"cookies_got\":%llu,\"registrations\":%llu,\"sb_rekeys\":%llu,\"switchboard_offers\":[%s],\"switchboard_codes\":[%s],\"relay\":{\"routes\":%d,\"forwarded\":%llu,\"dropped\":%llu,\"offer\":%s},\"peers\":[",
		kid, num, fullnum, sbs, refl, cs, (int)(n->now - n->started), n->cfg.port_kind, n->cfg.port_arg, sw_count(n),
		(unsigned long long)n->local_rx, (unsigned long long)n->local_tx, (unsigned long long)n->flood,
		(unsigned long long)n->drop_garbage, (unsigned long long)n->drop_noroute,
		(unsigned long long)n->frames_split, (unsigned long long)n->frames_joined, (unsigned long long)n->drop_flood,
		(unsigned long long)n->arp_answered, (unsigned long long)n->drop_standby, (unsigned long long)n->resegmented, (unsigned long long)n->drop_oversize, site_role(n), site_members(n), n->site_sync_n,
		(unsigned long long)n->hs_shed, (unsigned long long)n->cookies_sent, (unsigned long long)n->hs_nocookie, (unsigned long long)n->hs_badmac2, (unsigned long long)n->cookies_got, (unsigned long long)n->reg_count,
		(unsigned long long)(n->sb >= 0 ? n->peers[n->sb].rekeys : 0),
		(n->sb_caps & CAPS_CALLERID) && (n->sb_caps & CAPS_DIRECTORY) ? "\"caller-id\",\"directory\"" : (n->sb_caps & CAPS_CALLERID) ? "\"caller-id\"" : (n->sb_caps & CAPS_DIRECTORY) ? "\"directory\"" : "",
		codes_json,
		relay_live(n), (unsigned long long)n->relay_fwd, (unsigned long long)n->relay_drop, relay_offered(n) ? "true" : "false");
	for (i = 0; i < n->maxpeers && len < (int)cap - 700; i++) {
		lw_peer *p = &n->peers[i];
		char f[16] = "";

		if (!p->used || p->is_sb)
			continue;
		if (p->number[0])
			fmt_number(f, p->number);
		lw_hex(kid, p->key_id, 32);
		addr_str(&p->ep, ep, sizeof ep);
		len += snprintf(o + len, cap - (size_t)len,
			"%s{\"key_id\":\"%s\",\"number\":\"%s\",\"state\":\"%s\",\"path\":\"%s\",\"ep\":\"%s\",\"age\":%d,"
			"\"rx_frames\":%llu,\"tx_frames\":%llu,\"rekeys\":%llu,\"drop_auth\":%llu,\"drop_replay\":%llu}",
			any ? "," : "", kid, f, sess_state(p), p->relayed ? "relay" : "direct", ep,
			p->cur.valid ? (int)(n->now - p->cur.born) : -1,
			(unsigned long long)p->rx_frames, (unsigned long long)p->tx_frames,
			(unsigned long long)p->rekeys, (unsigned long long)p->drop_auth,
			(unsigned long long)p->drop_replay);
		any = 1;
	}
	len += snprintf(o + len, cap - (size_t)len, "],\"calls\":[");
	any = 0;
	for (i = 0; i < n->ncalls && len < (int)cap - 400; i++) {
		static const char *const SN[] = { "idle", "dialing", "ringing", "punching", "connecting", "up" };
		const lw_call *k = &n->calls[i];
		char f[16], hx[17];

		if (k->state == CALL_IDLE)
			continue;
		fmt_number(f, k->number);
		lw_hex(hx, k->id, 8);
		len += snprintf(o + len, cap - (size_t)len, "%s{\"call\":\"%s\",\"number\":\"%s\",\"state\":\"%s\",\"we_called\":%s,\"mesh\":%s}",
				any ? "," : "", hx, f, SN[k->state], k->we_caller ? "true" : "false", k->auto_ ? "true" : "false");
		any = 1;
	}
	len += snprintf(o + len, cap - (size_t)len, "],\"line\":");
	{
		int l = line_value(n, o + len, cap - (size_t)len);

		len += l;
	}
	len += snprintf(o + len, cap - (size_t)len, "}\n");
	return len;
}