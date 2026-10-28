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

#ifndef LW_VERSION
#define LW_VERSION "dev"
#endif
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

/* A number that arrives on the wire is nine digits. Anything else was not given out by a switchboard, and a byte that is
 * not a digit must not reach a pin file, an event or a status line (the checksum is not checked: it never mattered here). */
int num_wire_ok(const uint8_t *s)
{
	int i;

	for (i = 0; i < NUMLEN; i++)
		if (s[i] < '0' || s[i] > '9')
			return 0;
	return 1;
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
	char h[INET6_ADDRSTRLEN];

	if (ss->ss_family == AF_INET) {
		const struct sockaddr_in *a = (const void *)ss;

		inet_ntop(AF_INET, &a->sin_addr, h, sizeof h);
		snprintf(out, n, "%s:%u", h, (unsigned)ntohs(a->sin_port));
	} else if (ss->ss_family == AF_INET6) {
		const struct sockaddr_in6 *a = (const void *)ss;

		inet_ntop(AF_INET6, &a->sin6_addr, h, sizeof h);
		snprintf(out, n, "[%s]:%u", h, (unsigned)ntohs(a->sin6_port));
	} else
		snprintf(out, n, "-");
}

/* Whoever is "one source" for a limit: an IPv4 address, or an IPv6 /64 (a host can have as many addresses in its /64 as it likes). */
void net_str(const struct sockaddr_storage *ss, char *out, size_t n)
{
	if (ss->ss_family == AF_INET) {
		char h[INET_ADDRSTRLEN];

		inet_ntop(AF_INET, &((const struct sockaddr_in *)(const void *)ss)->sin_addr, h, sizeof h);
		snprintf(out, n, "%s", h);
	} else if (ss->ss_family == AF_INET6) {
		const uint8_t *a = (const uint8_t *)&((const struct sockaddr_in6 *)(const void *)ss)->sin6_addr;

		snprintf(out, n, "v6:%02x%02x%02x%02x%02x%02x%02x%02x", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
	} else
		snprintf(out, n, "-");
}

/* HOST:PORT or [IPV6]:PORT. A name with both kinds of address resolves to the IPv4 one. */
int lw_resolve(const char *hp, struct sockaddr_storage *ss, socklen_t *sl)
{
	char host[96];
	const char *c = strrchr(hp, ':');
	struct addrinfo hint, *res, *r, *pick = 0;
	size_t hl;

	if (!c || c == hp)
		return -1;
	hp += hp[0] == '[';
	hl = (size_t)(c - hp) - (c[-1] == ']');
	if (hl >= sizeof host)
		return -1;
	snprintf(host, sizeof host, "%.*s", (int)hl, hp);
	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host, c + 1, &hint, &res))
		return -1;
	for (r = res; r; r = r->ai_next)
		if (r->ai_family == AF_INET || (r->ai_family == AF_INET6 && !pick))
			if (!pick || pick->ai_family != AF_INET)
				pick = r;
	if (!pick) {
		freeaddrinfo(res);
		return -1;
	}
	memcpy(ss, pick->ai_addr, pick->ai_addrlen);
	*sl = pick->ai_addrlen;
	freeaddrinfo(res);
	return 0;
}

void cand_sa(const lw_cand *c, struct sockaddr_storage *ss, socklen_t *sl)
{
	memset(ss, 0, sizeof *ss);
	if (c->fam == 6) {
		struct sockaddr_in6 *a = (void *)ss;

		a->sin6_family = AF_INET6;
		memcpy(&a->sin6_addr, c->ip, 16);
		a->sin6_port = htons(c->port);
		*sl = sizeof *a;
	} else {
		struct sockaddr_in *a = (void *)ss;

		a->sin_family = AF_INET;
		memcpy(&a->sin_addr, c->ip, 4);
		a->sin_port = htons(c->port);
		*sl = sizeof *a;
	}
}

