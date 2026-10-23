/* SPDX-License-Identifier: GPL-3.0-or-later
 * Sessions: transport keys, data packets, replay window, rekey, keepalive
 * (docs/PROTOCOL.md §2-3). */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "int.h"
#include "proto.h"

#define REJECT_FACTOR 1.5

static void put64le(uint8_t *p, uint64_t v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t get64le(const uint8_t *p)
{
	uint64_t v = 0;
	int i;

	for (i = 7; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

uint32_t sess_new_idx(lw_node *n)
{
	uint32_t v;
	int i;

	for (;;) {
		lw_rand(&v, sizeof v);
		v &= 0xffffff;
		if (!v)
			continue;
		for (i = 0; i < n->maxpeers; i++) {
			lw_peer *p = &n->peers[i];

			if ((p->cur.valid && p->cur.rx_idx == v) || (p->prev.valid && p->prev.rx_idx == v) ||
			    (p->next.valid && p->next.rx_idx == v) || (p->hs.active && p->hs.my_idx == v))
				break;
		}
		if (i == n->maxpeers)
			return v;
	}
}

/* ck is the chaining key after the handshake; T1 keys initiator->responder. */
void sess_set_keys(lw_sess *s, int we_init, const uint8_t ck0[32], uint32_t rx_idx, uint32_t tx_idx, double now)
{
	uint8_t ck[32], k[32];

	memcpy(ck, ck0, 32);
	lw_kdf2(ck, 0, 0, k);
	memset(s, 0, sizeof *s);
	memcpy(s->k_send, we_init ? ck : k, 32);
	memcpy(s->k_recv, we_init ? k : ck, 32);
	s->we_init = we_init;
	s->rx_idx = rx_idx;
	s->tx_idx = tx_idx;
	s->born = now;
	s->valid = 1;
	crypto_wipe(ck, 32);
	crypto_wipe(k, 32);
}

static void nonce_of(uint8_t nonce[12], uint64_t ctr)
{
	memset(nonce, 0, 4);
	put64le(nonce + 4, ctr);
}

int sess_send(lw_node *n, lw_peer *p, uint8_t kind, const uint8_t *body, size_t len)
{
	lw_sess *s = &p->cur;
	uint8_t buf[12 + 1 + FRAME_MAX + 16], nonce[12];
	crypto_aead_ctx c;

	if (!s->valid || len > FRAME_MAX || !p->eplen)
		return -1;
	if (s->ctr_send >= ((uint64_t)1 << 60) || n->now - s->born > n->rekey_after * REJECT_FACTOR) {
		explicit_bzero(s, sizeof *s);		/* past its hard limit: erase the keys */
		return -1;
	}
	buf[0] = T_DATA;
	buf[1] = (uint8_t)s->tx_idx;
	buf[2] = (uint8_t)(s->tx_idx >> 8);
	buf[3] = (uint8_t)(s->tx_idx >> 16);
	put64le(buf + 4, s->ctr_send);
	buf[12] = kind;
	if (len)
		memcpy(buf + 13, body, len);
	nonce_of(nonce, s->ctr_send);
	crypto_aead_init_ietf(&c, s->k_send, nonce);
	crypto_aead_write(&c, buf + 12, buf + 12 + 1 + len, buf, 12, buf + 12, 1 + len);
	s->ctr_send++;
	peer_out(n, p, buf, 12 + 1 + len + 16);
	p->last_tx = n->now;
	p->tx_bytes += len;
	return 0;
}

/* Sliding window; call only after the tag has verified. */
static int replay_ok(lw_sess *s, uint64_t c)
{
	const uint64_t span = WIN_WORDS * 64;

	if (!s->got_any) {
		memset(s->win, 0, sizeof s->win);
		s->got_any = 1;
		s->rx_max = c;
		s->win[(c % span) / 64] |= (uint64_t)1 << (c % 64);
		return 1;
	}
	if (c > s->rx_max) {
		uint64_t d = c - s->rx_max, i;

		if (d >= span)
			memset(s->win, 0, sizeof s->win);
		else
			for (i = 1; i <= d; i++) {
				uint64_t b = (s->rx_max + i) % span;

				s->win[b / 64] &= ~((uint64_t)1 << (b % 64));
			}
		s->rx_max = c;
	} else if (s->rx_max - c >= span)
		return 0;
	{
		uint64_t b = c % span, m = (uint64_t)1 << (b % 64);

		if (s->win[b / 64] & m)
			return 0;
		s->win[b / 64] |= m;
	}
	return 1;
}

void sess_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl)
{
	uint8_t plain[1 + FRAME_MAX + 16], nonce[12];
	uint32_t idx;
	uint64_t ctr;
	lw_peer *p = 0;
	lw_sess *s = 0;
	crypto_aead_ctx c;
	size_t pl;
	int i;

	if (len < 12 + 1 + 16 || len > 12 + 1 + FRAME_MAX + 16) {
		n->drop_garbage++;
		return;
	}
	idx = pkt[1] | (uint32_t)pkt[2] << 8 | (uint32_t)pkt[3] << 16;
	for (i = 0; i < n->maxpeers && !s; i++) {
		lw_peer *q = &n->peers[i];

		if (!q->used)
			continue;
		if (q->cur.valid && q->cur.rx_idx == idx)
			s = &q->cur;
		else if (q->prev.valid && q->prev.rx_idx == idx)
			s = &q->prev;
		else if (q->next.valid && q->next.rx_idx == idx)
			s = &q->next;
		if (s)
			p = q;
	}
	if (!s) {
		n->drop_garbage++;
		return;
	}
	ctr = get64le(pkt + 4);
	pl = len - 12 - 16;
	nonce_of(nonce, ctr);
	crypto_aead_init_ietf(&c, s->k_recv, nonce);
	if (crypto_aead_read(&c, plain, pkt + len - 16, pkt, 12, pkt + 12, pl)) {
		p->drop_auth++;
		return;
	}
	if (!replay_ok(s, ctr)) {
		p->drop_replay++;
		return;
	}
	if (s == &p->next) {				/* first data from the initiator: confirmed */
		p->prev = p->cur;
		p->cur = p->next;
		explicit_bzero(&p->next, sizeof p->next);	/* the keys now live in cur */
		p->cur.confirmed = 1;
		p->rekeys++;
		s = &p->cur;
	}
	if (n->rx_relayed)				/* it came out of a relay: that says nothing about where the peer is */
		;
	else if (p->relayed || sl != p->eplen || memcmp(src, &p->ep, sl))	/* roaming: authenticated source */
		peer_heard_direct(n, p, src, sl);
	p->last_rx = n->now;
	if (plain[0] == K_CTL && pl >= 4) {
		size_t l = get16(plain + 2);

		if (l + 4 <= pl) {
			if (n->role)
				board_msg(n, p, plain[1], plain + 4, l);
			else
				node_msg(n, p, plain[1], plain + 4, l);
		}
	} else if (plain[0] == K_FRAG && !p->is_sb) {
		sw_fragment(n, (int)(p - n->peers), plain + 1, pl - 1);
	} else if (plain[0] == K_FRAME && !p->is_sb) {
		p->rx_frames++;
		p->rx_bytes += pl - 1;
		sw_from_peer(n, (int)(p - n->peers), plain + 1, pl - 1);
	}
}

void hs_start(lw_node *n, lw_peer *p)
{
	if (!p->eplen || !p->have_bundle)
		return;
	if (p->is_sb)				/* a switchboard has never heard of us: introduce ourselves first */
		hello_offer(n, (struct sockaddr *)&p->ep, p->eplen);
	if (hs_init_build(n, p))
		return;
	hs_send(n, T_HS_INIT, p->hs.hs_id, p->hs.msg, HS_INIT_LEN, p->key_id,
		(struct sockaddr *)&p->ep, p->eplen,
		p->cookie_t && n->now - p->cookie_t < 120.0 ? p->cookie : 0, p->hs.tag0, p);
	p->last_hs_try = n->now;
}

const char *sess_state(const lw_peer *p)
{
	if (p->cur.valid)
		return "up";
	if (p->hs.active)
		return "handshaking";
	return p->eplen ? "idle" : "waiting";
}

void sess_timers(lw_node *n)
{
	int i;

	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *p = &n->peers[i];
		lw_sess *c = &p->cur;
		double age;

		if (!p->used)
			continue;
		if (p->dial && !p->have_bundle && p->eplen && n->now >= p->hello_next) {
			int t = p->hello_tries++;

			hello_request(n, p);
			p->hello_next = n->now + (t < 3 ? (double)(1 << t) : 8.0);
			continue;
		}
		if (p->relayed && c->valid && p->probe_len && n->now < p->probe_until && n->now >= p->probe_next) {
			sess_send(n, p, K_KEEPALIVE, 0, 0);	/* goes both ways: a direct copy opens the path */
			p->probe_next = n->now + 0.5;
		}
		if (p->prev.valid && n->now - c->born > 10)
			explicit_bzero(&p->prev, sizeof p->prev);
		if (c->valid) {
			double heard = p->last_rx > c->born ? p->last_rx : c->born;
			double sent = p->last_tx > c->born ? p->last_tx : c->born;

			age = n->now - c->born;
			if (n->now - heard > 3.0 * n->keepalive + 1 || age > n->rekey_after * REJECT_FACTOR)
				explicit_bzero(c, sizeof *c);	/* dead peer, or keys past their hard limit: erase them */
			else if (n->now - sent >= n->keepalive)
				sess_send(n, p, K_KEEPALIVE, 0, 0);
		}
		/* start or retry a handshake: dialers when there's no session, the
		 * session's initiator when a rekey is due, the other side a bit later */
		if (p->hs.active) {
			if (n->now >= p->hs.next_retry) {
				int t = p->hs.tries + 1;
				double back = t < 4 ? (double)(1 << t) : 8.0;

				hs_start(n, p);
				p->hs.tries = t;
				p->hs.next_retry = n->now + back;
			}
		} else if (!c->valid) {
			if (p->dial && p->eplen && n->now - p->last_hs_try >= 1.0)
				hs_start(n, p);
		} else {
			double due = c->we_init ? n->rekey_after : n->rekey_after + 10.0;

			if (p->eplen && n->now - c->born >= due && n->now - p->last_hs_try >= 1.0)
				hs_start(n, p);
		}
	}
}

int ctl_msg_send(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *payload, size_t len)
{
	uint8_t b[3 + 2048];

	if (len > sizeof b - 3)
		return -1;
	b[0] = msg;
	put16(b + 1, (unsigned)len);
	if (len)
		memcpy(b + 3, payload, len);
	/* kind byte is added by sess_send; the 4-byte header is kind|msg|len16 */
	return sess_send(n, p, K_CTL, b, 3 + len);
}

lw_peer *peer_by_key(lw_node *n, const uint8_t key_id[32])
{
	int i;

	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !crypto_verify32(n->peers[i].key_id, key_id))
			return &n->peers[i];
	return 0;
}

