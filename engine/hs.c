/* SPDX-License-Identifier: GPL-3.0-or-later
 * The handshake: hybrid Noise-IK with ML-KEM (docs/CRYPTO.md §4) and its
 * fragmentation (docs/PROTOCOL.md §3). Message layouts are fixed:
 *   INIT  E32 ct_R1088 EK1184 c_id48 c_pay32 mac1 16 mac2 16        = 2416
 *   RESP  E'32 ct_E1088 ct_I1088 c_pay20 mac1 16 mac2 16            = 2260
 */
#define _GNU_SOURCE
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include "int.h"
#include "../vendor/mlkem768/kem.h"

#define KEM_KEYPAIR PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair
#define KEM_ENC     PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc
#define KEM_DEC     PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec

static const char HS_NAME[] = "longwire/1 pqIK X25519+MLKEM768 ChaChaPoly BLAKE2b";

static void mix(uint8_t h[32], const void *d, size_t n)
{
	uint8_t t[32];

	lw_h2(t, h, 32, d, n);
	memcpy(h, t, 32);
}

static void start(uint8_t ck[32], uint8_t h[32], const uint8_t responder_key_id[32])
{
	lw_h(ck, HS_NAME, sizeof HS_NAME - 1);
	lw_h2(h, ck, 32, responder_key_id, 32);
}

/* X25519 that refuses the all-zero (low-order point) result. */
static int dh(uint8_t out[32], const uint8_t sk[32], const uint8_t pk[32])
{
	uint8_t z = 0;
	int i;

	crypto_x25519(out, sk, pk);
	for (i = 0; i < 32; i++)
		z |= out[i];
	return z ? 0 : -1;
}

static void mac1_key(uint8_t out[32], const uint8_t key_id[32])
{
	lw_h2(out, "mac1", 4, key_id, 32);
}

static void mac16(uint8_t out[16], const uint8_t key[32], const uint8_t *m, size_t n)
{
	crypto_blake2b_keyed(out, 16, key, 32, m, n);
}

static void tai64n(uint8_t out[12])
{
	struct timespec t;
	uint64_t s;
	int i;

	clock_gettime(CLOCK_REALTIME, &t);
	s = ((uint64_t)1 << 62) + 10 + (uint64_t)t.tv_sec;
	for (i = 0; i < 8; i++)
		out[i] = (uint8_t)(s >> (56 - 8 * i));
	for (i = 0; i < 4; i++)
		out[8 + i] = (uint8_t)((uint32_t)t.tv_nsec >> (24 - 8 * i));
}

static void put24(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
}

static uint32_t get24(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
}

int hs_init_build(lw_node *n, lw_peer *p)
{
	lw_hsi *hs = &p->hs;
	uint8_t *m = hs->msg, ck[32], h[32], k[32], t[32], s1[32], EK[LW_MLKEM_PK];
	uint8_t pay[16], e[32], mk[32];

	memset(hs, 0, sizeof *hs);
	start(ck, h, p->key_id);
	lw_rand(e, 32);
	crypto_x25519_public_key(m, e);			/* E */
	KEM_KEYPAIR(EK, hs->ek);
	mix(h, m, 32);
	lw_kdf1(ck, m, 32);
	if (dh(t, e, p->x_pk))				/* es */
		return -1;
	lw_kdf2(ck, t, 32, k);
	KEM_ENC(m + 32, s1, p->kem_pk);			/* skem(R) */
	mix(h, m + 32, LW_MLKEM_CT);
	lw_kdf2(ck, s1, 32, k);
	memcpy(m + 1120, EK, LW_MLKEM_PK);
	mix(h, EK, LW_MLKEM_PK);
	lw_seal0(m + 2304, k, n->id.key_id, 32, h);	/* c_id: I's identity, sealed */
	mix(h, m + 2304, 48);
	if (dh(t, n->id.x_sk, p->x_pk))			/* ss */
		return -1;
	lw_kdf2(ck, t, 32, k);
	hs->my_idx = sess_new_idx(n);
	tai64n(pay);				/* tai64n(12) | index(3) | flags(1) */
	put24(pay + 12, hs->my_idx);
	pay[15] = 0;
	lw_seal0(m + 2352, k, pay, 16, h);
	mix(h, m + 2352, 32);
	mac1_key(mk, p->key_id);
	mac16(m + 2384, mk, m, 2384);
	memset(m + 2400, 0, 16);			/* mac2: cookies come later */
	memcpy(hs->ck, ck, 32);
	memcpy(hs->h, h, 32);
	memcpy(hs->e, e, 32);
	hs->active = 1;
	hs->sent = n->now;
	hs->next_retry = n->now + 1.0;
	lw_rand(&hs->hs_id, sizeof hs->hs_id);
	crypto_wipe(e, 32);
	crypto_wipe(k, 32);
	crypto_wipe(s1, 32);
	crypto_wipe(t, 32);
	return 0;
}

