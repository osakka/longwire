/* SPDX-License-Identifier: GPL-3.0-or-later
 * The switch: learn, forward, flood, split horizon (docs/ARCHITECTURE.md §1). */
#include <string.h>
#include "int.h"

#define MAC_AGE 300.0
#define MAC_PROBES 8

static uint32_t mac_hash(const uint8_t *m)
{
	uint32_t h = 2166136261u;
	int i;

	for (i = 0; i < 6; i++)
		h = (h ^ m[i]) * 16777619u;
	return h;
}

static mac_ent *mac_find(lw_node *n, const uint8_t *mac, int create)
{
	uint32_t h = mac_hash(mac);
	mac_ent *oldest = 0;
	int i;

	for (i = 0; i < MAC_PROBES; i++) {
		mac_ent *e = &n->macs[(h + (uint32_t)i) % MAC_SLOTS];

		if (e->port != -2 && n->now - e->seen > MAC_AGE)
			e->port = -2;
		if (e->port != -2 && !memcmp(e->mac, mac, 6))
			return e;
		if (!create)
			continue;
		if (e->port == -2) {
			memcpy(e->mac, mac, 6);
			return e;
		}
		if (!oldest || e->seen < oldest->seen)
			oldest = e;
	}
	if (create && oldest) {
		memcpy(oldest->mac, mac, 6);
		return oldest;
	}
	return 0;
}

static void learn(lw_node *n, const uint8_t *mac, int port)
{
	mac_ent *e;

	if (mac[0] & 1)				/* a group address is never a source */
		return;
	if ((e = mac_find(n, mac, 1))) {
		e->port = (int16_t)port;
		e->seen = n->now;
	}
}

static void to_peer(lw_node *n, int pi, const uint8_t *f, size_t len)
{
	lw_peer *p = &n->peers[pi];

	if (!p->is_sb && p->cur.valid && !sess_send(n, p, K_FRAME, f, len))
		p->tx_frames++;
}

int sw_count(const lw_node *n)
{
	int i, c = 0;

	for (i = 0; i < MAC_SLOTS; i++)
		if (n->macs[i].port != -2 && n->now - n->macs[i].seen <= MAC_AGE)
			c++;
	return c;
}

void sw_from_local(lw_node *n, const uint8_t *f, size_t len)
{
	mac_ent *e;
	int i;

	if (len < 14 || len > FRAME_MAX) {
		n->drop_garbage++;
		return;
	}
	n->local_rx++;
	learn(n, f + 6, -1);
	if (!(f[0] & 1) && (e = mac_find(n, f, 0))) {
		if (e->port == -1)
			return;			/* local to local: never leaves the site */
		to_peer(n, e->port, f, len);
		return;
	}
	n->flood++;
	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used)
			to_peer(n, i, f, len);
}

void sw_from_peer(lw_node *n, int pi, const uint8_t *f, size_t len)
{
	mac_ent *e;

	if (len < 14) {
		n->drop_garbage++;
		return;
	}
	learn(n, f + 6, pi);
	if (!(f[0] & 1) && (e = mac_find(n, f, 0)) && e->port >= 0) {
		n->drop_noroute++;		/* split horizon: the sender reached that peer itself */
		return;
	}
	n->local_tx++;
	port_write(n, f, len);
}
