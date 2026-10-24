/* SPDX-License-Identifier: GPL-3.0-or-later
 * Port spray (docs/PROTOCOL.md section 4). Two ends of a call, one behind a NAT that makes a new mapping for every
 * destination (strict) and one that does not (easy). The strict end opens SPRAY_N sockets and sends each to the easy
 * end's address, making SPRAY_N mappings. The easy end sends probes to random ports of the strict end's address. A probe
 * that lands on one of the mappings reaches that socket, the strict end answers from it, and from then on that socket is
 * the strict end's way to this peer. Two strict ends do not try: the odds are hopeless, and a relay is the answer. */

#define PROBES_PER_SEC 100.0
#define SPRAY_AFTER 2.0			/* the ordinary punch gets this long alone */
#define SPRAY_FOR 14.0

int peer_fd(const lw_node *n, const lw_peer *p)
{
	return p && p->sfd ? p->sfd : n->udp;
}

void spray_close(lw_node *n)
{
	int i;

	for (i = 0; i < n->nspray; i++)
		close(n->spray_fd[i]);
	n->nspray = 0;
}

int spray_is(const lw_node *n, int fd)
{
	int i;

	for (i = 0; i < n->nspray; i++)
		if (n->spray_fd[i] == fd)
			return 1;
	return 0;
}

/* The socket the probe came in on becomes this peer's. */
void spray_adopt(lw_node *n, lw_peer *p)
{
	int i;

	p->sfd = n->rx_fd;
	for (i = 0; i < n->nspray; i++)
		if (n->spray_fd[i] != p->sfd)
			close(n->spray_fd[i]);
	n->nspray = 0;
}

static int far_cand(const lw_cand *c)
{
	return c->type == CAND_REFL || c->type == CAND_MAPPED;
}

static void spray_open(lw_node *n, lw_call *c)
{
	struct sockaddr_in la, a;
	socklen_t ll = sizeof la;
	uint8_t pk[33];
	int i, j;

	if (getsockname(n->udp, (struct sockaddr *)&la, &ll) || la.sin_family != AF_INET)
		return;
	la.sin_port = 0;
	punch_pkt(n, c, pk);
	for (i = 0; i < SPRAY_N; i++) {
		int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

		if (fd < 0)
			break;
		if (bind(fd, (struct sockaddr *)&la, sizeof la) < 0) {
			close(fd);
			break;
		}
		n->spray_fd[n->nspray++] = fd;
		for (j = 0; j < c->npc; j++) {
			if (!far_cand(&c->pc[j]))
				continue;
			memset(&a, 0, sizeof a);
			a.sin_family = AF_INET;
			memcpy(&a.sin_addr, c->pc[j].ip, 4);
			a.sin_port = htons(c->pc[j].port);
			sendto(fd, pk, sizeof pk, 0, (struct sockaddr *)&a, sizeof a);
		}
	}
}

/* Called for a call that is punching. */
void spray_step(lw_node *n, lw_call *c)
{
	uint8_t pk[33];
	int j;

	if (n->now < c->start_at + SPRAY_AFTER || n->now > c->start_at + SPRAY_AFTER + SPRAY_FOR || c->have_verified)
		return;
	if (n->nat == 2 && !c->peer_strict) {			/* we are the strict end: make mappings, and listen */
		if (!c->sprayed) {
			c->sprayed = 1;
			spray_open(n, c);
		}
		return;
	}
	if (n->nat != 2 && c->peer_strict) {			/* the other end is: probe its public address at random ports */
		struct sockaddr_in a;

		if (!c->probe_last)
			c->probe_last = n->now;
		c->probe_credit += (n->now - c->probe_last) * PROBES_PER_SEC;
		c->probe_last = n->now;
		if (c->probe_credit > 50.0)
			c->probe_credit = 50.0;
		punch_pkt(n, c, pk);
		for (; c->probe_credit >= 1.0; c->probe_credit -= 1.0) {
			uint16_t r;

			lw_rand((uint8_t *)&r, 2);
			for (j = 0; j < c->npc; j++) {
				if (!far_cand(&c->pc[j]))
					continue;
				memset(&a, 0, sizeof a);
				a.sin_family = AF_INET;
				memcpy(&a.sin_addr, c->pc[j].ip, 4);
				a.sin_port = htons((uint16_t)(1024 + r % 64512));
				sendto(n->udp, pk, sizeof pk, 0, (struct sockaddr *)&a, sizeof a);
			}
		}
	}
}

/* The sockets are only for a call that is still punching. */
void spray_reap(lw_node *n)
{
	int i;

	if (!n->nspray)
		return;
	for (i = 0; i < n->ncalls; i++)
		if (n->calls[i].state == CALL_PUNCH && n->calls[i].sprayed)
			return;
	spray_close(n);
}
