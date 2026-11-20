/* SPDX-License-Identifier: GPL-3.0-or-later
 * A member: a phone as a host on the line (docs/adr/0076, docs/ARCHITECTURE.md §5).
 *
 * Android gives an app IP packets (VpnService), the line carries Ethernet frames. This is the adapter, in userspace, on the device: a MAC of its own, a DHCPv4 client that asks the far
 * LAN (so the phone gets a real address, gateway and DNS from home), an ARP responder, and the wrapping and unwrapping of IPv4. IPv6 is not carried (counted, dropped).
 *
 * It is a state machine over frames and a clock. It sends and delivers through two functions it is given and touches no descriptor, so a test can drive it with a script for the far LAN.
 * Everything that comes from the line is hostile until checked: every length is tested before it is read, and a lease is sane or it is refused. */

#define MB_ARP_AGE 300.0
#define DHCP_MAGIC 0x63825363u
#define BOOTP_LEN 236

#include <pthread.h>
#include <fcntl.h>
static pthread_mutex_t leaf_mx = PTHREAD_MUTEX_INITIALIZER;
static const uint8_t MB_BCAST[6] = { 255, 255, 255, 255, 255, 255 };

static uint32_t mb_ip(const uint8_t *p) { return get32(p); }

static int mb_prefix_ok(uint32_t mask)
{
	return mask && mask != 0xffffffffu && !(~mask & (~mask + 1));	/* contiguous, and not /0 or /32 */
}

static void mb_ip_csum(uint8_t *h)
{
	unsigned sum;

	h[10] = h[11] = 0;
	sum = lw_sum16(h, 20, 0);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	put16(h + 10, ~sum & 0xffff);
}

int leaf_bound(const lw_leaf *m)
{
	return m->state == MB_BOUND || m->state == MB_RENEW;
}

void leaf_init(lw_leaf *m, const uint8_t key_id[32], double now)
{
	uint8_t h[32];

	memset(m, 0, sizeof *m);
	lw_h2(h, "longwire member mac", 19, key_id, 32);
	m->mac[0] = 0x02;			/* locally administered, unicast */
	memcpy(m->mac + 1, h, 5);
	lw_rand(&m->xid, sizeof m->xid);
	m->state = MB_INIT;
	m->t_next = now;
	snprintf(m->host, sizeof m->host, "longwire");
}

/* The name the DHCP server is told, so the router's list says whose phone this is: a host name is letters, digits and single hyphens, not at an end (RFC 1123), so the phone's name is made into one: lower case,
 * anything else a hyphen, hyphens run together and trimmed, at most 32. A name with nothing of the kind in it (not Latin letters) leaves "longwire". */
void leaf_hostname(lw_leaf *m, const char *name)
{
	char h[40];
	size_t o = 0;

	for (; name && *name && o < 32; name++) {
		char c = *name;

		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
			c = '-';
		if (c == '-' && (!o || h[o - 1] == '-'))
			continue;
		h[o++] = c;
	}
	while (o && h[o - 1] == '-')
		o--;
	h[o] = 0;
	if (o)
		snprintf(m->host, sizeof m->host, "%s", h);
}

/* ---- DHCPv4 ----------------------------------------------------------------------- */

#define D_DISCOVER 1
#define D_OFFER 2
#define D_REQUEST 3
#define D_ACK 5
#define D_NAK 6