int hs_init_consume(lw_node *n, const uint8_t *m, size_t len, uint16_t hs_id, const struct sockaddr *src, socklen_t sl)
{
	uint8_t ck[32], h[32], k[32], t[32], s1[32], s2[32], s3[32], mac[16], mk[32];
	uint8_t kid[32], pl[16], resp[HS_RESP_LEN], e2[32];
	uint32_t their_idx, my_idx;
	lw_peer *p;
	lw_sess *ns;

	if (len != HS_INIT_LEN)
		return -1;
	mac16(mac, n->mac1_key, m, 2384);
	if (crypto_verify16(mac, m + 2384))
		return -1;
	{	/* each INIT from here on costs a KEM decapsulation: shed load rather than melt */
		double rate = n->role ? 2000.0 : 50.0;

		n->hs_tokens += (n->now - n->hs_t) * rate;
		if (n->hs_tokens > rate)
			n->hs_tokens = rate;
		n->hs_t = n->now;
		if (n->hs_tokens < 1.0) {
			n->hs_shed++;
			return -1;
		}
		n->hs_tokens -= 1.0;
	}
	start(ck, h, n->id.key_id);
	mix(h, m, 32);
	lw_kdf1(ck, m, 32);
	if (dh(t, n->id.x_sk, m))				/* es */
		return -1;
	lw_kdf2(ck, t, 32, k);
	KEM_DEC(s1, m + 32, n->id.kem_sk);			/* skem(R) */
	mix(h, m + 32, LW_MLKEM_CT);
	lw_kdf2(ck, s1, 32, k);
	mix(h, m + 1120, LW_MLKEM_PK);
	if (lw_open0(kid, k, m + 2304, 48, h))
		return -1;
	mix(h, m + 2304, 48);
	if (!(p = peer_by_key(n, kid)) && !(p = peer_for_unknown(n, kid)))
		return -1;					/* not someone we know */
	if (!p->have_bundle)
		return -1;
	if (dh(t, n->id.x_sk, p->x_pk))				/* ss */
		return -1;
	lw_kdf2(ck, t, 32, k);
	if (lw_open0(pl, k, m + 2352, 32, h))
		return -1;
	mix(h, m + 2352, 32);
	if (memcmp(pl, p->last_tai, 12) <= 0)			/* replayed INIT */
		return -1;
	memcpy(p->last_tai, pl, 12);
	their_idx = get24(pl + 12);

	/* response */
	lw_rand(e2, 32);
	crypto_x25519_public_key(resp, e2);			/* E' */
	mix(h, resp, 32);
	lw_kdf1(ck, resp, 32);
	if (dh(t, e2, m))					/* ee */
		return -1;
	lw_kdf1(ck, t, 32);
	if (dh(t, e2, p->x_pk))					/* se */
		return -1;
	lw_kdf1(ck, t, 32);
	KEM_ENC(resp + 32, s2, m + 1120);			/* ekem: PQ forward secrecy */
	mix(h, resp + 32, LW_MLKEM_CT);
	lw_kdf1(ck, s2, 32);
	KEM_ENC(resp + 1120, s3, p->kem_pk);			/* skem(I) */
	mix(h, resp + 1120, LW_MLKEM_CT);
	lw_kdf1(ck, s3, 32);
	lw_kdf2(ck, 0, 0, k);
	my_idx = sess_new_idx(n);
	put24(pl, my_idx);
	pl[3] = 0;
	lw_seal0(resp + 2208, k, pl, 4, h);
	mac1_key(mk, p->key_id);
	mac16(resp + 2228, mk, resp, 2228);
	memset(resp + 2244, 0, 16);

	ns = &p->next;
	memset(ns, 0, sizeof *ns);
	sess_set_keys(ns, 0, ck, my_idx, their_idx, n->now);
	if (src->sa_family) {
		memcpy(&p->ep, src, sl);			/* authenticated: moves the endpoint */
		p->eplen = sl;
	}
	hs_send(n, T_HS_RESP, hs_id, resp, HS_RESP_LEN,
		p->key_id, (struct sockaddr *)&p->ep, p->eplen);
	crypto_wipe(e2, 32); crypto_wipe(k, 32); crypto_wipe(t, 32); crypto_wipe(ck, 32);
	crypto_wipe(s1, 32); crypto_wipe(s2, 32); crypto_wipe(s3, 32);
	return 0;
}

