/* SPDX-License-Identifier: GPL-3.0-or-later */
/* NAT-PMP (RFC 6886): ask the home router, which is the default gateway, to keep a mapping for our UDP port. A node
 * behind a router that agrees has an address and port that anyone can reach, so a direct path needs no punching and
 * survives a strict NAT. It is offered to the other side as one more candidate (docs/PROTOCOL.md §4). Best effort:
 * no gateway, or a gateway that does not answer, costs three small packets every half hour. */
#include "int.h"

#define PMP_PORT 5351
#define PMP_LIFETIME 3600

/* The default gateway from the kernel's route table, or 0. */
static int default_gateway(uint8_t gw[4])
{
	FILE *f = fopen("/proc/net/route", "r");
	char line[256], ifn[32];
	unsigned long dst, g, flags;
	int found = 0;

	if (!f)
		return 0;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "%31s %lx %lx %lx", ifn, &dst, &g, &flags) == 4 && dst == 0 && g != 0 && (flags & 2)) {
			memcpy(gw, &g, 4);		/* the table is in network byte order, read as host: same bytes back */
			found = 1;
			break;
		}
	fclose(f);
	return found;
}

static void pmp_send(lw_node *n, const uint8_t *gw, const uint8_t *msg, size_t len)
{
	struct sockaddr_in a;

	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	memcpy(&a.sin_addr, gw, 4);
	a.sin_port = htons(PMP_PORT);
	sendto(n->udp, msg, len, 0, (struct sockaddr *)&a, sizeof a);
}

void pmp_timers(lw_node *n)
{
	uint8_t gw[4], req[12];

	if (n->cfg.no_nat_pmp || n->role || n->now < n->pmp.next)
		return;
	if (n->pmp.port && n->now >= n->pmp.expire)	/* it was not renewed: the mapping is gone */
		n->pmp.port = 0;
	if (n->pmp.tries >= 3) {			/* no answer three times: try again in half an hour */
		n->pmp.tries = 0;
		n->pmp.next = n->now + 1800.0;
		return;
	}
	if (!default_gateway(gw)) {
		n->pmp.next = n->now + 60.0;
		return;
	}
	memcpy(n->pmp.gw, gw, 4);
	req[0] = 0;
	req[1] = 0;				/* op 0: what is my public address? */
	pmp_send(n, gw, req, 2);
	req[1] = 1;				/* op 1: map UDP */
	req[2] = req[3] = 0;
	put16(req + 4, n->udp_port);
	put16(req + 6, n->udp_port);
	put32(req + 8, PMP_LIFETIME);
	pmp_send(n, gw, req, 12);
	n->pmp.tries++;
	n->pmp.next = n->now + 2.0;
}

void pmp_input(lw_node *n, const uint8_t *p, size_t len, const struct sockaddr *src, socklen_t sl)
{
	const struct sockaddr_in *a = (const void *)src;

	if (n->role || n->cfg.no_nat_pmp || sl != sizeof *a || memcmp(&a->sin_addr, n->pmp.gw, 4) || ntohs(a->sin_port) != PMP_PORT ||
	    len < 8 || p[0] != 0 || get16(p + 2) != 0)		/* a result other than success: not offered here */
		return;
	if (p[1] == 128 && len == 12) {
		memcpy(n->pmp.ip, p + 8, 4);
		n->pmp.have_ip = 1;
	} else if (p[1] == 129 && len == 16 && get16(p + 8) == n->udp_port) {
		uint32_t life = get32(p + 12);

		n->pmp.port = (uint16_t)get16(p + 10);
		if (life < 60)
			life = 60;
		n->pmp.expire = n->now + life;
		n->pmp.next = n->now + life / 2.0;	/* renew halfway */
		n->pmp.tries = 0;
	}
}