static void dhcp_send(lw_leaf *m, int type, lw_mtx tx, void *ctx)
{
	uint8_t f[14 + 20 + 8 + BOOTP_LEN + 4 + 64];
	uint8_t *ip = f + 14, *udp = f + 34, *b = f + 42, *o;
	size_t len;

	memset(f, 0, sizeof f);
	memcpy(f, MB_BCAST, 6);
	memcpy(f + 6, m->mac, 6);
	put16(f + 12, 0x0800);
	ip[0] = 0x45;
	ip[8] = 64;
	ip[9] = 17;
	if (type == D_REQUEST && m->state == MB_RENEW)
		put32(ip + 12, m->ip);		/* a renewal comes from the address we hold */
	put32(ip + 16, 0xffffffffu);		/* the server may be anywhere on the segment: broadcast it */
	put16(udp, 68);
	put16(udp + 2, 67);
	b[0] = 1;				/* BOOTREQUEST */
	b[1] = 1;				/* Ethernet */
	b[2] = 6;
	put32(b + 4, m->xid);
	put16(b + 10, 0x8000);			/* broadcast the reply: the host has no address to be unicast to yet */
	if (m->state == MB_RENEW)
		put32(b + 12, m->ip);		/* ciaddr */
	memcpy(b + 28, m->mac, 6);
	put32(b + BOOTP_LEN, DHCP_MAGIC);
	o = b + BOOTP_LEN + 4;
	*o++ = 53; *o++ = 1; *o++ = (uint8_t)type;
	*o++ = 61; *o++ = 7; *o++ = 1; memcpy(o, m->mac, 6); o += 6;
	if (type == D_REQUEST && m->state == MB_REQUEST) {
		*o++ = 50; *o++ = 4; put32(o, m->offer_ip); o += 4;
		*o++ = 54; *o++ = 4; put32(o, m->offer_server); o += 4;
	}
	*o++ = 12; *o++ = (uint8_t)strlen(m->host); memcpy(o, m->host, strlen(m->host)); o += strlen(m->host);
	*o++ = 55; *o++ = 6; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 15; *o++ = 26; *o++ = 51;	/* mask, router, dns, domain, MTU, lease */
	*o++ = 255;
	len = (size_t)(o - f);
	if (len < 14 + 20 + 8 + BOOTP_LEN + 4 + 4)
		len = 14 + 20 + 8 + BOOTP_LEN + 4 + 4;
	put16(ip + 2, (unsigned)(len - 14));
	put16(udp + 4, (unsigned)(len - 34));
	mb_ip_csum(ip);			/* the UDP checksum is optional over IPv4: zero */
	tx(ctx, f, len);
	m->dhcp_sent++;
}

/* A lease is refused unless it is one a host could use: a real unicast address, a contiguous mask, a gateway on the link. A hostile member answering our DISCOVER can say anything. */
static int lease_sane(uint32_t ip, uint32_t mask, uint32_t gw)
{
	uint32_t a = ip >> 24;

	if (!ip || a == 0 || a == 127 || a >= 224 || (ip >> 16) == 0xa9fe)	/* nothing, loopback, multicast and up, link-local */
		return 0;
	if (!mb_prefix_ok(mask) || mask < 0xff000000u || mask > 0xfffffffcu)	/* /8 to /30 */
		return 0;
	if ((ip & ~mask) == 0 || (ip | mask) == 0xffffffffu)			/* the network or the broadcast address */
		return 0;
	if (gw && ((gw & mask) != (ip & mask) || gw == ip))
		return 0;
	return 1;
}

static void dhcp_to_init(lw_leaf *m, double now)
{
	if (leaf_bound(m))
		m->gen++;			/* the address is gone: the app's tunnel must not carry on with it */
	m->state = MB_INIT;
	m->ip = m->mask = m->gw = m->server = 0;
	m->ndns = 0;
	m->domain[0] = 0;
	m->mtu = 0;
	m->tries = 0;
	m->pendlen = 0;
	memset(m->arp, 0, sizeof m->arp);
	m->t_next = now + 1;
}

