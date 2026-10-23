/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Relaying: two members that cannot reach each other directly carry their
 * packets through a third (docs/PROTOCOL.md §5).
 *
 * The relay sits entirely below sessions. A relayed pair still runs its own
 * end-to-end handshake and session; the relay only forwards sealed datagrams
 * wrapped as RELAY | route(3) | inner, and can see nothing but their size and
 * timing. A relayed peer keeps trying for a direct path and drops the relay
 * the moment an authenticated datagram arrives straight from the other end. */
#include "int.h"
#include <ifaddrs.h>
#include <net/if.h>

#define ROUTE_IDLE 60.0

const lw_member *line_member(lw_node *n, const uint8_t kid[32])
{
	int i;

	for (i = 0; i < n->line.nm; i++)
		if (!memcmp(n->line.m[i].key_id, kid, 32))
			return &n->line.m[i];
	return 0;
}

/* Is this node reachable from the internet as it stands: an address it holds is the address the switchboard saw. */
int node_public(lw_node *n)
{
	struct ifaddrs *ifa, *i;
	int pub = 0;

	if (!n->have_refl || getifaddrs(&ifa))
		return 0;
	for (i = ifa; i && !pub; i = i->ifa_next) {
		const struct sockaddr_in *a;

		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK))
			continue;
		a = (const void *)i->ifa_addr;
		pub = !memcmp(&a->sin_addr, n->refl.ip, 4);
	}
	freeifaddrs(ifa);
	return pub;
}

int relay_offered(lw_node *n)
{
	return n->cfg.relay == 1 || (n->cfg.relay == 0 && node_public(n));
}

int relay_live(const lw_node *n)
{
	int i, c = 0;

	for (i = 0; i < 64; i++)
		c += n->routes[i].used;
	return c;
}

/* The way out to a peer: straight, or wrapped through its relay. While a relayed peer probes a direct address,
 * each packet goes both ways and the receiver's replay window drops the second copy. */
void peer_out(lw_node *n, lw_peer *p, const uint8_t *buf, size_t len)
{
	uint8_t w[4 + 2304];
	lw_peer *r;

	if (!p->relayed) {
		sendto(n->udp, buf, len, 0, (struct sockaddr *)&p->ep, p->eplen);
		return;
	}
	if (p->probe_len && n->now < p->probe_until)
		sendto(n->udp, buf, len, 0, (struct sockaddr *)&p->probe_ep, p->probe_len);
	r = &n->peers[p->via_peer];
	if (!r->used || memcmp(r->key_id, p->via_kid, 32) || len > sizeof w - 4)
		return;
	w[0] = T_RELAY;
	w[1] = (uint8_t)p->via_route;
	w[2] = (uint8_t)(p->via_route >> 8);
	w[3] = (uint8_t)(p->via_route >> 16);
	memcpy(w + 4, buf, len);
	sendto(n->udp, w, 4 + len, 0, (struct sockaddr *)&r->ep, r->eplen);
}

/* An authenticated datagram came from the other end itself: whatever path it had, it has this one now. */
void peer_heard_direct(lw_node *n, lw_peer *p, const struct sockaddr *src, socklen_t sl)
{
	char f[16], ep[64];

	memcpy(&p->ep, src, sl);
	p->eplen = sl;
	if (!p->relayed)
		return;
	p->relayed = 0;
	p->probe_len = 0;
	p->direct_tries = 0;
	fmt_number(f, p->number);
	addr_str(&p->ep, ep, sizeof ep);
	ev_emit(n, "{\"event\":\"path\",\"number\":\"%s\",\"path\":\"direct\",\"ep\":\"%s\"}", f, ep);
}

static lw_route *route_find(lw_node *n, uint32_t id)
{
	int i;

	for (i = 0; i < 64; i++)
		if (n->routes[i].used && n->routes[i].id == id)
			return &n->routes[i];
	return 0;
}

static uint32_t route_fresh(lw_node *n)
{
	uint8_t b[3];
	uint32_t id;
	int i;

	for (;;) {
		lw_rand(b, 3);
		id = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16;
		if (!id || route_find(n, id))
			continue;
		for (i = 0; i < n->maxpeers; i++)
			if (n->peers[i].used && n->peers[i].relayed && n->peers[i].via_route == id)
				break;
		if (i == n->maxpeers)
			return id;
	}
}

static lw_route *route_slot(lw_node *n)
{
	int i;

	for (i = 0; i < 64; i++)
		if (!n->routes[i].used)
			return &n->routes[i];
	return 0;
}

void relay_peer_gone(lw_node *n, int idx)
{
	int i;

	for (i = 0; i < 64; i++)
		if (n->routes[i].used && (n->routes[i].owner == idx || n->routes[i].remote == idx))
			memset(&n->routes[i], 0, sizeof n->routes[i]);
}

void relay_timers(lw_node *n)
{
	int i;

	for (i = 0; i < 64; i++)
		if (n->routes[i].used && n->now - n->routes[i].last > ROUTE_IDLE)
			memset(&n->routes[i], 0, sizeof n->routes[i]);
}