/* The address a socket address names, as a candidate (its type is the caller's to set). */
void sa_cand(const struct sockaddr *sa, lw_cand *c)
{
	memset(c, 0, sizeof *c);
	if (sa->sa_family == AF_INET6) {
		const struct sockaddr_in6 *a = (const void *)sa;

		c->fam = 6;
		memcpy(c->ip, &a->sin6_addr, 16);
		c->port = ntohs(a->sin6_port);
	} else {
		const struct sockaddr_in *a = (const void *)sa;

		c->fam = 4;
		memcpy(c->ip, &a->sin_addr, 4);
		c->port = ntohs(a->sin_port);
	}
}

/* ---- candidates: where the other side may reach us --------------------------- */

static int is_jack(const lw_node *n)
{
	return !strcmp(n->cfg.port_kind, "jack");
}

static int gather(lw_node *n, lw_cand *out, int max)
{
	struct ifaddrs *ifa, *i;
	int c = 0;

	if (max > MAXCANDS - 1)			/* the switchboard adds the address it sees us at */
		max = MAXCANDS - 1;
	if (getifaddrs(&ifa))
		return 0;
	for (i = ifa; i && c < max - 1; i = i->ifa_next) {
		const struct sockaddr_in *a;
		const uint8_t *ip;

		if (is_jack(n) && !strcmp(i->ifa_name, n->cfg.port_arg))	/* an address on our own jack is on the far side of the tunnel: no use to the other end */
			continue;
		if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET6 && (i->ifa_flags & IFF_UP) && n->udp6 >= 0) {
			const struct sockaddr_in6 *a6 = (const void *)i->ifa_addr;

			if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr) || IN6_IS_ADDR_MULTICAST(&a6->sin6_addr) ||
			    ((i->ifa_flags & IFF_LOOPBACK) && !n->cfg.cand_loopback))
				continue;
			memset(&out[c], 0, sizeof out[c]);
			memcpy(out[c].ip, &a6->sin6_addr, 16);
			out[c].port = n->udp6_port;
			out[c].type = CAND_HOST;
			out[c].fam = 6;
			c++;
			continue;
		}
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || n->udp < 0)
			continue;
		if ((i->ifa_flags & IFF_LOOPBACK) && !n->cfg.cand_loopback)
			continue;
		a = (const void *)i->ifa_addr;
		ip = (const uint8_t *)&a->sin_addr;
		if (ip[0] == 169 && ip[1] == 254)
			continue;
		memset(&out[c], 0, sizeof out[c]);
		memcpy(out[c].ip, ip, 4);
		out[c].port = n->udp_port;
		out[c].type = CAND_HOST;
		out[c].fam = 4;
		c++;
	}
	freeifaddrs(ifa);
	if (n->have_refl && c < max)
		out[c++] = n->refl;
	if (n->pmp.port && n->pmp.have_ip && c < max) {
		memset(&out[c], 0, sizeof out[c]);
		memcpy(out[c].ip, n->pmp.ip, 4);
		out[c].port = n->pmp.port;
		out[c].type = CAND_MAPPED;
		out[c].fam = 4;
		c++;
	}
	return c;
}

static int cands_put(lw_node *n, uint8_t *p, const lw_cand *c, int nc)
{
	int i, o = 1;

	p[0] = (uint8_t)nc | (n->nat == 2 ? CANDS_STRICT : 0);	/* the other end decides who sprays and who probes */
	for (i = 0; i < nc; i++)
		o += cand_put(p + o, &c[i]);
	return o;
}