static void dhcp_input(lw_leaf *m, double now, const uint8_t *f, size_t len, lw_mtx tx, void *ctx)
{
	const uint8_t *ip = f + 14, *b, *o, *end;
	size_t ihl;
	int type = 0, have_mask = 0, ndns = 0;
	uint32_t mask = 0, gw = 0, server = 0, lease = 0, t1 = 0, t2 = 0, dns[3] = { 0, 0, 0 }, yi;
	char domain[64] = "";
	int mtu = 0;

	if (len < 14 + 20 + 8 + BOOTP_LEN + 4 + 1 || (ip[0] >> 4) != 4)
		return;
	ihl = (size_t)(ip[0] & 15) * 4;
	if (ihl < 20 || 14 + ihl + 8 + BOOTP_LEN + 4 > len || get16(ip + ihl) != 67 || get16(ip + ihl + 2) != 68)
		return;
	b = ip + ihl + 8;
	if (b[0] != 2 || b[1] != 1 || b[2] != 6 || get32(b + 4) != m->xid || memcmp(b + 28, m->mac, 6) || get32(b + BOOTP_LEN) != DHCP_MAGIC)
		return;
	yi = mb_ip(b + 16);
	o = b + BOOTP_LEN + 4;
	end = f + len;
	while (o < end && *o != 255) {
		unsigned code, l;

		if (*o == 0) {
			o++;
			continue;
		}
		if (o + 2 > end)
			return;
		code = o[0];
		l = o[1];
		if (o + 2 + l > end)
			return;
		o += 2;
		switch (code) {
		case 53: if (l == 1) type = o[0]; break;
		case 1: if (l == 4) { mask = mb_ip(o); have_mask = 1; } break;
		case 3: if (l >= 4) gw = mb_ip(o); break;
		case 6: for (ndns = 0; ndns < 3 && (unsigned)(ndns * 4 + 4) <= l; ndns++) dns[ndns] = mb_ip(o + ndns * 4); break;
		case 15: if (l && l < sizeof domain) { memcpy(domain, o, l); domain[l] = 0; } break;
		case 26: if (l == 2) mtu = (int)get16(o); break;
		case 51: if (l == 4) lease = mb_ip(o); break;
		case 54: if (l == 4) server = mb_ip(o); break;
		case 58: if (l == 4) t1 = mb_ip(o); break;
		case 59: if (l == 4) t2 = mb_ip(o); break;
		}
		o += l;
	}
	for (ndns = 0; ndns < 3 && dns[ndns]; ndns++)
		if ((dns[ndns] >> 24) == 127 || (dns[ndns] >> 24) >= 224)	/* a resolver at the phone's own loopback, or multicast, is a trick */
			break;
	for (int k = 0; k < (int)sizeof domain && domain[k]; k++)	/* a search domain is a name, nothing else */
		if (!((domain[k] >= 'a' && domain[k] <= 'z') || (domain[k] >= 'A' && domain[k] <= 'Z') || (domain[k] >= '0' && domain[k] <= '9') || domain[k] == '.' || domain[k] == '-')) {
			domain[0] = 0;
			break;
		}
	if (type == D_NAK && m->state != MB_INIT && m->state != MB_SELECT) {
		dhcp_to_init(m, now);
		return;
	}
	if (type == D_OFFER && m->state == MB_SELECT && server && lease_sane(yi, have_mask ? mask : 0, gw)) {
		m->offer_ip = yi;
		m->offer_server = server;
		m->state = MB_REQUEST;
		m->tries = 0;
		dhcp_send(m, D_REQUEST, tx, ctx);
		m->t_next = now + 4;
		return;
	}
	if (type == D_ACK && (m->state == MB_REQUEST || m->state == MB_RENEW) && lease_sane(yi, have_mask ? mask : 0, gw)) {
		int moved = !leaf_bound(m) || m->ip != yi || m->mask != mask || m->gw != gw;

		if (m->state == MB_REQUEST && (yi != m->offer_ip || (server && server != m->offer_server)))
			return;
		if (!lease || lease < 60)
			lease = 60;
		if (lease > 7 * 86400)
			lease = 7 * 86400;
		m->ip = yi;
		m->mask = mask;
		m->gw = gw;
		m->server = server ? server : m->offer_server;
		memcpy(m->dns, dns, sizeof dns);
		m->ndns = ndns;
		memcpy(m->domain, domain, sizeof m->domain);
		m->mtu = mtu >= 576 && mtu <= 1500 ? mtu : 0;
		m->lease = lease;
		m->t1 = t1 && t1 < lease ? t1 : lease / 2;
		m->t2 = t2 && t2 < lease && t2 > m->t1 ? t2 : lease - lease / 8;
		m->t_bound = now;
		m->state = MB_BOUND;
		m->t_next = now + m->t1;
		m->tries = 0;
		if (moved) {
			m->gen++;
			memset(m->arp, 0, sizeof m->arp);
			m->pendlen = 0;
		}
	}
}