/* A RELAY datagram: at a relay, forward it; at an end of a relayed pair, take the inner datagram out. */
void relay_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl)
{
	uint32_t id;
	lw_route *r;
	int i;

	if (len < 4 + 13 || pkt[4] == T_RELAY) {		/* nothing smaller than a data header, and no wrapping in wrapping */
		n->drop_garbage++;
		return;
	}
	id = (uint32_t)pkt[1] | (uint32_t)pkt[2] << 8 | (uint32_t)pkt[3] << 16;
	if ((r = route_find(n, id))) {			/* we are the relay */
		lw_peer *from = &n->peers[r->owner], *to = &n->peers[r->remote];
		uint8_t w[4 + 2304];

		if (!from->used || !to->used || !to->cur.valid || sl != from->eplen || memcmp(src, &from->ep, sl) || len > sizeof w) {
			n->drop_garbage++;
			return;
		}
		r->tok += (n->now - r->tok_t) * 10000.0;
		r->tok_t = n->now;
		if (r->tok > 2000.0)
			r->tok = 2000.0;
		if (r->tok < 1.0) {
			n->relay_drop++;
			return;
		}
		r->tok -= 1.0;
		r->last = n->now;
		memcpy(w, pkt, len);
		w[1] = (uint8_t)r->remote_id;
		w[2] = (uint8_t)(r->remote_id >> 8);
		w[3] = (uint8_t)(r->remote_id >> 16);
		sendto(n->udp, w, len, 0, (struct sockaddr *)&to->ep, to->eplen);
		n->relay_fwd++;
		return;
	}
	for (i = 0; i < n->maxpeers; i++) {		/* we are an end: the route names the peer, and it must come from that peer's relay */
		lw_peer *p = &n->peers[i], *v;

		if (!p->used || !p->relayed || p->via_route != id)
			continue;
		v = &n->peers[p->via_peer];
		if (!v->used || memcmp(v->key_id, p->via_kid, 32) || sl != v->eplen || memcmp(src, &v->ep, sl))
			continue;
		n->rx_relayed = 1;
		node_dispatch(n, pkt + 4, len - 4, src, sl);
		n->rx_relayed = 0;
		return;
	}
	n->drop_garbage++;
}

/* ---- the requests -------------------------------------------------------------- */

void relay_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len)
{
	if (!p->linked || !line_member(n, p->key_id))	/* only members of our own line */
		return;
	if (msg == M_RELAY_REQ && len >= 32) {
		lw_peer *a = p, *c = peer_by_key(n, pl);
		lw_route *ra, *rc;
		uint8_t b[1 + 32 + 3 + LW_BUNDLE];
		uint32_t ia, ic;

		if (!relay_offered(n) || !c || c == a || !c->linked || !c->cur.valid || c->relayed || a->relayed ||
		    !(ra = route_slot(n))) {
			ctl_msg_send(n, a, M_RELAY_NO, pl, 32);
			return;
		}
		ra->used = 1;				/* claim the first slot before looking for the second */
		if (!(rc = route_slot(n))) {
			ra->used = 0;
			ctl_msg_send(n, a, M_RELAY_NO, pl, 32);
			return;
		}
		ra->id = 0;
		ra->id = ia = route_fresh(n);
		rc->used = 1;
		rc->id = 0;
		rc->id = ic = route_fresh(n);
		ra->owner = (int)(a - n->peers);
		ra->remote = (int)(c - n->peers);
		ra->remote_id = ic;
		rc->owner = ra->remote;
		rc->remote = ra->owner;
		rc->remote_id = ia;
		ra->last = rc->last = n->now;
		ra->tok = rc->tok = 2000.0;
		ra->tok_t = rc->tok_t = n->now;
		b[0] = 1;				/* a dials */
		memcpy(b + 1, c->key_id, 32);
		b[33] = (uint8_t)ia; b[34] = (uint8_t)(ia >> 8); b[35] = (uint8_t)(ia >> 16);
		memcpy(b + 36, c->bundle, LW_BUNDLE);
		ctl_msg_send(n, a, M_RELAY_OK, b, sizeof b);
		b[0] = 0;
		memcpy(b + 1, a->key_id, 32);
		b[33] = (uint8_t)ic; b[34] = (uint8_t)(ic >> 8); b[35] = (uint8_t)(ic >> 16);
		memcpy(b + 36, a->bundle, LW_BUNDLE);
		ctl_msg_send(n, c, M_RELAY_OK, b, sizeof b);
		n->relay_routes++;
	} else if (msg == M_RELAY_OK && len >= 36 + LW_BUNDLE) {
		const lw_member *m;
		uint8_t kid[32];
		lw_peer *q;

		lw_h(kid, pl + 36, LW_BUNDLE);
		if (memcmp(kid, pl + 1, 32) || !(m = line_member(n, kid)) || line_evicted(n, kid) || !memcmp(kid, n->id.key_id, 32))
			return;
		if (peer_by_key(n, kid))		/* already in touch, or a direct handshake is under way */
			return;
		if (!(q = peer_add(n, pl + 36)))
			return;
		memcpy(q->number, m->number, NUMLEN + 1);
		q->relayed = 1;
		q->via_peer = (int)(p - n->peers);
		memcpy(q->via_kid, p->key_id, 32);
		q->via_route = (uint32_t)pl[33] | (uint32_t)pl[34] << 8 | (uint32_t)pl[35] << 16;
		memcpy(&q->ep, &p->ep, p->eplen);	/* a placeholder: nothing is sent there while relayed */
		q->eplen = p->eplen;
		q->dial = pl[0] == 1;
		q->next_direct = n->now + n->cfg.direct_retry;
	}
	/* RELAY_NO needs no action: the maintainer asks the next member in a few seconds */
}

/* Ask one connected member to carry our packets to `member_kid`. */
int relay_request(lw_node *n, const uint8_t member_kid[32], int *cursor)
{
	int i, k;

	for (k = 0; k < n->maxpeers; k++) {
		lw_peer *r = &n->peers[(*cursor + k) % n->maxpeers];

		if (!r->used || r->is_sb || !r->linked || !r->cur.valid || r->relayed || !memcmp(r->key_id, member_kid, 32))
			continue;
		i = (int)(r - n->peers);
		*cursor = i + 1;
		ctl_msg_send(n, r, M_RELAY_REQ, member_kid, 32);
		return 1;
	}
	return 0;
}
