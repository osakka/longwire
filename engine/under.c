/* SPDX-License-Identifier: GPL-3.0-or-later
 * The road under the tunnel (docs/adr/0037). A node whose jack has an address in the same network as the other end sits in a trap:
 * the kernel sends a packet for that address into the jack, and the packet was meant to bring the tunnel up. A packet for such
 * an address leaves instead through a socket of its own, tied to the network device the machine reaches the world by, so the jack's
 * route cannot capture it (the way WireGuard's wg-quick keeps its own traffic off the tunnel, without the root it needs). */
#include <sys/socket.h>

#ifndef SO_BINDTODEVICE
#define SO_BINDTODEVICE 25
#endif

static int jack_mode(const lw_node *n)
{
	return !strcmp(n->cfg.port_kind, "jack");
}

/* The routes through the jack count as its networks too: a network routed into the tunnel is as much a trap for the packets that bring the tunnel up as one
 * connected to it (docs/adr/0044). Read from the kernel's route table (/proc/net/route, readable by anyone): IPv4 only, as the table is. */
static void jnets_routes(lw_node *n)
{
	FILE *f;
	char line[256], ifn[32];
	unsigned long dst, gw, flags, mask;

	if (!(f = fopen(n->cfg.routes_file[0] ? n->cfg.routes_file : "/proc/net/route", "r")))
		return;
	if (!fgets(line, sizeof line, f)) {
		fclose(f);
		return;
	}
	while (n->njnet < LW_JNETS && fgets(line, sizeof line, f)) {
		lw_jnet *j = &n->jnet[n->njnet];
		uint32_t d, m;
		int k;

		if (sscanf(line, "%31s %lx %lx %lx %*d %*d %*d %lx", ifn, &dst, &gw, &flags, &mask) != 5 || !(flags & 1) || strcmp(ifn, n->cfg.port_arg))
			continue;
		d = (uint32_t)dst;			/* the table is in the host's byte order of a network-order address: the bytes as they are */
		m = (uint32_t)mask;
		memset(j, 0, sizeof *j);
		j->fam = 4;
		memcpy(j->mask, &m, 4);
		memcpy(j->net, &d, 4);
		for (k = 0; k < 4; k++)
			j->net[k] &= j->mask[k];
		n->njnet++;
	}
	fclose(f);
}

static int underlay_dev(const lw_node *n, char *dev, size_t cap);