/* ---- ARP -------------------------------------------------------------------------- */

static void arp_put(lw_leaf *m, double now, uint32_t ip, const uint8_t *mac)
{
	int i, old = 0;

	for (i = 0; i < MB_ARP; i++) {
		if (m->arp[i].ip == ip) {
			old = i;
			break;
		}
		if (m->arp[i].t < m->arp[old].t)
			old = i;
	}
	m->arp[old].ip = ip;
	memcpy(m->arp[old].mac, mac, 6);
	m->arp[old].t = now;
}

static const uint8_t *arp_get(lw_leaf *m, double now, uint32_t ip)
{
	int i;

	for (i = 0; i < MB_ARP; i++)
		if (m->arp[i].ip == ip && now - m->arp[i].t < MB_ARP_AGE)
			return m->arp[i].mac;
	return 0;
}

static void arp_send(lw_leaf *m, int op, const uint8_t *dmac, uint32_t sip, uint32_t tip, lw_mtx tx, void *ctx)
{
	uint8_t f[60];

	memset(f, 0, sizeof f);
	memcpy(f, dmac, 6);
	memcpy(f + 6, m->mac, 6);
	put16(f + 12, 0x0806);
	put16(f + 14, 1);
	put16(f + 16, 0x0800);
	f[18] = 6;
	f[19] = 4;
	put16(f + 20, (unsigned)op);
	memcpy(f + 22, m->mac, 6);
	put32(f + 28, sip);
	if (op == 2)
		memcpy(f + 32, dmac, 6);
	put32(f + 38, tip);
	tx(ctx, f, sizeof f);
}

static void frame_out(lw_leaf *m, const uint8_t *dmac, const uint8_t *pkt, size_t len, lw_mtx tx, void *ctx)
{
	uint8_t f[14 + 1500];

	memcpy(f, dmac, 6);
	memcpy(f + 6, m->mac, 6);
	put16(f + 12, 0x0800);
	memcpy(f + 14, pkt, len);
	tx(ctx, f, 14 + len);
	m->os_in++;
}

/* ---- the clock -------------------------------------------------------------------- */

void leaf_tick(lw_leaf *m, double now, lw_mtx tx, void *ctx)
{
	if (m->pendlen && now - m->pend_t > 5.0)	/* nobody answered for the next hop */
		m->pendlen = 0;
	if (m->pendlen && now - m->ask_t >= 1.0 && m->pend_asks < 4) {
		m->ask_t = now;
		m->pend_asks++;
		arp_send(m, 1, MB_BCAST, m->ip, m->pend_hop, tx, ctx);
	}
	if (now < m->t_next)
		return;
	switch (m->state) {
	case MB_INIT:
		m->xid++;
		m->state = MB_SELECT;
		dhcp_send(m, D_DISCOVER, tx, ctx);
		m->tries = 1;
		m->t_next = now + 4;
		break;
	case MB_SELECT:				/* no offer: again, slower, up to a minute between */
		dhcp_send(m, D_DISCOVER, tx, ctx);
		m->t_next = now + (m->tries < 5 ? (double)(4 << m->tries) : 64.0);
		m->tries++;
		break;
	case MB_REQUEST:			/* the offer was not confirmed: start again, after a few tries */
		if (++m->tries > 3) {
			m->state = MB_INIT;
			m->t_next = now;
		} else {
			dhcp_send(m, D_REQUEST, tx, ctx);
			m->t_next = now + 4;
		}
		break;
	case MB_BOUND:				/* T1: renew with the server that gave it */
		m->state = MB_RENEW;
		m->tries = 0;
		/* fall through */
	case MB_RENEW:
		if (now - m->t_bound >= m->lease) {	/* the lease ran out unrenewed: the address is not ours any more */
			dhcp_to_init(m, now);
			break;
		}
		m->xid++;
		dhcp_send(m, D_REQUEST, tx, ctx);
		m->tries++;
		m->t_next = now + (now - m->t_bound >= m->t2 ? 30.0 : 60.0);
		if (m->t_next - m->t_bound > m->lease)
			m->t_next = m->t_bound + m->lease;
		break;
	}
}