int hs_resp_consume(lw_node *n, const uint8_t *m, size_t len, uint16_t hs_id, const struct sockaddr *src, socklen_t sl)
{
	uint8_t ck[32], h[32], k[32], t[32], s2[32], s3[32], mac[16], pl[4];
	uint32_t their_idx;
	lw_peer *p = 0;
	lw_hsi *hs = 0;
	lw_sess ns;
	int i;

	(void)src; (void)sl;
	if (len != HS_RESP_LEN)
		return -1;
	mac16(mac, n->mac1_key, m, 2228);
	if (crypto_verify16(mac, m + 2228))
		return -1;
	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && n->peers[i].hs.active && n->peers[i].hs.hs_id == hs_id) {
			p = &n->peers[i];
			hs = &p->hs;
			break;
		}
	if (!p)
		return -1;
	memcpy(ck, hs->ck, 32);
	memcpy(h, hs->h, 32);
	mix(h, m, 32);
	lw_kdf1(ck, m, 32);
	if (dh(t, hs->e, m))					/* ee */
		return -1;
	lw_kdf1(ck, t, 32);
	if (dh(t, n->id.x_sk, m))				/* se */
		return -1;
	lw_kdf1(ck, t, 32);
	KEM_DEC(s2, m + 32, hs->ek);				/* ekem */
	mix(h, m + 32, LW_MLKEM_CT);
	lw_kdf1(ck, s2, 32);
	KEM_DEC(s3, m + 1120, n->id.kem_sk);			/* skem(I) */
	mix(h, m + 1120, LW_MLKEM_CT);
	lw_kdf1(ck, s3, 32);
	lw_kdf2(ck, 0, 0, k);
	if (lw_open0(pl, k, m + 2208, 20, h)) {
		/* not the response to this attempt; leave the handshake running */
		return -1;
	}
	their_idx = get24(pl);
	memset(&ns, 0, sizeof ns);
	sess_set_keys(&ns, 1, ck, hs->my_idx, their_idx, n->now);
	ns.confirmed = 1;
	if (p->cur.valid) {
		p->prev = p->cur;
		p->rekeys++;
	}
	p->cur = ns;
	p->next.valid = 0;
	crypto_wipe(hs->ek, sizeof hs->ek);
	crypto_wipe(hs->e, 32);
	hs->active = 0;
	sess_send(n, p, K_KEEPALIVE, 0, 0);			/* key confirmation */
	crypto_wipe(k, 32); crypto_wipe(t, 32); crypto_wipe(ck, 32);
	crypto_wipe(s2, 32); crypto_wipe(s3, 32);
	return 0;
}