/* returns bytes consumed, or -1; entries of a kind we do not know are skipped */
static int cands_get(lw_node *n, const uint8_t *p, size_t len, lw_cand *c, int *nc, int *strict)
{
	int i, k, o = 1, w, known;

	if (len < 1)
		return -1;
	k = p[0] & ~CANDS_STRICT;
	if (strict)
		*strict = p[0] & CANDS_STRICT ? 1 : 0;
	if (k > MAXCANDS)
		return -1;
	jnets_fresh(n);
	*nc = 0;
	for (i = 0; i < k; i++) {
		if ((w = cand_get(p + o, len - o, &c[*nc], &known)) < 0)
			return -1;
		o += w;
		if (known && jack_net_has(n, c[*nc].fam, c[*nc].ip) && !(c[*nc].fam == 4 && under_fd(n) >= 0)) {
			char ab[64] = "";

			n->cand_skipped++;
			if (c[*nc].fam == 4)
				snprintf(ab, sizeof ab, "%u.%u.%u.%u", c[*nc].ip[0], c[*nc].ip[1], c[*nc].ip[2], c[*nc].ip[3]);
			else
				inet_ntop(AF_INET6, c[*nc].ip, ab, sizeof ab);
			if (n->now - n->cand_skip_said > 60.0) {		/* said now and then, not for every ring */
				n->cand_skip_said = n->now;
				ev_emit(n, "{\"event\":\"address-skipped\",\"address\":\"%s\",\"port\":%u}", ab, c[*nc].port);
			}
			continue;
		}
		*nc += known;
	}
	return o;
}

/* ---- HELLO: fetch a bundle when only a key id is known ----------------------- */

void hello_request(lw_node *n, lw_peer *p)
{
	uint8_t b[10];

	b[0] = T_HELLO;
	b[1] = 0;
	memcpy(b + 2, p->key_id, 8);
	sendto(peer_fd(n, p), b, sizeof b, 0, (struct sockaddr *)&p->ep, p->eplen);
}

void hello_offer(lw_node *n, const struct sockaddr *dst, socklen_t dl)
{
	uint8_t b[2 + LW_BUNDLE];

	b[0] = T_HELLO;
	b[1] = 2;
	memcpy(b + 2, n->id.bundle, LW_BUNDLE);
	usend(n, ufd(n, dst->sa_family), b, sizeof b, dst, dl);
}

unsigned offer_home(const lw_node *n, const uint8_t kid[32])
{
	uint8_t hv[8];

	crypto_blake2b_keyed(hv, 8, (const uint8_t *)&n->hsalt, 8, kid, 32);	/* a key id can be ground to any value: the place is not the key id's to choose */
	return (unsigned)(get32(hv) % OFFERS);
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
		sendto(n->rx_fd, b, sizeof b, 0, src, sl);
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
		lw_offer *a, *b2, *o;

		lw_h(kid, pkt + 2, LW_BUNDLE);
		a = &n->offers[offer_home(n, kid)];
		b2 = &n->offers[offer_home(n, kid) ^ 1];
		if (a->used && !crypto_verify32(a->key_id, kid))
			o = a;
		else if (b2->used && !crypto_verify32(b2->key_id, kid))
			o = b2;
		else if (!a->used)
			o = a;
		else if (!b2->used)
			o = b2;
		else
			o = a->t <= b2->t ? a : b2;
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
	char path[300], tmp[310], hex[65];
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
	snprintf(tmp, sizeof tmp, "%s.new", path);		/* written whole, then put in place: a crash keeps the old pins */
	if ((f = fopen(tmp, "w"))) {
		chmod(tmp, 0600);
		for (i = 0; i < n->npins; i++) {
			char sbh[65];

			lw_hex(hex, n->pins[i].key_id, 32);
			lw_hex(sbh, n->pins[i].sb, 32);
			fprintf(f, "%s %s %s\n", sbh, n->pins[i].number, hex);
		}
		if (fclose(f) == 0)
			rename(tmp, path);
		else
			unlink(tmp);
	}
	return 1;
}