/* ---- from the OS tunnel to the line ----------------------------------------------- */

void leaf_from_os(lw_leaf *m, double now, const uint8_t *pkt, size_t len, lw_mtx tx, void *ctx)
{
	uint32_t dst, src, hop;
	const uint8_t *mac;
	uint8_t mc[6];
	size_t ihl;

	if (len < 1)
		return;
	if ((pkt[0] >> 4) == 6) {		/* IPv6 is not carried (docs/adr/0076): the tunnel has no route for it, this is what slips through */
		m->drop_v6++;
		return;
	}
	if ((pkt[0] >> 4) != 4 || len < 20 || len > 1500 || !leaf_bound(m)) {
		m->drop_os++;
		return;
	}
	ihl = (size_t)(pkt[0] & 15) * 4;
	if (ihl < 20 || ihl > len || get16(pkt + 2) != len) {
		m->drop_os++;
		return;
	}
	src = mb_ip(pkt + 12);
	dst = mb_ip(pkt + 16);
	if (src != m->ip || (dst >> 24) == 127 || dst == m->ip || !dst) {	/* only our own address leaves, and not to ourselves */
		m->drop_os++;
		return;
	}
	if (dst == 0xffffffffu || dst == (m->ip | ~m->mask)) {
		frame_out(m, MB_BCAST, pkt, len, tx, ctx);
		return;
	}
	if ((dst >> 28) == 14) {		/* multicast: 01:00:5e + the low 23 bits */
		mc[0] = 1; mc[1] = 0; mc[2] = 0x5e;
		mc[3] = (uint8_t)((dst >> 16) & 0x7f); mc[4] = (uint8_t)(dst >> 8); mc[5] = (uint8_t)dst;
		frame_out(m, mc, pkt, len, tx, ctx);
		return;
	}
	hop = (dst & m->mask) == (m->ip & m->mask) ? dst : m->gw;
	if (!hop) {				/* off the link and no gateway */
		m->drop_os++;
		return;
	}
	if ((mac = arp_get(m, now, hop))) {
		frame_out(m, mac, pkt, len, tx, ctx);
		return;
	}
	memcpy(m->pend, pkt, len);		/* one packet waits for the next hop's address; a newer one replaces it */
	m->pendlen = len;
	m->pend_hop = hop;
	m->pend_t = now;
	m->pend_asks = 1;
	m->ask_t = now;
	arp_send(m, 1, MB_BCAST, m->ip, hop, tx, ctx);
}

/* ---- from the line to the OS tunnel ----------------------------------------------- */

void leaf_from_line(lw_leaf *m, double now, const uint8_t *f, size_t len, lw_mtx tx, void *ctx, lw_mup up, void *uctx)
{
	unsigned et;
	const uint8_t *a;
	uint32_t sip, tip, dst;
	size_t total;

	if (len < 14 || len > 1514 || !memcmp(f + 6, m->mac, 6)) {
		m->drop_line++;
		return;
	}
	if (memcmp(f, m->mac, 6) && !(f[0] & 1)) {	/* another host's frame: not ours */
		m->drop_line++;
		return;
	}
	et = get16(f + 12);
	if (et == 0x0806) {
		if (len < 42 || get16(f + 14) != 1 || get16(f + 16) != 0x0800 || f[18] != 6 || f[19] != 4) {
			m->drop_line++;
			return;
		}
		a = f + 14;
		sip = mb_ip(a + 14);
		tip = mb_ip(a + 24);
		if (!leaf_bound(m) || !sip || (sip >> 24) >= 224 || sip == m->ip)
			return;
		if (get16(a + 6) == 1 && tip == m->ip) {	/* who has our address */
			arp_put(m, now, sip, a + 8);
			arp_send(m, 2, a + 8, m->ip, sip, tx, ctx);
			m->arp_answered++;
		} else if (get16(a + 6) == 2 && !memcmp(a + 18, m->mac, 6) && tip == m->ip)	/* an answer to us */
			arp_put(m, now, sip, a + 8);
		else
			return;
		if (m->pendlen && m->pend_hop == sip) {	/* the packet that waited */
			frame_out(m, a + 8, m->pend, m->pendlen, tx, ctx);
			m->pendlen = 0;
		}
		return;
	}
	if (et != 0x0800 || len < 14 + 20 || (f[14] >> 4) != 4) {
		m->drop_line++;
		return;
	}
	if (f[14 + 9] == 17 && len >= 14 + 20 + 8) {	/* DHCP, whether or not we are bound: a NAK matters most then */
		size_t ihl = (size_t)(f[14] & 15) * 4;

		if (ihl >= 20 && 14 + ihl + 4 <= len && get16(f + 14 + ihl + 2) == 68 && get16(f + 14 + ihl) == 67) {
			dhcp_input(m, now, f, len, tx, ctx);
			return;
		}
	}
	if (!leaf_bound(m) || (f[0] & 1)) {		/* broadcast and multicast are not delivered: the phone is not on a LAN's chatter */
		m->drop_line++;
		return;
	}
	dst = mb_ip(f + 14 + 16);
	total = get16(f + 14 + 2);
	if (dst != m->ip || total < 20 || total > len - 14 || ((f[14] & 15) * 4u) > total) {
		m->drop_line++;
		return;
	}
	sip = mb_ip(f + 14 + 12);
	if (!sip || sip == m->ip || (sip >> 24) == 127 || (sip >> 24) >= 224) {
		m->drop_line++;
		return;
	}
	up(uctx, f + 14, total);
	m->os_out++;
}