/* A device name as it may go into an event: what a name usually is, or "?" (docs/adr/0046). */
static const char *dev_safe(const char *d)
{
	const char *c;

	for (c = d; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.' || *c == '@'))
			return "?";
	return d;
}

/* The road under the line may change while the node runs: a laptop moves from Wi-Fi to a cable, or loses both (docs/adr/0046). Looked at once a second.
 * The underlay socket of docs/adr/0037 is opened again on the new road when next wanted. With everything through the line, every socket is tied to the
 * old road and cannot be tied again without privilege: the node lets go of the jack at once, so that the kernel's safety net (docs/adr/0045) puts the
 * machine on its own road, and says so; the script starts the node again on the new one. */
static void road_check(lw_node *n)
{
	char dev[32];
	int have;

	if (!jack_mode(n) || n->cfg.underlay[0])
		return;					/* a device named in the options: nothing to follow */
	have = underlay_dev(n, dev, sizeof dev);
	if (n->tie[0]) {
		if (!n->roamed && (!have || strcmp(dev, n->tie))) {
			n->roamed = 1;
			if (n->port_fd >= 0) {
				close(n->port_fd);	/* the jack's carrier goes with it */
				n->port_fd = -1;
			}
			ev_emit(n, "{\"event\":\"roamed\",\"from\":\"%s\",\"to\":\"%s\"}", dev_safe(n->tie), have ? dev_safe(dev) : "");
		}
	} else if (n->uudp >= 0 && (!have || strcmp(dev, n->uudev))) {
		close(n->uudp);
		n->uudp = -1;
	} else if (n->uudp == -2 && have)
		n->uudp = -1;				/* a road again: try once more when next wanted */
}

/* The networks connected to the jack, read from the system. Kept for a second: it is looked at for every packet to a peer. */
void jnets_refresh(lw_node *n)
{
	struct ifaddrs *ifa, *i;

	n->njnet = 0;
	n->jnet_at = n->now;
	snprintf(n->jnet_dev, sizeof n->jnet_dev, "%s", n->cfg.port_arg);
	if (!jack_mode(n) || getifaddrs(&ifa))
		return;
	for (i = ifa; i && n->njnet < LW_JNETS; i = i->ifa_next) {
		lw_jnet *j = &n->jnet[n->njnet];
		const uint8_t *a, *m;
		int k, len;

		if (!i->ifa_addr || !i->ifa_netmask || strcmp(i->ifa_name, n->cfg.port_arg))
			continue;
		memset(j, 0, sizeof *j);
		if (i->ifa_addr->sa_family == AF_INET) {
			a = (const void *)&((const struct sockaddr_in *)i->ifa_addr)->sin_addr;
			m = (const void *)&((const struct sockaddr_in *)i->ifa_netmask)->sin_addr;
			len = 4;
			j->fam = 4;
		} else if (i->ifa_addr->sa_family == AF_INET6 &&
			   !IN6_IS_ADDR_LINKLOCAL(&((const struct sockaddr_in6 *)i->ifa_addr)->sin6_addr)) {
			a = (const void *)&((const struct sockaddr_in6 *)i->ifa_addr)->sin6_addr;
			m = (const void *)&((const struct sockaddr_in6 *)i->ifa_netmask)->sin6_addr;
			len = 16;
			j->fam = 6;
		} else
			continue;
		for (k = 0; k < len; k++) {
			j->net[k] = a[k] & m[k];
			j->mask[k] = m[k];
		}
		n->njnet++;
	}
	freeifaddrs(ifa);
	jnets_routes(n);
	road_check(n);
}

void jnets_fresh(lw_node *n)
{
	if (n->now - n->jnet_at >= 1.0 || n->now < n->jnet_at || strcmp(n->jnet_dev, n->cfg.port_arg))
		jnets_refresh(n);
}

/* Is this address inside a network connected to our own jack? */
int jack_net_has(const lw_node *n, int fam, const uint8_t *ip)
{
	int i, k, len = fam == 6 ? 16 : 4, hit;

	for (i = 0; i < n->njnet; i++) {
		if (n->jnet[i].fam != fam)
			continue;
		for (k = 0, hit = 1; k < len; k++)
			if ((ip[k] & n->jnet[i].mask[k]) != n->jnet[i].net[k])
				hit = 0;
		if (hit)
			return 1;
	}
	return 0;
}

int jack_net_sa(const lw_node *n, const struct sockaddr *sa)
{
	if (!n->njnet)
		return 0;
	if (sa->sa_family == AF_INET)
		return jack_net_has(n, 4, (const uint8_t *)&((const struct sockaddr_in *)sa)->sin_addr);
	if (sa->sa_family == AF_INET6)
		return jack_net_has(n, 6, (const uint8_t *)&((const struct sockaddr_in6 *)sa)->sin6_addr);
	return 0;
}

/* The device the machine reaches the world by: the one named in the options, or the default route's, whichever is not the jack. */
static int underlay_dev(const lw_node *n, char *dev, size_t cap)
{
	FILE *f;
	char line[256], ifn[32];
	unsigned long dst, gw, flags, mask;
	int found = 0;

	if (n->cfg.underlay[0]) {
		snprintf(dev, cap, "%s", n->cfg.underlay);
		return 1;
	}
	if (!(f = fopen(n->cfg.routes_file[0] ? n->cfg.routes_file : "/proc/net/route", "r")))
		return 0;
	if (!fgets(line, sizeof line, f)) {
		fclose(f);
		return 0;
	}
	while (!found && fgets(line, sizeof line, f))
		if (sscanf(line, "%31s %lx %lx %lx %*d %*d %*d %lx", ifn, &dst, &gw, &flags, &mask) == 5 && dst == 0 && mask == 0 &&
		    (flags & 1) && strcmp(ifn, n->cfg.port_arg) && strcmp(ifn, "lo")) {
			snprintf(dev, cap, "%s", ifn);
			found = 1;
		}
	fclose(f);
	return found;
}

/* The socket for what lies in the jack's own networks, opened when first wanted; -1 if there is none to be had (a kernel that lets
 * only root tie a socket to a device, or no default route). Tried once. */
int under_fd(lw_node *n)
{
	char dev[32];
	struct sockaddr_in a;
	int fd;

	if (n->tie[0])
		return n->udp;		/* everything through the line: every socket is tied already */
	if (n->uudp != -1)
		return n->uudp < 0 ? -1 : n->uudp;
	n->uudp = -2;
	if (!jack_mode(n) || !underlay_dev(n, dev, sizeof dev))
		return -1;
	if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) < 0)
		return -1;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, dev, (socklen_t)strlen(dev) + 1) < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
		close(fd);
		return -1;
	}
	snprintf(n->uudev, sizeof n->uudev, "%s", dev);
	n->uudp = fd;
	return fd;
}

/* Everything through the line (docs/adr/0045): the machine's default route is in a table of the jack's own, which every packet not tied to a device
 * follows. So every socket of ours is tied, before it is bound, to the device the machine reaches the world by; the kernel then skips the jack's
 * table for it. One socket per family, not a second one: a second source port would be a second NAT mapping, and the punch would miss. */
int under_tie(lw_node *n, char *err, size_t errn)
{
	if (!n->cfg.everything)
		return 0;
	if (!jack_mode(n)) {
		snprintf(err, errn, "everything through the line needs a jack");
		return -1;
	}
	if (!underlay_dev(n, n->tie, sizeof n->tie)) {
		snprintf(err, errn, "everything through the line: no default route outside the jack to keep the line itself on");
		return -1;
	}
	return 0;
}

int tie_fd(const lw_node *n, int fd)
{
	if (!n->tie[0])
		return 0;
	return setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, n->tie, (socklen_t)strlen(n->tie) + 1);
}

/* sendto(), except that an address inside the jack's own networks goes out through the underlay socket. */
ssize_t usend(lw_node *n, int fd, const void *b, size_t len, const struct sockaddr *sa, socklen_t sl)
{
	if (n->njnet && sa->sa_family == AF_INET && jack_net_sa(n, sa)) {
		int u = under_fd(n);

		if (u >= 0)
			fd = u;
	}
	return sendto(fd, b, len, 0, sa, sl);
}