/* A free slot; when the table is full, the stalest peer with no session is
 * recycled (never one that is up, never our switchboard). */
static lw_peer *peer_slot(lw_node *n)
{
	lw_peer *best = 0;
	int i;

	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *p = &n->peers[i];

		if (!p->used)
			return p;
		if (!p->cur.valid && !p->next.valid && !p->hs.active && !p->is_sb && !p->dial &&
		    (!best || p->touched < best->touched))
			best = p;
	}
	if (best)
		peer_drop(n, best);
	return best;
}

void peer_drop(lw_node *n, lw_peer *p)
{
	int idx = (int)(p - n->peers), i;

	free(p->fr);
	relay_peer_gone(n, idx);

	for (i = 0; i < MAC_SLOTS; i++)		/* whatever was learned behind this peer is no longer reachable */
		if (n->macs[i].port == idx)
			n->macs[i].port = -2;
	explicit_bzero(p, sizeof *p);
}

lw_peer *peer_add(lw_node *n, const uint8_t bundle[LW_BUNDLE])
{
	lw_peer *p;

	if (bundle[0] != 0x01 || !(p = peer_slot(n)))
		return 0;
	memset(p, 0, sizeof *p);
	memcpy(p->bundle, bundle, LW_BUNDLE);
	p->x_pk = p->bundle + 1;
	p->kem_pk = p->bundle + 1 + 32;
	lw_h(p->key_id, p->bundle, LW_BUNDLE);
	p->have_bundle = 1;
	p->used = 1;
	p->touched = n->now;
	return p;
}

lw_peer *peer_add_keyid(lw_node *n, const uint8_t key_id[32])
{
	lw_peer *p = peer_slot(n);

	if (!p)
		return 0;
	memset(p, 0, sizeof *p);
	memcpy(p->key_id, key_id, 32);
	p->x_pk = p->bundle + 1;
	p->kem_pk = p->bundle + 1 + 32;
	p->used = 1;
	p->touched = n->now;
	return p;
}

/* Switchboard: accept a stranger only if it offered its bundle just before. */
lw_peer *peer_for_unknown(lw_node *n, const uint8_t key_id[32])
{
	int i;

	if (!n->role)
		return 0;
	for (i = 0; i < OFFERS; i++) {
		lw_offer *o = &n->offers[i];

		if (o->used && n->now - o->t < 10.0 && !crypto_verify32(o->key_id, key_id)) {
			lw_peer *p = peer_add(n, o->bundle);

			o->used = 0;
			return p;
		}
	}
	return 0;
}