/* ---- the node's side of it -------------------------------------------------------- */

static void mb_tx(void *ctx, const uint8_t *f, size_t len)
{
	sw_from_local(ctx, f, len);
}

static void mb_up(void *ctx, const uint8_t *p, size_t len)
{
	lw_node *n = ctx;

	if (n->port_fd >= 0)
		(void)!write(n->port_fd, p, len);	/* a packet the tunnel will not take (it is full, or gone) is dropped like any other */
}

/* Is this address on the phone's own tunnel (the lease, or the made-up IPv6 address the app gives it so that IPv6 goes nowhere: fd6c:7700::/48)? It is no use to anyone as a place to reach the engine, and
 * its appearing and going is not the road changing. */
int leaf_owns(const lw_node *n, int fam, const uint8_t *ip)
{
	static const uint8_t ula[6] = { 0xfd, 0x6c, 0x77, 0x00, 0x00, 0x00 };

	if (!n->leaf)
		return 0;
	if (fam == 6)
		return !memcmp(ip, ula, 6);
	return leaf_bound(n->leaf) && get32(ip) == n->leaf->ip;
}

int leaf_open(lw_node *n, char *err, size_t errn)
{
	n->leaf = calloc(1, sizeof *n->leaf);
	if (!n->leaf) {
		snprintf(err, errn, "out of memory");
		return -1;
	}
	leaf_init(n->leaf, n->id.key_id, n->now);
	leaf_hostname(n->leaf, n->cfg.name);
	n->port_fd = -1;			/* the OS tunnel is attached when the app has one (the address is not known until the line has answered) */
	return 0;
}

/* The tunnel's descriptor is the app's own: the node reads and writes it and never closes it. The app calls this from its own thread, and the engine's loop is one thread that must not have a
 * descriptor taken from under a read: this asks, and waits (a second at most) until the loop has taken it. Once it returns, the loop touches no descriptor it had before, and the app may close
 * the old one. */
int leaf_attach(lw_node *n, int fd)
{
	unsigned asked;
	int i, done = 0;

	pthread_mutex_lock(&leaf_mx);
	n->leaf_want = fd;
	asked = ++n->leaf_asked;
	pthread_mutex_unlock(&leaf_mx);
	/* The loop looks at this at the top of an iteration, and an idle one sleeps up to a second (poll_ms): three seconds is more than the longest sleep and a call's busy spell. */
	for (i = 0; i < 600 && !done && !n->stop; i++) {
		pthread_mutex_lock(&leaf_mx);
		done = n->leaf_done == asked;
		pthread_mutex_unlock(&leaf_mx);
		if (!done)
			usleep(5000);
	}
	return done;				/* 0: the engine has not let go of what it had (it is stopping, or stuck): the caller must not close the old descriptor */
}