/* Register with a switchboard given as HOST:PORT/KEY (for a switchboard worker: the registry of codes). */
int sb_connect(lw_node *n, const char *spec, char *err, size_t errn)
{
	char hp[200];
	const char *slash = strrchr(spec, '/');
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
	uint8_t b[8 + 1 + 1 + MAXCANDS * CANDWIRE];
	lw_cand cc[MAXCANDS];
	int nc;

	if (!s)
		return;
	memcpy(b, c->id, 8);
	b[8] = (uint8_t)verdict;
	if (verdict == V_ACCEPT)
		c->answered = 1;
	nc = verdict == V_ACCEPT ? gather(n, cc, MAXCANDS) : 0;
	ctl_msg_send(n, s, M_ANSWER, b, 9 + (size_t)cands_put(n, b + 9, cc, nc));
}

/* Start a call: the interactive kind a person asked for, or a mesh call to a fellow member. */
int call_dial(lw_node *n, const char *num, int auto_, char *err, size_t errn)
{
	lw_peer *s = sbp(n);
	uint8_t b[NUMLEN + 1 + MAXCANDS * CANDWIRE + 2 + ADMIT_LEN];
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
	o = NUMLEN + (size_t)cands_put(n, b + NUMLEN, cc, nc);
	if (line_admit_for(n, admit))			/* present our admit: a member we dial will not ask about us */
		o += (size_t)tlv_put(b + o, TAG_ADMIT, admit, ADMIT_LEN);
	ctl_msg_send(n, s, M_DIAL, b, o);
	return 0;
}

void punch_pkt(lw_node *n, const lw_call *c, uint8_t pk[33])
{
	pk[0] = T_PUNCH;
	memcpy(pk + 1, c->id, 8);
	memcpy(pk + 9, n->id.key_id, 8);
	crypto_blake2b_keyed(pk + 17, 16, c->token, 32, pk, 17);
}

static void punch_send(lw_node *n, lw_call *c)
{
	uint8_t pk[33];
	int i;

	punch_pkt(n, c, pk);
	for (i = 0; i < c->npc; i++) {
		struct sockaddr_storage a;
		socklen_t al;

		cand_sa(&c->pc[i], &a, &al);
		usend(n, ufd(n, a.ss_family), pk, sizeof pk, (struct sockaddr *)&a, al);
	}
	if (c->have_verified)		/* answer from where it really came from */
		usend(n, c->peer >= 0 ? peer_fd(n, &n->peers[c->peer]) : ufd(n, c->verified.ss_family), pk, sizeof pk, (struct sockaddr *)&c->verified, c->verified_len);
}

/* ECHO (docs/PROTOCOL.md section 4). A switchboard answers on its second port with the address it sees the asker at; a node
 * compares that with what the first port said. The same port both times means the NAT keeps one mapping however many
 * destinations there are (easy); a different port means it makes a new one for each (strict). */
void echo_serve(lw_node *n)
{
	uint8_t in[64], out[15];
	struct sockaddr_in a;
	socklen_t al;
	int i;

	for (i = 0; i < 64; i++) {
		al = sizeof a;
		ssize_t r = recvfrom(n->udp2, in, sizeof in, 0, (struct sockaddr *)&a, &al);

		if (r < 0)
			break;
		if (r < 40 || in[0] != T_ECHO || al != sizeof a || a.sin_family != AF_INET)	/* a request is longer than its reply: no amplification */
			continue;
		out[0] = T_ECHO;
		memcpy(out + 1, in + 1, 8);
		memcpy(out + 9, &a.sin_addr, 4);
		put16(out + 13, ntohs(a.sin_port));
		sendto(n->udp2, out, sizeof out, 0, (struct sockaddr *)&a, sizeof a);
	}
}

void echo_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl)
{
	lw_peer *s = sbp(n);
	const struct sockaddr_in *a = (const struct sockaddr_in *)src;

	if (n->role || len != 15 || !s || !n->have_refl || !n->echo_tries || memcmp(pkt + 1, n->echo_nonce, 8) ||
	    sl != sizeof *a || s->ep.ss_family != AF_INET || a->sin_addr.s_addr != ((struct sockaddr_in *)&s->ep)->sin_addr.s_addr)
		return;
	n->nat = (!memcmp(pkt + 9, n->refl.ip, 4) && get16(pkt + 13) == n->refl.port) ? 1 : 2;
	n->echo_tries = 0;
	n->next_echo = n->now + 600.0;
}

