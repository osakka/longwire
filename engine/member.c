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
#include <arpa/inet.h>
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
	m->ll6[0] = 0xfe;			/* link-local: fe80::/64 and the interface id made from the MAC (EUI-64, the universal/local bit turned) */
	m->ll6[1] = 0x80;
	m->ll6[8] = m->mac[0] ^ 2;
	m->ll6[9] = m->mac[1];
	m->ll6[10] = m->mac[2];
	m->ll6[11] = 0xff;
	m->ll6[12] = 0xfe;
	memcpy(m->ll6 + 13, m->mac + 3, 3);
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
int lease_sane(uint32_t ip, uint32_t mask, uint32_t gw)
{
	uint32_t a = ip >> 24;

	if (!ip || a == 0 || a == 127 || a >= 224 || (ip >> 16) == 0xa9fe)	/* nothing, loopback, multicast and up, link-local */
		return 0;
	if (!(a == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8 || (ip >> 22) == 0x191))	/* a network that is a LAN's: 10/8, 172.16/12, 192.168/16, 100.64/10. A lease for a public one would put a route over it (docs/adr/0066, 0102) */
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


/* ---- IPv6: an address from the router's advertisement, and neighbour discovery (docs/adr/0098) -------------------------------------------------------------------- */

static const uint8_t MB6_ALLROUTERS[16] = { 0xff, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 };

/* The ICMPv6 checksum: the pseudo header (both addresses, the length, the next header 58) and the message with its own checksum field zero. */
static unsigned icmp6_sum(const uint8_t *src, const uint8_t *dst, const uint8_t *msg, size_t len)
{
	uint32_t s = 0;
	size_t i;

	for (i = 0; i < 16; i += 2)
		s += (unsigned)(src[i] << 8 | src[i + 1]) + (unsigned)(dst[i] << 8 | dst[i + 1]);
	s += (uint32_t)(len >> 16) + (uint32_t)(len & 0xffff) + 58;
	for (i = 0; i + 1 < len; i += 2)
		s += (unsigned)(msg[i] << 8 | msg[i + 1]);
	if (len & 1)
		s += (unsigned)(msg[len - 1] << 8);
	while (s >> 16)
		s = (s & 0xffff) + (s >> 16);
	return (~s) & 0xffff;
}

static int icmp6_ok(const uint8_t *src, const uint8_t *dst, const uint8_t *msg, size_t len)
{
	uint8_t c[1500];

	if (len < 8 || len > sizeof c)
		return 0;
	memcpy(c, msg, len);
	c[2] = c[3] = 0;
	return icmp6_sum(src, dst, c, len) == get16(msg + 2);
}

static int is_ll6(const uint8_t *a) { return a[0] == 0xfe && (a[1] & 0xc0) == 0x80; }
static int is_zero6(const uint8_t *a)
{
	static const uint8_t z[16] = {0};

	return !memcmp(a, z, 16);
}

/* One ICMPv6 message in a frame: hop limit 255 (a neighbour discovery message from beyond the link would not have it), the checksum made here. */
static void f6_out(lw_leaf *m, const uint8_t *dmac, const uint8_t *src, const uint8_t *dst, uint8_t *msg, size_t il, lw_mtx tx, void *ctx)
{
	uint8_t f[14 + 40 + 96];

	if (il > 96)
		return;
	memcpy(f, dmac, 6);
	memcpy(f + 6, m->mac, 6);
	put16(f + 12, 0x86dd);
	f[14] = 0x60;
	f[15] = f[16] = f[17] = 0;
	put16(f + 18, (unsigned)il);
	f[20] = 58;
	f[21] = 255;
	memcpy(f + 22, src, 16);
	memcpy(f + 38, dst, 16);
	msg[2] = msg[3] = 0;
	memcpy(f + 54, msg, il);
	put16(f + 56, icmp6_sum(src, dst, f + 54, il));
	tx(ctx, f, 54 + il);
}

static void mc6_mac(uint8_t mac[6], const uint8_t *a)	/* the Ethernet address of an IPv6 multicast address: 33:33 and its last four bytes */
{
	mac[0] = mac[1] = 0x33;
	memcpy(mac + 2, a + 12, 4);
}

static void nd6_put(lw_leaf *m, double now, const uint8_t *ip, const uint8_t *mac)
{
	int i, old = 0;

	for (i = 0; i < MB_ARP; i++) {
		if (!memcmp(m->nd[i].ip, ip, 16)) {
			old = i;
			break;
		}
		if (m->nd[i].t < m->nd[old].t)
			old = i;
	}
	memcpy(m->nd[old].ip, ip, 16);
	memcpy(m->nd[old].mac, mac, 6);
	m->nd[old].t = now;
}

static const uint8_t *nd6_get(lw_leaf *m, double now, const uint8_t *ip)
{
	int i;

	for (i = 0; i < MB_ARP; i++)
		if (!memcmp(m->nd[i].ip, ip, 16) && now - m->nd[i].t < MB_ARP_AGE)
			return m->nd[i].mac;
	return 0;
}

static void rs_send(lw_leaf *m, lw_mtx tx, void *ctx)		/* router solicitation, from the link-local address, with the MAC as the option */
{
	uint8_t msg[16], mac[6];

	memset(msg, 0, sizeof msg);
	msg[0] = 133;
	msg[8] = 1;
	msg[9] = 1;
	memcpy(msg + 10, m->mac, 6);
	mc6_mac(mac, MB6_ALLROUTERS);
	f6_out(m, mac, m->ll6, MB6_ALLROUTERS, msg, sizeof msg, tx, ctx);
}

static void ns_send(lw_leaf *m, const uint8_t *target, lw_mtx tx, void *ctx)
{
	uint8_t msg[32], dst[16] = { 0xff, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0xff, 0, 0, 0 }, mac[6];

	memset(msg, 0, sizeof msg);
	msg[0] = 135;
	memcpy(msg + 8, target, 16);
	msg[24] = 1;
	msg[25] = 1;
	memcpy(msg + 26, m->mac, 6);
	memcpy(dst + 13, target + 13, 3);			/* the solicited-node multicast address of the target */
	mc6_mac(mac, dst);
	f6_out(m, mac, m->has6 ? m->a6 : m->ll6, dst, msg, sizeof msg, tx, ctx);
}

static void na_send(lw_leaf *m, const uint8_t *dmac, const uint8_t *dst, const uint8_t *target, lw_mtx tx, void *ctx)
{
	uint8_t msg[32];

	memset(msg, 0, sizeof msg);
	msg[0] = 136;
	msg[4] = 0x60;						/* solicited, override */
	memcpy(msg + 8, target, 16);
	msg[24] = 2;
	msg[25] = 1;
	memcpy(msg + 26, m->mac, 6);
	f6_out(m, dmac, target, dst, msg, sizeof msg, tx, ctx);
}

static int mine6(const lw_leaf *m, const uint8_t *a)
{
	return !memcmp(a, m->ll6, 16) || (m->has6 && !memcmp(a, m->a6, 16));
}

static void frame6_out(lw_leaf *m, const uint8_t *dmac, const uint8_t *pkt, size_t len, lw_mtx tx, void *ctx)
{
	uint8_t f[14 + 1500];

	memcpy(f, dmac, 6);
	memcpy(f + 6, m->mac, 6);
	put16(f + 12, 0x86dd);
	memcpy(f + 14, pkt, len);
	tx(ctx, f, 14 + len);
	m->os_in++;
}

/* A router advertisement (docs/adr/0098): what the node let through is from a member this node asked for (the guard), and is checked again here: from a link-local address, hop limit 255,
 * a good checksum. The address is the first prefix that says it may be made from (the A flag) at 64 bits, with this phone's interface id; the router is the sender, for as long as it says. */
static void ra_input(lw_leaf *m, double now, const uint8_t *f, size_t len)
{
	const uint8_t *ip = f + 14, *msg = f + 54, *rmac = f + 6;
	size_t il = get16(ip + 4), o;
	unsigned life;
	int got = 0;
	uint8_t a[16];

	if (14 + 40 + il > len || il < 16 || ip[6] != 58 || ip[7] != 255 || !is_ll6(ip + 8) || msg[0] != 134 || msg[1] != 0 || !icmp6_ok(ip + 8, ip + 24, msg, il))
		return;
	life = get16(msg + 6);
	m->ra_seen++;
	for (o = 16; o + 2 <= il;) {
		size_t ol = (size_t)msg[o + 1] * 8;

		if (!ol || o + ol > il)
			return;
		if (msg[o] == 1 && ol == 8)				/* the router's own link-layer address */
			rmac = msg + o + 2;
		else if (msg[o] == 3 && ol == 32 && !got && msg[o + 2] == 64 && (msg[o + 3] & 0x40) && get32(msg + o + 4) && get32(msg + o + 8) <= get32(msg + o + 4)) {
			const uint8_t *pre = msg + o + 16;

			if (pre[0] != 0xff && !is_ll6(pre) && !is_zero6(pre) && !(pre[0] == 0 && pre[1] == 0)) {	/* not multicast, link-local or the unspecified: a prefix that is a network */
				memcpy(a, pre, 8);
				a[8] = m->mac[0] ^ 2;
				a[9] = m->mac[1];
				a[10] = m->mac[2];
				a[11] = 0xff;
				a[12] = 0xfe;
				memcpy(a + 13, m->mac + 3, 3);
				got = 1;
				m->addr_until = now + (double)get32(msg + o + 4);
			}
		}
		o += ol;
	}
	if (life) {
		memcpy(m->gw6, ip + 8, 16);
		memcpy(m->gwmac, rmac, 6);
		m->has_gw6 = 1;
		m->router_until = now + (double)life;
		nd6_put(m, now, m->gw6, m->gwmac);
	} else if (!memcmp(ip + 8, m->gw6, 16))			/* a router that says it is going */
		m->has_gw6 = 0;
	if (got) {
		if (!m->has6 || memcmp(m->a6, a, 16)) {
			memcpy(m->a6, a, 16);
			m->has6 = 1;
			m->pfx6 = 64;
			m->gen++;				/* the app builds its tunnel again, with the address */
		}
		m->ra_t = now;
	}
}

/* Neighbour discovery for this phone: a solicitation for its address is answered, an advertisement is kept, and a packet that waits for a neighbour goes when it is known. Returns 1 if the frame
 * was one of these. */
static int nd6_input(lw_leaf *m, double now, const uint8_t *f, size_t len, lw_mtx tx, void *ctx)
{
	const uint8_t *ip = f + 14, *msg = f + 54;
	size_t il = get16(ip + 4), o;
	const uint8_t *lla = 0, *sa;

	if (14 + 40 + il > len || il < 24 || ip[7] != 255 || !icmp6_ok(ip + 8, ip + 24, msg, il) || msg[1] != 0)
		return 1;
	sa = ip + 8;
	for (o = 24; o + 2 <= il;) {
		size_t ol = (size_t)msg[o + 1] * 8;

		if (!ol || o + ol > il)
			return 1;
		if (msg[o] == (msg[0] == 135 ? 1 : 2) && ol == 8)
			lla = msg + o + 2;
		o += ol;
	}
	if (msg[0] == 135) {					/* who has this address */
		if (is_zero6(sa) || !mine6(m, msg + 8) || !lla)	/* an address check (unspecified source) is not answered: nobody here takes the phone's address */
			return 1;
		nd6_put(m, now, sa, lla);
		na_send(m, lla, sa, msg + 8, tx, ctx);
		m->nd_answered++;
		return 1;
	}
	if (!lla && (!(msg[4] & 0x20) || is_zero6(msg + 8)))
		lla = f + 6;						/* an advertisement with no option is the sender's frame */
	if (!lla || msg[8] == 0xff || is_zero6(msg + 8))
		return 1;
	if (m->pend6len && !memcmp(msg + 8, m->pend6_hop, 16)) {
		nd6_put(m, now, msg + 8, lla);
		frame6_out(m, lla, m->pend6, m->pend6len, tx, ctx);
		m->pend6len = 0;
	} else if (m->has_gw6 && !memcmp(msg + 8, m->gw6, 16)) {
		nd6_put(m, now, msg + 8, lla);
		memcpy(m->gwmac, lla, 6);
	}
	return 1;
}

static void v6_tick(lw_leaf *m, double now, lw_mtx tx, void *ctx)
{
	if (m->pend6len && now - m->pend6_t > 5.0)
		m->pend6len = 0;
	if (m->pend6len && now - m->ask6_t >= 1.0 && m->pend6_asks < 4) {
		m->ask6_t = now;
		m->pend6_asks++;
		ns_send(m, m->pend6_hop, tx, ctx);
	}
	if (m->has6 && m->addr_until && now > m->addr_until) {	/* the prefix's lifetime ran out unrenewed: no address, and ask again */
		m->has6 = 0;
		m->has_gw6 = 0;
		m->gen++;
		m->rs_tries = 0;
		m->rs_t = 0;
	}
	if (now < m->rs_t)
		return;
	if (!m->has6 || now - m->ra_t > 600.0) {		/* ask the routers on the link: three times at once-ish, then once a minute; a router that is there says so by itself every few minutes besides */
		rs_send(m, tx, ctx);
		m->rs_t = now + (m->rs_tries < 3 ? 4.0 : 60.0);
		m->rs_tries++;
	} else
		m->rs_t = now + 30.0;
}

static void v6_from_os(lw_leaf *m, double now, const uint8_t *pkt, size_t len, lw_mtx tx, void *ctx)
{
	const uint8_t *dst = pkt + 24, *mac, *hop;
	uint8_t mc[6];

	if (len < 40 || len > 1500 || !m->has6 || get16(pkt + 4) + 40u != len || !mine6(m, pkt + 8) || mine6(m, dst) || is_zero6(dst) || dst[0] == 0xff) {
		if (len >= 40 && dst[0] == 0xff && m->has6 && mine6(m, pkt + 8) && (dst[1] & 0x0f) >= 2) {	/* link-local and wider multicast: the LAN's, on its multicast address */
			mc6_mac(mc, dst);
			frame6_out(m, mc, pkt, len, tx, ctx);
			return;
		}
		m->drop_v6++;
		return;
	}
	hop = (!memcmp(dst, m->a6, 8) || is_ll6(dst)) ? dst : m->has_gw6 ? m->gw6 : 0;	/* on the link, or through the router */
	if (!hop) {
		m->drop_v6++;
		return;
	}
	if ((mac = nd6_get(m, now, hop))) {
		frame6_out(m, mac, pkt, len, tx, ctx);
		return;
	}
	memcpy(m->pend6, pkt, len);
	m->pend6len = len;
	memcpy(m->pend6_hop, hop, 16);
	m->pend6_t = now;
	m->pend6_asks = 1;
	m->ask6_t = now;
	ns_send(m, hop, tx, ctx);
}

/* An IPv6 frame from the line: neighbour discovery and router advertisements are this adapter's; a packet to this phone's address goes to the tunnel; the rest is not delivered. */
static void v6_from_line(lw_leaf *m, double now, const uint8_t *f, size_t len, lw_mtx tx, void *ctx, lw_mup up, void *uctx)
{
	const uint8_t *ip = f + 14;
	size_t pl;

	if (len < 14 + 40 || (ip[0] >> 4) != 6) {
		m->drop_line++;
		return;
	}
	pl = get16(ip + 4);
	if (14 + 40 + pl > len || (is_zero6(ip + 8) && ip[6] != 58) || ip[8] == 0xff) {
		m->drop_line++;
		return;
	}
	if (ip[6] == 58 && pl >= 8) {
		if (f[54] == 134) {
			ra_input(m, now, f, len);
			return;
		}
		if (f[54] == 135 || f[54] == 136) {
			nd6_input(m, now, f, len, tx, ctx);
			return;
		}
	}
	if (!m->has6 && !mine6(m, ip + 24)) {
		m->drop_line++;
		return;
	}
	if ((f[0] & 1) || !mine6(m, ip + 24) || mine6(m, ip + 8) || is_zero6(ip + 8)) {	/* chatter, or not to this phone */
		m->drop_line++;
		return;
	}
	up(uctx, ip, 40 + pl);
	m->os_out++;
}

/* ---- the clock -------------------------------------------------------------------- */

void leaf_dhcp_tick(lw_leaf *m, double now, lw_mtx tx, void *ctx);

void leaf_tick(lw_leaf *m, double now, lw_mtx tx, void *ctx)
{
	v6_tick(m, now, tx, ctx);
	if (m->pendlen && now - m->pend_t > 5.0)	/* nobody answered for the next hop */
		m->pendlen = 0;
	if (m->pendlen && now - m->ask_t >= 1.0 && m->pend_asks < 4) {
		m->ask_t = now;
		m->pend_asks++;
		arp_send(m, 1, MB_BCAST, m->ip, m->pend_hop, tx, ctx);
	}
	leaf_dhcp_tick(m, now, tx, ctx);
}

/* The DHCP client alone: what a desktop's jack runs (packaging/lw-dhcp.c, docs/adr/0102) and what the phone's adapter runs inside leaf_tick. One implementation, one retry rule. */
void leaf_dhcp_tick(lw_leaf *m, double now, lw_mtx tx, void *ctx)
{
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

/* A frame from the LAN, looked at only as DHCP: for the jack's client, which has no tunnel to hand IP packets to. */
void leaf_dhcp_input(lw_leaf *m, double now, const uint8_t *f, size_t len, lw_mtx tx, void *ctx)
{
	size_t ihl;

	if (len < 14 + 20 + 8 || get16(f + 12) != 0x0800 || (f[14] >> 4) != 4 || f[14 + 9] != 17)
		return;
	if (memcmp(f, m->mac, 6) && !(f[0] & 1))		/* another host's frame */
		return;
	ihl = (size_t)(f[14] & 15) * 4;
	if (ihl >= 20 && 14 + ihl + 4 <= len && get16(f + 14 + ihl + 2) == 68 && get16(f + 14 + ihl) == 67)
		dhcp_input(m, now, f, len, tx, ctx);
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
	if ((pkt[0] >> 4) == 6) {		/* IPv6 (docs/adr/0098): with an address from the router it goes out; without one the tunnel has no route for it, and this is what slips through */
		v6_from_os(m, now, pkt, len, tx, ctx);
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
	if (et == 0x86dd) {
		v6_from_line(m, now, f, len, tx, ctx, up, uctx);
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
		return !memcmp(ip, ula, 6) || (n->leaf->has6 && !memcmp(ip, n->leaf->a6, 16));
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
	char ip[20], mask[20], gw[20], d[3][20], ip6[48] = "", gw6[48] = "";
	static const char *const st[] = { "asking", "asking", "asking", "bound", "renewing" };
	int i, prefix = 0;

	if (!m)
		return snprintf(out, cap, "{\"error\":\"this node is not a member\"}\n");
	if (m->has6)
		inet_ntop(AF_INET6, m->a6, ip6, sizeof ip6);
	if (m->has6 && m->has_gw6)
		inet_ntop(AF_INET6, m->gw6, gw6, sizeof gw6);
	mb_addr(ip, sizeof ip, m->ip);
	mb_addr(mask, sizeof mask, m->mask);
	mb_addr(gw, sizeof gw, m->gw);
	for (i = 0; i < 3; i++)
		mb_addr(d[i], sizeof d[i], m->ndns > i ? m->dns[i] : 0);
	for (i = 31; i >= 0 && (m->mask >> i & 1); i--)
		prefix++;
	return snprintf(out, cap,
		"{\"member\":\"%s\",\"ip6\":\"%s\",\"prefix6\":%d,\"gateway6\":\"%s\",\"ra_seen\":%llu,\"nd_answered\":%llu,\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"gen\":%u,\"ip\":\"%s\",\"mask\":\"%s\",\"prefix\":%d,\"gateway\":\"%s\",\"dns\":[%s%s%s%s%s],\"domain\":\"%s\",\"mtu\":%d,\"lease\":%u,"
		"\"tun\":%s,\"os_in\":%llu,\"os_out\":%llu,\"drop_v6\":%llu,\"drop_os\":%llu,\"drop_line\":%llu,\"arp_answered\":%llu,\"dhcp_sent\":%llu}\n",
		st[m->state], ip6, m->has6 ? m->pfx6 : 0, gw6, (unsigned long long)m->ra_seen, (unsigned long long)m->nd_answered, m->mac[0], m->mac[1], m->mac[2], m->mac[3], m->mac[4], m->mac[5], m->gen,
		leaf_bound(m) ? ip : "", leaf_bound(m) ? mask : "", prefix, leaf_bound(m) ? gw : "",
		m->ndns > 0 ? "\"" : "", m->ndns > 0 ? d[0] : "", m->ndns > 1 ? "\",\"" : "", m->ndns > 1 ? d[1] : "", m->ndns > 0 ? "\"" : "",
		m->domain, m->mtu, m->lease, n->port_fd >= 0 ? "true" : "false",
		(unsigned long long)m->os_in, (unsigned long long)m->os_out, (unsigned long long)m->drop_v6, (unsigned long long)m->drop_os,
		(unsigned long long)m->drop_line, (unsigned long long)m->arp_answered, (unsigned long long)m->dhcp_sent);
}