static void leaf_take(lw_node *n)
{
	int fd;

	pthread_mutex_lock(&leaf_mx);		/* the lock is also the memory barrier between the two threads */
	if (n->leaf_done == n->leaf_asked) {
		pthread_mutex_unlock(&leaf_mx);
		return;
	}
	fd = n->leaf_want;
	pthread_mutex_unlock(&leaf_mx);
	n->port_fd = fd;
	if (fd >= 0) {
		int fl = fcntl(fd, F_GETFL, 0);

		if (fl >= 0)
			fcntl(fd, F_SETFL, fl | O_NONBLOCK);	/* a read of an empty tunnel must return, not wait: the loop is one thread */
	}
	pthread_mutex_lock(&leaf_mx);
	n->leaf_done = n->leaf_asked;
	pthread_mutex_unlock(&leaf_mx);
}

void leaf_pump(lw_node *n)
{
	uint8_t pkt[2048];
	int i;

	for (i = 0; i < 256 && n->port_fd >= 0; i++) {
		ssize_t r = read(n->port_fd, pkt, sizeof pkt);

		if (r < 1)
			break;
		leaf_from_os(n->leaf, n->now, pkt, (size_t)r, mb_tx, n);
	}
}

void leaf_line(lw_node *n, const uint8_t *f, size_t len)
{
	leaf_from_line(n->leaf, n->now, f, len, mb_tx, n, mb_up, n);
}

void leaf_step(lw_node *n)
{
	leaf_take(n);
	leaf_tick(n->leaf, n->now, mb_tx, n);
}

static void mb_addr(char *o, size_t cap, uint32_t a)
{
	snprintf(o, cap, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
}

/* What the app needs to build its tunnel: the lease, and `gen`, which changes whenever the address does. */
int leaf_json(lw_node *n, char *out, size_t cap)
{
	lw_leaf *m = n->leaf;
	char ip[20], mask[20], gw[20], d[3][20];
	static const char *const st[] = { "asking", "asking", "asking", "bound", "renewing" };
	int i, prefix = 0;

	if (!m)
		return snprintf(out, cap, "{\"error\":\"this node is not a member\"}\n");
	mb_addr(ip, sizeof ip, m->ip);
	mb_addr(mask, sizeof mask, m->mask);
	mb_addr(gw, sizeof gw, m->gw);
	for (i = 0; i < 3; i++)
		mb_addr(d[i], sizeof d[i], m->ndns > i ? m->dns[i] : 0);
	for (i = 31; i >= 0 && (m->mask >> i & 1); i--)
		prefix++;
	return snprintf(out, cap,
		"{\"member\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"gen\":%u,\"ip\":\"%s\",\"mask\":\"%s\",\"prefix\":%d,\"gateway\":\"%s\",\"dns\":[%s%s%s%s%s],\"domain\":\"%s\",\"mtu\":%d,\"lease\":%u,"
		"\"tun\":%s,\"os_in\":%llu,\"os_out\":%llu,\"drop_v6\":%llu,\"drop_os\":%llu,\"drop_line\":%llu,\"arp_answered\":%llu,\"dhcp_sent\":%llu}\n",
		st[m->state], m->mac[0], m->mac[1], m->mac[2], m->mac[3], m->mac[4], m->mac[5], m->gen,
		leaf_bound(m) ? ip : "", leaf_bound(m) ? mask : "", prefix, leaf_bound(m) ? gw : "",
		m->ndns > 0 ? "\"" : "", m->ndns > 0 ? d[0] : "", m->ndns > 1 ? "\",\"" : "", m->ndns > 1 ? d[1] : "", m->ndns > 0 ? "\"" : "",
		m->domain, m->mtu, m->lease, n->port_fd >= 0 ? "true" : "false",
		(unsigned long long)m->os_in, (unsigned long long)m->os_out, (unsigned long long)m->drop_v6, (unsigned long long)m->drop_os,
		(unsigned long long)m->drop_line, (unsigned long long)m->arp_answered, (unsigned long long)m->dhcp_sent);
}