static void echo_ask(lw_node *n, lw_peer *s)
{
	uint8_t q[40] = { T_ECHO };
	struct sockaddr_in a;

	if (s->ep.ss_family != AF_INET || n->udp < 0)
		return;				/* ECHO is IPv4 only */
	lw_rand(n->echo_nonce, 8);
	memcpy(q + 1, n->echo_nonce, 8);
	memcpy(&a, &s->ep, sizeof a);
	a.sin_port = htons((uint16_t)(ntohs(a.sin_port) + 1));
	sendto(n->udp, q, sizeof q, 0, (struct sockaddr *)&a, sizeof a);
	n->echo_tries++;
	n->next_echo = n->now + 2.0;
}

void punch_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl)
{
	lw_call *c;
	uint8_t mac[16];
	lw_peer *p;

	if (len != 33 || sl > sizeof c->verified || !(c = call_by_id(n, pkt + 1)) || c->state != CALL_PUNCH || c->peer < 0)
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
	c->verified_len = sl;
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
	if (n->rx_fd != n->udp && spray_is(n, n->rx_fd))	/* found by a probe on one of the sprayed sockets: keep that one */
		spray_adopt(n, p);
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
			if (len >= NUMLEN + 6 && num_wire_ok(pl)) {
				int news = !n->registered || memcmp(n->number, pl, NUMLEN);	/* re-registering on a rekey isn't news */

				memcpy(n->number, pl, NUMLEN);
				n->number[NUMLEN] = 0;
				memcpy(n->refl.ip, pl + NUMLEN, 4);
				n->refl.port = (uint16_t)get16(pl + NUMLEN + 4);
				n->refl.type = CAND_REFL;
				n->refl.fam = 4;
				n->have_refl = n->refl.port != 0;	/* a switchboard reached over IPv6 has nothing to say of our IPv4 address */
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
			if (len < 4 + ql || kind > 1 || ql > 64 || (kind == 0 && ql != 32))
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

				if (o + NUMLEN + 33 > len || !num_wire_ok(pl + o))
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
				n->refl.fam = 4;
				n->have_refl = n->refl.port != 0;
			}
			return;
		case M_REFUSED: {
			char r[80];
			size_t k;

			snprintf(r, sizeof r, "%.*s", (int)(len > 70 ? 70 : len), (const char *)pl);
			for (k = 0; r[k]; k++)		/* text from the switchboard: plain ASCII, then escaped, because it goes into JSON */
				if ((unsigned char)r[k] > 0x7e)
					r[k] = '?';
			n->reg_refused = p->cur.born;	/* don't hammer: try again on the next session */
			json_esc(n->reg_why, sizeof n->reg_why, r);
			ev_emit(n, "{\"event\":\"register-refused\",\"reason\":\"%s\"}", n->reg_why);
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

			if (len < base + 1 || !num_wire_ok(pl + 8))
				return;
			memcpy(num, pl + 8, NUMLEN);
			num[NUMLEN] = 0;
			memcpy(reply, pl, 8);
			if ((cn = cands_get(n, pl + base, len - base, cc, &nc, 0)) < 0)
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
			c->has_admit = admit != 0;	/* the hub of a managed line passes it on, as the operator signed it */
			if (admit)
				memcpy(c->admit, admit, ADMIT_LEN);
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
			int nc = 0, role, delay, gostrict = 0;
			char num[NUMLEN + 1], f[16];
			uint8_t kid[32];
			lw_cand cc[MAXCANDS];
			lw_peer *q;
			lw_call *c;
			size_t o = 8 + 1 + 2 + NUMLEN + 32;

			if (len < o + LW_BUNDLE + 1 || !num_wire_ok(pl + 11))
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
			if (c->state != CALL_OUT && (c->state != CALL_RINGING || !c->answered))	/* nothing connects to a ring that was not answered */
				return;
			if (cands_get(n, pl + o + LW_BUNDLE, len - o - LW_BUNDLE, cc, &nc, &gostrict) < 0)
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
			c->peer_strict = gostrict;
			c->peer = (int)(q - n->peers);
			c->we_caller = role == 1;
			c->state = CALL_PUNCH;
			c->start_at = n->now + delay / 1000.0;
			c->next_punch = c->start_at;
			c->deadline = c->start_at + 8.0;
			if ((n->nat == 2) != c->peer_strict)	/* one end is strict and the other is not: there is a way, and it takes longer */
				c->deadline = c->start_at + 17.0;
			c->have_verified = 0;
			return;
		}
		case M_REJECTED:
			if (len >= 9 + NUMLEN && num_wire_ok(pl + 9)) {
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
			uint8_t b[1 + MAXCANDS * CANDWIRE];
			lw_cand cc[MAXCANDS];
			int nc = gather(n, cc, MAXCANDS);

			ctl_msg_send(n, s, M_HEARTBEAT, b, (size_t)cands_put(n, b, cc, nc));
			n->next_hb = n->now + n->keepalive;
		} else if (n->registered && !n->role && n->have_refl && n->now >= n->next_echo) {
			if (n->echo_tries >= 4) {		/* no answer: the switchboard has no second port, or a firewall hides it */
				n->echo_tries = 0;
				n->next_echo = n->now + 600.0;
			} else
				echo_ask(n, s);
		}
	}
}

