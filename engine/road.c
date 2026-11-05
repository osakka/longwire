/* SPDX-License-Identifier: GPL-3.0-or-later
 * The road under the node (docs/adr/0061): the networks the machine reaches the world by. Looked at once a second, whether or not a packet flows, by a
 * fingerprint of the default route and of every address on a device that is up (the jack and the loopback aside). When it changes, the node does not wait
 * for its sessions to die of silence (three keepalives): it says the same thing to every peer at once, from the new road, so that an authenticated packet from
 * a new address moves the far end's idea of where we are (docs/adr/0046's roaming, as a session does it), and it makes every handshake that was backing off
 * try now. When the road is gone, which it was not before, the node goes quiet: nothing is sent into the void, and nothing is retried until it is back. */
#include "int.h"
#include <ifaddrs.h>
#include <net/if.h>

static uint64_t fp_mix(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = p;

	while (n--)
		h = (h ^ *b++) * 1099511628211ull;
	return h;
}

/* A fingerprint of the road, order-independent, and whether there is a road at all: a default route that is not the jack's, or an address on a device that
 * is up. (A machine on a LAN with no default route has a road; one whose link is down, or whose addresses are gone, has none.) */
static uint64_t road_fp(lw_node *n, int *has_road)
{
	struct ifaddrs *ifa, *i;
	uint64_t fp = 0;
	char dev[32];

	*has_road = underlay_dev(n, dev, sizeof dev);
	if (*has_road)
		fp += fp_mix(1469598103934665603ull, dev, strlen(dev));
	if (getifaddrs(&ifa))
		return fp;
	for (i = ifa; i; i = i->ifa_next) {
		uint64_t h = 1469598103934665603ull;

		if (!i->ifa_addr || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK) || !strcmp(i->ifa_name, n->cfg.port_arg))
			continue;
		if (i->ifa_addr->sa_family == AF_INET)
			h = fp_mix(h, &((const struct sockaddr_in *)i->ifa_addr)->sin_addr, 4);
		else if (i->ifa_addr->sa_family == AF_INET6 && !IN6_IS_ADDR_LINKLOCAL(&((const struct sockaddr_in6 *)i->ifa_addr)->sin6_addr))
			h = fp_mix(h, &((const struct sockaddr_in6 *)i->ifa_addr)->sin6_addr, 16);
		else
			continue;
		fp += fp_mix(h, i->ifa_name, strlen(i->ifa_name));
		*has_road = 1;
	}
	freeifaddrs(ifa);
	return fp;
}

/* The road changed: tell every peer now, from the new road, and let every handshake that was backing off try again. */
static void road_kick(lw_node *n)
{
	int i;

	n->next_hb = n->now;				/* the switchboard learns our new address at once */
	n->next_reg = n->now;
	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *p = &n->peers[i];

		if (!p->used)
			continue;
		if (p->cur.valid)
			sess_send(n, p, K_KEEPALIVE, 0, 0);	/* authenticated, so the far end follows our address (sess.c) */
		else if (p->dial) {
			p->hs.tries = 0;
			p->hs.next_retry = n->now;
			p->last_hs_try = 0;
		}
	}
	for (i = 0; i < n->line.nm; i++) {		/* the mesh redials its members at once, not after the back-off it had earned */
		n->line.m[i].tries = 0;
		n->line.m[i].next_try = 0;
	}
}

void road_timers(lw_node *n)
{
	int has;
	uint64_t fp;

	if (n->role || (n->now - n->road_t < 1.0 && n->now >= n->road_t))
		return;
	n->road_t = n->now;
	road_check(n);					/* the jack's own socket follows the road too (docs/adr/0046) */
	fp = road_fp(n, &has);
	if (!n->road_seen) {
		n->road_seen = 1;
		n->road_fp = fp;
		n->road_had = has;
		return;
	}
	if (fp == n->road_fp)
		return;
	n->road_fp = fp;
	n->road_changes++;
	n->road_changed_at = n->now;
	if (!has && n->road_had) {
		n->road_down = 1;			/* there was a road and now there is none: quiet until one is back */
		ev_emit(n, "{\"event\":\"road-changed\",\"road\":\"down\"}");
		return;
	}
	if (has)
		n->road_had = 1;
	ev_emit(n, "{\"event\":\"road-changed\",\"road\":\"%s\"}", n->road_down ? "back" : "changed");
	n->road_down = 0;
	road_kick(n);
}

/* For the status: what the node knows of its road. */
int road_value(lw_node *n, char *o, size_t cap)
{
	return snprintf(o, cap, "{\"state\":\"%s\",\"changes\":%llu,\"changed\":%d,\"sent\":%llu}", n->road_down ? "down" : "up", (unsigned long long)n->road_changes,
			n->road_changes ? (int)(n->now - n->road_changed_at) : -1, (unsigned long long)n->tx_pkts);
}
