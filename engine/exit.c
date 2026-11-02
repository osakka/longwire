/* SPDX-License-Identifier: GPL-3.0-or-later
 * The exit (docs/adr/0056): a member that shares its machine's internet says so to the others, inside the session, every
 * EXIT_EVERY seconds: "I am an exit, my jack's MAC is this". A node that sends everything through the line points its default
 * route at a gateway that is not a machine (LW_EXIT_MAC, fixed beside LW_EXIT_IP by the jack helper); the switch hands every
 * frame for it to the chosen exit's session, with the exit's MAC put in. So the gateway is a member's key: no address is
 * typed, no ARP answer is believed, and another member cannot become the gateway by claiming its address. The forwarding and
 * the NAT on the exit are its kernel's, set up by its jack helper (--share); this file only says and chooses. */
#include <sys/ioctl.h>
#include "int.h"

#define LW_SIOCGIFHWADDR_ 0x8927

/* The MAC this exit is reached by: the one given (a site's router), else the jack's own, asked of the tap now (it can be changed). */
static int exit_own_mac(lw_node *n, uint8_t mac[6])
{
	struct { char name[16]; struct { unsigned short fam; unsigned char data[14]; } sa; char pad[8]; } ifr;

	if (n->cfg.has_exit_mac) {
		memcpy(mac, n->cfg.exit_mac, 6);
		return 0;
	}
	if (strcmp(n->cfg.port_kind, "jack") || n->port_fd < 0)
		return -1;
	memset(&ifr, 0, sizeof ifr);
	snprintf(ifr.name, sizeof ifr.name, "%s", n->cfg.port_arg);
	if (ioctl(n->port_fd, LW_SIOCGIFHWADDR_, &ifr) < 0)
		return -1;
	memcpy(mac, ifr.sa.data, 6);
	return 0;
}

static int mac_usable(const uint8_t m[6])
{
	static const uint8_t zero[6];

	return !(m[0] & 1) && memcmp(m, zero, 6) && memcmp(m, LW_EXIT_MAC, 6);
}

/* A member is an exit to us while it has said so lately, on a session that is up. */
static int is_exit(lw_node *n, const lw_peer *p)
{
	return p->used && !p->is_sb && p->cur.valid && p->exit_seen > 0 && n->now - p->exit_seen <= EXIT_STALE && line_member(n, p->key_id);
}

void exit_msg(lw_node *n, lw_peer *p, const uint8_t *pl, size_t len)
{
	if (len != 6 || !mac_usable(pl) || p->is_sb || !line_member(n, p->key_id))
		return;				/* only a member of our line is ever a way out */
	memcpy(p->exit_mac, pl, 6);
	p->exit_seen = n->now;
}

/* The member to use: the one asked for, if it offers; else the lowest number that offers. -1: none. */
static int exit_pick(lw_node *n)
{
	int i, best = -1;

	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *p = &n->peers[i];

		if (!is_exit(n, p))
			continue;
		if (n->cfg.exit_through[0]) {
			if (!strcmp(p->number, n->cfg.exit_through))
				return i;
			continue;
		}
		if (best < 0 || strcmp(p->number, n->peers[best].number) < 0)
			best = i;
	}
	return best;
}

static int exit_offers(lw_node *n)
{
	int i, c = 0;

	for (i = 0; i < n->maxpeers; i++)
		c += is_exit(n, &n->peers[i]);
	return c;
}

void exit_timers(lw_node *n)
{
	int i;

	if (n->role)
		return;
	if (n->cfg.exit_share) {		/* to each member as soon as it is one, then every EXIT_EVERY seconds */
		uint8_t mac[6];
		int have = -1;

		for (i = 0; i < n->maxpeers; i++) {
			lw_peer *p = &n->peers[i];

			if (!p->used || p->is_sb || !p->cur.valid || (p->exit_tx > 0 && n->now - p->exit_tx < EXIT_EVERY) || !line_member(n, p->key_id))
				continue;
			if (have < 0)
				have = exit_own_mac(n, mac);
			if (have)
				break;
			ctl_msg_send(n, p, M_EXIT, mac, 6);
			p->exit_tx = n->now;
		}
	}
	if (n->cfg.use_exit) {
		const char *was = n->exit_using;
		char now[NUMLEN + 1] = "";

		n->exit_pi = exit_pick(n);
		if (n->exit_pi >= 0)
			snprintf(now, sizeof now, "%s", n->peers[n->exit_pi].number);
		if (!n->exit_told || strcmp(was, now)) {
			char f[16] = "", w[16] = "";

			if (now[0])
				fmt_number(f, now);
			if (n->cfg.exit_through[0])
				fmt_number(w, n->cfg.exit_through);
			ev_emit(n, "{\"event\":\"exit\",\"number\":\"%s\",\"wanted\":\"%s\",\"offers\":%d}", f, w, exit_offers(n));
			snprintf(n->exit_using, sizeof n->exit_using, "%s", now);
			n->exit_told = 1;
		}
	}
}

/* A frame from our side for the exit gateway: to the exit's session with its MAC put in, or dropped (never flooded). 1: it was one. */
int exit_frame(lw_node *n, uint8_t *f, size_t len)
{
	lw_peer *p;

	if (memcmp(f, LW_EXIT_MAC, 6))
		return 0;
	if (!n->cfg.use_exit || n->exit_pi < 0 || !is_exit(n, (p = &n->peers[n->exit_pi]))) {
		n->exit_dropped++;
		return 1;
	}
	memcpy(f, p->exit_mac, 6);
	n->exit_sent++;
	send_one(n, p, f, len);
	return 1;
}

/* For the status: what this node shares and uses. */
int exit_value(lw_node *n, char *o, size_t cap)
{
	char f[16] = "", w[16] = "";

	if (n->cfg.use_exit && n->exit_pi >= 0 && is_exit(n, &n->peers[n->exit_pi]))
		fmt_number(f, n->peers[n->exit_pi].number);
	if (n->cfg.exit_through[0])
		fmt_number(w, n->cfg.exit_through);
	return snprintf(o, cap, "{\"share\":%s,\"use\":%s,\"wanted\":\"%s\",\"using\":\"%s\",\"offers\":%d,\"sent\":%llu,\"dropped\":%llu}",
			n->cfg.exit_share ? "true" : "false", n->cfg.use_exit ? "true" : "false", w, f, exit_offers(n),
			(unsigned long long)n->exit_sent, (unsigned long long)n->exit_dropped);
}