void node_timers(lw_node *n)
{
	int i;

	registration_timers(n);
	pmp_timers(n);
	spray_reap(n);
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
			if (c->state == CALL_PUNCH)
				spray_step(n, c);
			if (n->now > c->deadline) {
				char f[16];

				fmt_number(f, c->number);
				if (!c->auto_)
					ev_emit(n, "{\"event\":\"call-failed\",\"number\":\"%s\",\"reason\":\"no direct path (%s)\"}", f,
						n->nat == 2 ? "this network's NAT is strict: it makes a new mapping for every destination" : "both sides behind strict NAT?");
				if (c->peer >= 0 && !n->peers[c->peer].relayed)
					peer_drop(n, &n->peers[c->peer]);
				spray_close(n);
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
	char codes_json[48] = "", kid[65], ep[64], num[16] = "", fullnum[40] = "", sbs[24], refl[64] = "", mapped[64];
	char nm[2 * LW_NAME_MAX + 8];
	const char *cs = "idle";
	int len, i, any = 0;

	json_esc(nm, sizeof nm, n->cfg.name);
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
	mapped[0] = 0;
	if (n->pmp.port && n->pmp.have_ip)
		snprintf(mapped, sizeof mapped, "%u.%u.%u.%u:%u", n->pmp.ip[0], n->pmp.ip[1], n->pmp.ip[2], n->pmp.ip[3], n->pmp.port);
	if (n->have_refl)
		snprintf(refl, sizeof refl, "%u.%u.%u.%u:%u", n->refl.ip[0], n->refl.ip[1], n->refl.ip[2], n->refl.ip[3], n->refl.port);
	len = snprintf(o, cap,
		"{\"node\":\"up\",\"key_id\":\"%s\",\"number\":\"%s\",\"full\":\"%s\",\"name\":\"%s\",\"listed\":%s,\"switchboard\":\"%s\",\"reflexive\":\"%s\",\"nat\":\"%s\",\"mapped\":\"%s\",\"call\":\"%s\","
		"\"uptime\":%d,\"port\":\"%s:%s\",\"macs\":%d,"
		"\"local_rx\":%llu,\"local_tx\":%llu,\"flood\":%llu,\"drop_garbage\":%llu,\"drop_noroute\":%llu,\"frames_split\":%llu,\"frames_joined\":%llu,\"drop_flood\":%llu,\"arp_answered\":%llu,\"dhcp_conflicts\":%llu,\"drop_standby\":%llu,\"resegmented\":%llu,\"drop_oversize\":%llu,\"cand_skipped\":%llu,\"version\":\"%s\",\"underlay\":\"%s\",\"underlay_state\":\"%s\",\"jack_nets\":%d,\"site\":\"%s\",\"site_members\":%d,\"site_synced\":%d,\"hs_shed\":%llu,\"cookies_sent\":%llu,\"hs_nocookie\":%llu,\"hs_badmac2\":%llu,\"cookies_got\":%llu,\"registrations\":%llu,\"sb_rekeys\":%llu,\"switchboard_offers\":[%s],\"switchboard_codes\":[%s],\"relay\":{\"routes\":%d,\"forwarded\":%llu,\"dropped\":%llu,\"offer\":%s},\"peers\":[",
		kid, num, fullnum, nm, n->cfg.listed ? "true" : "false", sbs, refl, n->nat == 1 ? "easy" : n->nat == 2 ? "strict" : "unknown", mapped, cs, (int)(n->now - n->started), n->cfg.port_kind, n->cfg.port_arg, sw_count(n),
		(unsigned long long)n->local_rx, (unsigned long long)n->local_tx, (unsigned long long)n->flood,
		(unsigned long long)n->drop_garbage, (unsigned long long)n->drop_noroute,
		(unsigned long long)n->frames_split, (unsigned long long)n->frames_joined, (unsigned long long)n->drop_flood,
		(unsigned long long)n->arp_answered, (unsigned long long)n->dhcp_conflicts, (unsigned long long)n->drop_standby, (unsigned long long)n->resegmented, (unsigned long long)n->drop_oversize, (unsigned long long)n->cand_skipped, LW_VERSION, n->uudp >= 0 ? n->uudev : "", n->uudp >= 0 ? "open" : n->uudp == -2 ? "failed" : "unused", n->njnet, site_role(n), site_members(n), n->site_sync_n,
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
			"\"rx_frames\":%llu,\"tx_frames\":%llu,\"rx_bytes\":%llu,\"tx_bytes\":%llu,\"rekeys\":%llu,\"drop_auth\":%llu,\"drop_replay\":%llu}",
			any ? "," : "", kid, f, sess_state(p), p->relayed ? "relay" : "direct", ep,
			p->cur.valid ? (int)(n->now - p->cur.born) : -1,
			(unsigned long long)p->rx_frames, (unsigned long long)p->tx_frames,
			(unsigned long long)p->rx_bytes, (unsigned long long)p->tx_bytes,
			(unsigned long long)p->rekeys, (unsigned long long)p->drop_auth,
			(unsigned long long)p->drop_replay);
		any = 1;
	}
	len += snprintf(o + len, cap - (size_t)len, "],\"calls\":[");
	any = 0;
	for (i = 0; i < n->ncalls && len < (int)cap - 400; i++) {
		static const char *const SN[] = { "idle", "dialing", "ringing", "punching", "connecting", "up" };
		const lw_call *k = &n->calls[i];
		char f[16], hx[17], ph[64] = "", cn[2 * LW_NAME_MAX + 8];

		if (k->state == CALL_IDLE)
			continue;
		fmt_number(f, k->number);
		lw_hex(hx, k->id, 8);
		json_esc(cn, sizeof cn, k->name);
		if (k->state == CALL_RINGING && !k->we_caller) {	/* what a person is asked to compare before picking up */
			uint8_t ck[32];

			lw_h(ck, k->bundle, LW_BUNDLE);
			phrase_of(ph, sizeof ph, ck, n->id.key_id);
		}
		len += snprintf(o + len, cap - (size_t)len, "%s{\"call\":\"%s\",\"number\":\"%s\",\"state\":\"%s\",\"we_called\":%s,\"mesh\":%s,\"name\":\"%s\",\"phrase\":\"%s\"}",
				any ? "," : "", hx, f, SN[k->state], k->we_caller ? "true" : "false", k->auto_ ? "true" : "false", cn, ph);
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