/* Fragments carry a 16-byte tag keyed by the receiver's mac1 key, so a
 * responder can discard unauthenticated fragments before buffering anything. */
void hs_send(lw_node *n, int type, uint16_t hs_id, const uint8_t *m, size_t len,
	     const uint8_t peer_key_id[32], const struct sockaddr *dst, socklen_t dl)
{
	uint8_t pkt[4 + HS_CHUNK + 16], mk[32];
	int count = (int)((len + HS_CHUNK - 1) / HS_CHUNK), i;

	mac1_key(mk, peer_key_id);
	for (i = 0; i < count; i++) {
		size_t off = (size_t)i * HS_CHUNK, c = len - off > HS_CHUNK ? HS_CHUNK : len - off;

		pkt[0] = (uint8_t)type;
		pkt[1] = (uint8_t)hs_id;
		pkt[2] = (uint8_t)(hs_id >> 8);
		pkt[3] = (uint8_t)(i << 4 | count);
		memcpy(pkt + 4, m + off, c);
		mac16(pkt + 4 + c, mk, pkt, 4 + c);
		sendto(n->udp, pkt, 4 + c + 16, 0, dst, dl);
	}
}

static hs_slot *slot_for(lw_node *n, const struct sockaddr *src, socklen_t sl, int type, uint16_t id)
{
	int i, free_i = -1, old_i = 0;

	for (i = 0; i < 8; i++) {
		hs_slot *s = &n->slots[i];

		if (s->used && n->now - s->t > 1.0)
			s->used = 0;
		if (s->used && s->srclen == sl && !memcmp(&s->src, src, sl)) {
			if (s->type == type && s->hs_id == id)
				return s;
			s->used = 0;			/* one in flight per source */
			free_i = i;
			break;
		}
		if (!s->used && free_i < 0)
			free_i = i;
		if (s->used && s->t < n->slots[old_i].t)
			old_i = i;
	}
	if (free_i < 0)
		free_i = old_i;
	{
		hs_slot *s = &n->slots[free_i];

		memset(s, 0, sizeof *s);
		s->used = 1;
		s->t = n->now;
		memcpy(&s->src, src, sl);
		s->srclen = sl;
		s->type = (uint8_t)type;
		s->hs_id = id;
		return s;
	}
}

void hs_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl)
{
	uint8_t mac[16], msg[HS_CHUNK * 2];
	int count, idx, type = pkt[0];
	uint16_t id;
	hs_slot *s;
	size_t c;

	if (len < 4 + 16 || len > 4 + HS_CHUNK + 16)
		goto bad;
	mac16(mac, n->mac1_key, pkt, len - 16);
	if (crypto_verify16(mac, pkt + len - 16))
		goto bad;
	id = (uint16_t)(pkt[1] | pkt[2] << 8);
	idx = pkt[3] >> 4;
	count = pkt[3] & 15;
	if (count < 1 || count > 2 || idx >= count)
		goto bad;
	c = len - 4 - 16;
	s = slot_for(n, src, sl, type, id);
	if (s->count && s->count != count)
		goto bad;
	s->count = count;
	if (!(s->got & (1 << idx))) {
		memcpy(s->buf[idx], pkt + 4, c);
		s->len[idx] = (int)c;
		s->got |= 1 << idx;
	}
	if (s->got != (1 << count) - 1)
		return;
	memcpy(msg, s->buf[0], (size_t)s->len[0]);
	c = (size_t)s->len[0];
	if (count == 2) {
		memcpy(msg + c, s->buf[1], (size_t)s->len[1]);
		c += (size_t)s->len[1];
	}
	s->used = 0;
	if (type == T_HS_INIT) {
		if (hs_init_consume(n, msg, c, id, src, sl))
			n->drop_garbage++;
	} else if (hs_resp_consume(n, msg, c, id, src, sl))
		n->drop_garbage++;
	return;
bad:
	n->drop_garbage++;
}
