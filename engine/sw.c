/* SPDX-License-Identifier: GPL-3.0-or-later
 * The switch: learn, forward, flood, split horizon (docs/ARCHITECTURE.md §1),
 * and what a bridge over the internet needs on top: frames that don't fit one
 * datagram, TCP MSS clamping, a flood budget, ARP answered locally, and a
 * standby mode for a second box on the same LAN segment (site.c). */
#include <stdlib.h>
#include <string.h>
#include "int.h"

#define MAC_AGE 300.0
#define MAC_PROBES 8
#define MAC_STEAL_GUARD 10.0		/* seconds a locally learned address is held against a peer's frame that claims it */
#define ARP_AGE 120.0
#define FLOOD_RATE 1000.0		/* flooded frames a second sent to one peer ... */
#define FLOOD_BURST 200.0		/* ... after a burst of this many */

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
	if (port >= 0 && (e = mac_find(n, mac, 0)) && e->port == -1 && n->now - e->seen < MAC_STEAL_GUARD)
		return;				/* a host on our own LAN that spoke a moment ago keeps its address: a member cannot pull its traffic out over the line by saying it is
						 * that host (docs/adr/0063). A host that really moved stops speaking here, and the entry then moves; one that speaks here takes it back at once. */
	if ((e = mac_find(n, mac, 1))) {
		e->port = (int16_t)port;
		e->seen = n->now;
	}
}

int sw_count(const lw_node *n)
{
	int i, c = 0;

	for (i = 0; i < MAC_SLOTS; i++)
		if (n->macs[i].port != -2 && n->now - n->macs[i].seen <= MAC_AGE)
			c++;
	return c;
}

/* A frame from a virtual NIC (veth, virtio) can arrive with the TCP/UDP checksum only partly done: the
 * kernel knows the hardware will finish it. We are the hardware now: complete it, or the far LAN drops it. */
unsigned lw_sum16(const uint8_t *p, size_t n, unsigned sum)
{
	for (; n > 1; p += 2, n -= 2)
		sum += (unsigned)p[0] << 8 | p[1];
	if (n)
		sum += (unsigned)p[0] << 8;
	return sum;
}

void l4_csum_fix(uint8_t *f, size_t len)
{
	unsigned et, proto, sum, l4len, off;
	size_t l4;
	uint8_t *c;

	if (len < 14 + 20)
		return;
	et = (unsigned)f[12] << 8 | f[13];
	if (et == 0x0800) {
		size_t ihl = (size_t)(f[14] & 15) * 4;
		unsigned tl = (unsigned)f[16] << 8 | f[17];

		if ((f[14] >> 4) != 4 || ihl < 20 || tl < ihl || 14 + tl > len || ((f[20] & 0x3f) << 8 | f[21]))
			return;
		proto = f[23];
		l4 = 14 + ihl;
		l4len = tl - (unsigned)ihl;
		sum = lw_sum16(f + 26, 8, 0) + proto + l4len;
	} else if (et == 0x86dd) {
		unsigned pl = (unsigned)f[18] << 8 | f[19];

		if (len < 14 + 40 || 14 + 40 + pl > len)
			return;
		proto = f[20];
		l4 = 14 + 40;
		l4len = pl;
		sum = lw_sum16(f + 22, 32, 0) + proto + l4len;
	} else
		return;
	off = proto == 6 ? 16 : proto == 17 ? 6 : 0;
	if (!off || l4len < off + 2)
		return;
	c = f + l4 + off;
	c[0] = c[1] = 0;
	sum = lw_sum16(f + l4, l4len, sum);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	sum = ~sum & 0xffff;
	if (proto == 17 && !sum)
		sum = 0xffff;
	c[0] = (uint8_t)(sum >> 8);
	c[1] = (uint8_t)sum;
}


static void ip4_hdr_csum(uint8_t *f)
{
	size_t ihl = (size_t)(f[14] & 15) * 4;
	unsigned sum;

	f[24] = f[25] = 0;
	sum = lw_sum16(f + 14, ihl, 0);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	sum = ~sum & 0xffff;
	f[24] = (uint8_t)(sum >> 8);
	f[25] = (uint8_t)sum;
}

/* A NIC can hand us a coalesced TCP frame (GRO, or a virtual NIC's GSO) of up to 64 KB: far more than the line
 * carries. TCP does not care where segment boundaries fall, so cut it into line-sized segments, each a complete
 * frame with fresh lengths, sequence numbers and checksums, FIN and PSH only on the last. Returns the number of
 * pieces forwarded, 0 if it isn't TCP we can cut. */
int resegment(lw_node *n, const uint8_t *f, size_t len)
{
	uint8_t o[FRAME_MAX];
	size_t th, hl, plen, chunk, off, thl;
	unsigned et, seq, pieces = 0;
	int v6;

	if (len < 14 + 20 + 20)
		return 0;
	et = get16(f + 12);
	if (et == 0x0800) {
		size_t ihl = (size_t)(f[14] & 15) * 4;

		if ((f[14] >> 4) != 4 || ihl < 20 || len < 14 + ihl + 20 || f[23] != 6 || (get16(f + 20) & 0x3fff))
			return 0;
		th = 14 + ihl;
		v6 = 0;
		chunk = LINE_MSS4;
	} else if (et == 0x86dd) {
		if (len < 14 + 40 + 20 || f[20] != 6)
			return 0;
		th = 14 + 40;
		v6 = 1;
		chunk = LINE_MSS6;
	} else
		return 0;
	thl = (size_t)(f[th + 12] >> 4) * 4;
	if (thl < 20 || th + thl > len)
		return 0;
	hl = th + thl;
	plen = len - hl;
	if (!plen || hl + chunk > FRAME_MAX)
		return 0;
	seq = (unsigned)f[th + 4] << 24 | (unsigned)f[th + 5] << 16 | (unsigned)f[th + 6] << 8 | f[th + 7];
	for (off = 0; off < plen; off += chunk, pieces++) {
		size_t c = plen - off < chunk ? plen - off : chunk;
		unsigned s2 = seq + (unsigned)off;
		int last = off + c >= plen;

		memcpy(o, f, hl);
		memcpy(o + hl, f + hl + off, c);
		o[th + 4] = (uint8_t)(s2 >> 24); o[th + 5] = (uint8_t)(s2 >> 16);
		o[th + 6] = (uint8_t)(s2 >> 8);  o[th + 7] = (uint8_t)s2;
		if (!last)
			o[th + 13] &= (uint8_t)~0x09;	/* FIN and PSH belong to the last piece */
		if (off)
			o[th + 13] &= (uint8_t)~0x80;	/* so does CWR... only the first carries it */
		if (v6) {
			put16(o + 18, (unsigned)(thl + c));
		} else {
			put16(o + 16, (unsigned)(hl - 14 + c));
			put16(o + 18, get16(f + 18) + (unsigned)pieces);
			ip4_hdr_csum(o);
		}
		l4_csum_fix(o, hl + c);
		sw_from_local(n, o, hl + c);
	}
	return (int)pieces;
}

/* ---- TCP MSS clamp -------------------------------------------------------------
 * LAN hosts believe in a 1500-byte MTU. The line's is smaller, so SYNs that cross
 * it get their MSS option lowered; almost all TCP then fits one datagram. */

static void mss_clamp(uint8_t *f, size_t len)
{
	size_t th, doff, o;
	unsigned et, lim, mss, hc, sum;

	if (len < 54)
		return;
	et = get16(f + 12);
	if (et == 0x0800) {
		size_t ihl = (size_t)(f[14] & 15) * 4;

		if ((f[14] >> 4) != 4 || ihl < 20 || len < 14 + ihl + 20 || f[23] != 6 || (get16(f + 20) & 0x1fff))
			return;
		th = 14 + ihl;
		lim = LINE_MSS4;
	} else if (et == 0x86dd) {
		if (len < 14 + 40 + 20 || f[20] != 6)
			return;
		th = 14 + 40;
		lim = LINE_MSS6;
	} else
		return;
	if (!(f[th + 13] & 0x02))		/* SYN (and SYN-ACK) only */
		return;
	doff = (size_t)(f[th + 12] >> 4) * 4;
	if (doff < 20 || th + doff > len)
		return;
	for (o = th + 20; o + 1 < th + doff;) {
		unsigned kind = f[o], l;

		if (kind == 0)
			return;
		if (kind == 1) {
			o++;
			continue;
		}
		l = f[o + 1];
		if (l < 2 || o + l > th + doff)
			return;
		if (kind == 2 && l == 4) {
			mss = get16(f + o + 2);
			if (mss <= lim)
				return;
			hc = get16(f + th + 16);	/* RFC 1624 incremental checksum update */
			sum = (~hc & 0xffff) + (~mss & 0xffff) + lim;
			sum = (sum & 0xffff) + (sum >> 16);
			sum = (sum & 0xffff) + (sum >> 16);
			put16(f + th + 16, ~sum & 0xffff);
			put16(f + o + 2, lim);
			return;
		}
		o += l;
	}
}

/* ---- ARP: answer for hosts that are across the line ---------------------------- */

#define ARP_OFF 14
static int is_arp(const uint8_t *f, size_t len)
{
	return len >= 42 && get16(f + 12) == 0x0806 && get16(f + ARP_OFF) == 1 && get16(f + ARP_OFF + 2) == 0x0800 &&
	       f[ARP_OFF + 4] == 6 && f[ARP_OFF + 5] == 4;
}

static uint32_t ip_of(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void arp_learn(lw_node *n, const uint8_t *f, size_t len)
{
	uint32_t ip;
	int i;

	if (!is_arp(f, len))
		return;
	ip = ip_of(f + ARP_OFF + 14);
	if (!ip)
		return;
	i = (int)((ip * 2654435761u) >> 24) & 255;
	n->arp[i].ip = ip;
	memcpy(n->arp[i].mac, f + ARP_OFF + 8, 6);
	n->arp[i].t = n->now;
}

/* Two DHCP servers on one line hand out clashing leases. Offers pass through the switch, so watch them: a second
 * server's offer within ten minutes of the first's is reported, at most once in ten minutes per server. */
static void dhcp_watch(lw_node *n, const uint8_t *f, size_t len)
{
	size_t ip, o;
	uint32_t src;
	int i, slot = -1, other = -1;

	if (len < 14 + 20 + 8 + 240 + 4 || get16(f + 12) != 0x0800 || (f[14] >> 4) != 4 || f[14 + 9] != 17)
		return;
	ip = 14 + (size_t)(f[14] & 15) * 4;
	if (ip + 8 + 241 > len || get16(f + ip) != 67 || get16(f + ip + 2) != 68 || f[ip + 8] != 2)
		return;
	for (o = ip + 8 + 240; o + 2 < len && f[o] != 255;) {		/* options: find 53, must be an offer */
		if (f[o] == 0) {
			o++;
			continue;
		}
		if (o + 2 + f[o + 1] > len)
			return;
		if (f[o] == 53)
			break;
		o += 2 + f[o + 1];
	}
	if (o + 2 >= len || f[o] != 53 || f[o + 1] != 1 || f[o + 2] != 2)
		return;
	src = ip_of(f + 14 + 12);
	if (!src)
		return;
	for (i = 0; i < 4; i++) {
		if (n->dhcp[i].ip == src)
			slot = i;
		else if (n->dhcp[i].ip && n->now - n->dhcp[i].t < 600)
			other = i;
	}
	if (slot < 0)
		for (i = 0; i < 4 && slot < 0; i++)
			if (!n->dhcp[i].ip || n->now - n->dhcp[i].t >= 600)
				slot = i;
	if (slot < 0)
		slot = 0;
	if (n->dhcp[slot].ip != src)
		n->dhcp[slot].said = 0;
	n->dhcp[slot].ip = src;
	n->dhcp[slot].t = n->now;
	if (other >= 0 && n->now - n->dhcp[slot].said >= 600 && n->now - n->dhcp_say >= 30.0) {	/* and never more than one a half-minute: frames are free to forge */
		n->dhcp[slot].said = n->now;
		n->dhcp_say = n->now;
		n->dhcp_conflicts++;
		ev_emit(n, "{\"event\":\"dhcp-conflict\",\"servers\":[\"%u.%u.%u.%u\",\"%u.%u.%u.%u\"]}",
			(unsigned)(src >> 24), (unsigned)(src >> 16 & 255), (unsigned)(src >> 8 & 255), (unsigned)(src & 255),
			(unsigned)(n->dhcp[other].ip >> 24), (unsigned)(n->dhcp[other].ip >> 16 & 255),
			(unsigned)(n->dhcp[other].ip >> 8 & 255), (unsigned)(n->dhcp[other].ip & 255));
	}
}

/* A local host asks who has an address we know lives across the line: say so
 * from here, and the question never crosses the WAN. */
static int arp_suppress(lw_node *n, const uint8_t *f, size_t len)
{
	uint32_t tip, sip;
	uint8_t r[42];
	mac_ent *e;
	int i;

	if (!is_arp(f, len) || get16(f + ARP_OFF + 6) != 1)
		return 0;
	sip = ip_of(f + ARP_OFF + 14);
	tip = ip_of(f + ARP_OFF + 24);
	if (!sip || sip == tip)			/* a probe or an announcement: let it cross */
		return 0;
	i = (int)((tip * 2654435761u) >> 24) & 255;
	if (n->arp[i].ip != tip || n->now - n->arp[i].t > ARP_AGE)
		return 0;
	if (!(e = mac_find(n, n->arp[i].mac, 0)) || e->port < 0)	/* only for hosts that are really remote */
		return 0;
	memcpy(r, f + 6, 6);			/* to the asker, from the host it asked about */
	memcpy(r + 6, n->arp[i].mac, 6);
	put16(r + 12, 0x0806);
	memcpy(r + 14, f + 14, 6);		/* htype, ptype, hlen, plen */
	put16(r + 20, 2);			/* reply */
	memcpy(r + 22, n->arp[i].mac, 6);
	memcpy(r + 28, f + ARP_OFF + 24, 4);
	memcpy(r + 32, f + ARP_OFF + 8, 6);
	memcpy(r + 38, f + ARP_OFF + 14, 4);
	port_write(n, r, sizeof r);
	n->arp_answered++;
	return 1;
}

/* ---- Neighbour discovery: ARP's IPv6 counterpart ----------------------------------- */

/* The ICMPv6 header of a neighbour solicitation (135) or advertisement (136), or 0. Hop limit 255 is how ND says it came
 * from the link. Layout: Ethernet 14, IPv6 40, ICMPv6 at 54: type, code, checksum, flags 4, target 16, options. */
#define ND_OFF 54
static int nd_type(const uint8_t *f, size_t len)
{
	if (len < ND_OFF + 24 || get16(f + 12) != 0x86dd || (f[14] >> 4) != 6 || f[20] != 58 || f[21] != 255)
		return 0;
	if ((f[ND_OFF] != 135 && f[ND_OFF] != 136) || f[ND_OFF + 1] != 0)
		return 0;
	return f[ND_OFF];
}

/* The link-layer address option of the given kind (1 source, 2 target), if the message has one. */
static const uint8_t *nd_lladdr(const uint8_t *f, size_t len, int kind)
{
	size_t o = ND_OFF + 24;

	while (o + 8 <= len) {
		if (f[o + 1] == 0)
			return 0;
		if (f[o] == kind && f[o + 1] == 1)
			return f + o + 2;
		o += (size_t)f[o + 1] * 8;
	}
	return 0;
}

static int nd_slot(const uint8_t ip[16])
{
	uint32_t w;

	memcpy(&w, ip + 12, 4);
	return (int)((w * 2654435761u) >> 24) & 255;
}

static void nd_learn(lw_node *n, const uint8_t *f, size_t len)
{
	static const uint8_t none[16] = {0};
	const uint8_t *ip, *mac;
	int t = nd_type(f, len), i;

	if (!t)
		return;
	if (t == 135) {				/* the asker says who it is: its address and where it sits */
		ip = f + 22;
		mac = nd_lladdr(f, len, 1);
	} else {				/* an advertisement says whose the target is */
		ip = f + ND_OFF + 8;
		mac = nd_lladdr(f, len, 2);
	}
	if (!mac)
		mac = f + 6;
	if (!memcmp(ip, none, 16) || ip[0] == 0xff)
		return;
	i = nd_slot(ip);
	memcpy(n->nd[i].ip, ip, 16);
	memcpy(n->nd[i].mac, mac, 6);
	n->nd[i].t = n->now;
}

/* A solicitation for a host across the line is answered here, as ARP is, instead of crossing as a flood. */
static int nd_suppress(lw_node *n, const uint8_t *f, size_t len)
{
	static const uint8_t none[16] = {0};
	uint8_t r[14 + 40 + 32];
	const uint8_t *tgt = f + ND_OFF + 8;
	unsigned sum;
	mac_ent *e;
	int i;

	if (nd_type(f, len) != 135 || !memcmp(f + 22, none, 16))	/* duplicate address detection must cross */
		return 0;
	i = nd_slot(tgt);
	if (memcmp(n->nd[i].ip, tgt, 16) || n->now - n->nd[i].t > ARP_AGE)
		return 0;
	if (!(e = mac_find(n, n->nd[i].mac, 0)) || e->port < 0)	/* only for hosts that are really remote */
		return 0;
	memcpy(r, f + 6, 6);
	memcpy(r + 6, n->nd[i].mac, 6);
	put16(r + 12, 0x86dd);
	r[14] = 0x60;
	r[15] = r[16] = r[17] = 0;
	put16(r + 18, 32);
	r[20] = 58;
	r[21] = 255;
	memcpy(r + 22, tgt, 16);		/* from the target, to the asker */
	memcpy(r + 38, f + 22, 16);
	r[ND_OFF] = 136;
	r[ND_OFF + 1] = 0;
	r[ND_OFF + 2] = r[ND_OFF + 3] = 0;
	r[ND_OFF + 4] = 0x60;			/* solicited, override */
	r[ND_OFF + 5] = r[ND_OFF + 6] = r[ND_OFF + 7] = 0;
	memcpy(r + ND_OFF + 8, tgt, 16);
	r[ND_OFF + 24] = 2;			/* target link-layer address */
	r[ND_OFF + 25] = 1;
	memcpy(r + ND_OFF + 26, n->nd[i].mac, 6);
	sum = lw_sum16(r + 22, 32, 0) + 32 + 58;
	sum = lw_sum16(r + ND_OFF, 32, sum);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	put16(r + ND_OFF + 2, ~sum & 0xffff);
	port_write(n, r, sizeof r);
	n->arp_answered++;
	return 1;
}

int sw_remote_macs(lw_node *n, uint8_t out[][6], int max)
{
	int i, c = 0;

	for (i = 0; i < MAC_SLOTS && c < max; i++)
		if (n->macs[i].port >= 0 && n->now - n->macs[i].seen <= 120.0)
			memcpy(out[c++], n->macs[i].mac, 6);
	return c;
}

/* After a takeover: tell the LAN's switches where the remote hosts are now. */
void sw_gratuitous(lw_node *n)
{
	int i;

	for (i = 0; i < 256; i++) {
		uint8_t g[42];
		mac_ent *e;

		if (!n->arp[i].ip || n->now - n->arp[i].t > ARP_AGE)
			continue;
		if (!(e = mac_find(n, n->arp[i].mac, 0)) || e->port < 0)
			continue;
		memset(g, 0xff, 6);
		memcpy(g + 6, n->arp[i].mac, 6);
		put16(g + 12, 0x0806);
		put16(g + 14, 1);
		put16(g + 16, 0x0800);
		g[18] = 6;
		g[19] = 4;
		put16(g + 20, 1);
		memcpy(g + 22, n->arp[i].mac, 6);
		g[28] = (uint8_t)(n->arp[i].ip >> 24); g[29] = (uint8_t)(n->arp[i].ip >> 16);
		g[30] = (uint8_t)(n->arp[i].ip >> 8);  g[31] = (uint8_t)n->arp[i].ip;
		memset(g + 32, 0, 6);
		memcpy(g + 38, g + 28, 4);
		port_write(n, g, sizeof g);
	}
}

/* ---- sending -------------------------------------------------------------------- */

static void send_one(lw_node *n, lw_peer *p, const uint8_t *f, size_t len)
{
	if (len <= FRAME_SINGLE) {
		if (!sess_send(n, p, K_FRAME, f, len)) {
			p->tx_frames++;
			p->tx_bytes += len;
		}
		return;
	}
	{	/* two datagrams: id(2) part(1) total(2), then half the frame each */
		uint8_t b[5 + FRAME_MAX / 2 + 2];
		size_t h = (len + 1) / 2;
		uint16_t id = n->frag_id++;

		put16(b, id);
		b[2] = 0;
		put16(b + 3, (unsigned)len);
		memcpy(b + 5, f, h);
		if (sess_send(n, p, K_FRAG, b, 5 + h))
			return;
		b[2] = 1;
		memcpy(b + 5, f + h, len - h);
		sess_send(n, p, K_FRAG, b, 5 + len - h);
		p->tx_frames++;
		p->tx_bytes += len;
		n->frames_split++;
	}
}

static void to_peer(lw_node *n, int pi, const uint8_t *f, size_t len)
{
	lw_peer *p = &n->peers[pi];

	if (!p->is_sb && p->cur.valid)
		send_one(n, p, f, len);
}

/* The budget for frames we flood to one peer: a broadcast storm costs the WAN almost nothing. */
static int flood_ok(lw_node *n, lw_peer *p)
{
	p->bc_tok += (n->now - p->bc_t) * FLOOD_RATE;
	if (p->bc_tok > FLOOD_BURST)
		p->bc_tok = FLOOD_BURST;
	p->bc_t = n->now;
	if (p->bc_tok < 1.0) {
		n->drop_flood++;
		return 0;
	}
	p->bc_tok -= 1.0;
	return 1;
}

/* A piece of a split frame; delivers when both halves are in. */
void sw_fragment(lw_node *n, int pi, const uint8_t *b, size_t len, int ctl)
{
	lw_peer *p = &n->peers[pi];
	unsigned id, part, total, h;
	int i, slot = -1;

	if (len < 6 || len > 5 + (ctl ? CTL_BUFMAX : FRAME_MAX))
		return;
	id = get16(b);
	part = b[2];
	total = get16(b + 3);
	h = (total + 1) / 2;
	if (part > 1 || total <= FRAME_SINGLE || total > (ctl ? CTL_BUFMAX : FRAME_MAX) || len - 5 != (part ? total - h : h))
		return;				/* not what we send */
	if (!p->fr && !(p->fr = calloc(1, sizeof *p->fr)))
		return;
	for (i = 0; i < 4; i++) {
		if (p->fr->s[i].used && n->now - p->fr->s[i].t > 2.0)
			p->fr->s[i].used = 0;
		if (p->fr->s[i].used && p->fr->s[i].id == id) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		for (i = 0; i < 4; i++)
			if (!p->fr->s[i].used || slot < 0) {
				slot = i;
				if (!p->fr->s[i].used)
					break;
			}
	if (!p->fr->s[slot].used || p->fr->s[slot].id != id) {
		memset(&p->fr->s[slot], 0, sizeof p->fr->s[slot]);
		p->fr->s[slot].used = 1;
		p->fr->s[slot].ctl = ctl;
		p->fr->s[slot].id = (uint16_t)id;
		p->fr->s[slot].total = (int)total;
		p->fr->s[slot].t = n->now;
	}
	if (p->fr->s[slot].total != (int)total || p->fr->s[slot].ctl != ctl)
		return;
	memcpy(p->fr->s[slot].buf + (part ? h : 0), b + 5, len - 5);
	p->fr->s[slot].got |= 1 << part;
	if (p->fr->s[slot].got == 3 && ctl) {		/* a control message, put back together: handled as if it had come whole */
		uint8_t *m = p->fr->s[slot].buf;

		p->fr->s[slot].used = 0;
		if (total >= 3 && (size_t)get16(m + 1) + 3 == total)
			node_msg(n, p, m[0], m + 3, total - 3);
	} else if (p->fr->s[slot].got == 3) {
		p->fr->s[slot].used = 0;
		n->frames_joined++;
		p->rx_frames++;
		p->rx_bytes += total;
		sw_from_peer(n, pi, p->fr->s[slot].buf, total);
	}
}

/* ---- the switch ------------------------------------------------------------------ */

/* ---- what a bridge never carries -------------------------------------------------------------------------------------------------------------------------------------------------- */

/* The link-local control frames are for the wire they were sent on, whatever they are: spanning tree (BPDUs), pause and LACP, 802.1X, LLDP and the rest of 01:80:C2:00:00:00 to 0F, and Cisco's
 * CDP, VTP, DTP and PVST+ (01:00:0C:CC:CC:CC, :CD, and 01:00:0C:CD:CD:CD). A bridge consumes them and sends its own. Carried over a line they join two sites' spanning trees (a root over there becomes
 * a root here), trip BPDU guard on the port the box is on, and show a switch at one site as a neighbour of another's. They cross in neither direction (docs/adr/0083). */
static int ctrl_frame(const uint8_t *f)
{
	if (f[0] == 0x01 && f[1] == 0x80 && f[2] == 0xc2 && f[3] == 0 && f[4] == 0 && f[5] <= 0x0f)
		return 1;
	if (f[0] == 0x01 && f[1] == 0x00 && f[2] == 0x0c)
		return (f[3] == 0xcc && f[4] == 0xcc && (f[5] == 0xcc || f[5] == 0xcd)) || (f[3] == 0xcd && f[4] == 0xcd && f[5] == 0xcd);
	return 0;
}

/* Two boxes on one segment that both forward (the election failed, or one has just joined and not yet got its secret) carry each frame twice, and put each other's frames back on the LAN: a loop. The
 * address a frame is from cannot say whose it is (another box's copy of a host's frame is the host's frame, byte for byte), so the boxes remember what they have carried, by the bytes: a frame from a member
 * whose bytes are those of a frame that came off our LAN in the last second was already on it, and a frame off our LAN whose bytes are those of one a member has just handed us was put there by that member.
 * Either way the member is **on our segment** (a member of the line at another site cannot have done it), and nothing is carried between it and the LAN for a minute, which ends the loop whatever the two boxes
 * believe about each other. The copy itself is dropped at once. Three in five seconds make a member a segment-mate: a stray collision (two sites that share a MAC) must not cut a member off. Nothing is done
 * to a frame that has no twin from the other side, so a host's repeated frames, a burst of identical datagrams, a retransmission, are never touched. Remembered by a 64-bit hash of the bytes, in a table of 4096. */
#define SEEN_N 4096
#define SEEN_FOR 1.0
#define MATE_FOR 60.0
static uint64_t frame_hash(const uint8_t *f, size_t len)
{
	uint64_t h = 0x9e3779b97f4a7c15ull ^ len, w;
	size_t i = 0;

	for (; i + 8 <= len; i += 8) {
		memcpy(&w, f + i, 8);
		h = (h ^ w) * 0xff51afd7ed558ccdull;
		h ^= h >> 32;
	}
	for (w = 0; i < len; i++)
		w = w << 8 | f[i];
	h = (h ^ w) * 0xc4ceb9fe1a85ec53ull;
	h ^= h >> 29;
	return h ? h : 1;
}

static int is_mate(const lw_node *n, int pi)
{
	return n->peers[pi].mate_until > n->now;
}

static void mate_note(lw_node *n, int pi)
{
	lw_peer *p = &n->peers[pi];

	if (n->now - p->mate_t > 5.0)
		p->mate_hits = 0;
	p->mate_t = n->now;
	if (++p->mate_hits < 3)
		return;
	if (!is_mate(n, pi)) {
		char num[16] = "";

		if (p->number[0])
			fmt_number(num, p->number);
		ev_emit(n, "{\"event\":\"loop-guard\",\"number\":\"%s\"}", num);
	}
	p->mate_until = n->now + MATE_FOR;
}

#define CLAIM_FOR 120.0		/* how long an address said on our own LAN is a local host's: ARP caches expire about as fast */

/* The address a frame claims for its sender, if it is ARP or neighbour discovery: 4 an ARP sender's IPv4 address (in ip[0..3], the rest zero), 6 an IPv6 address a solicitation comes
 * from or an advertisement says is the sender's (a priority tag and the extension headers are looked through, as the guard does). 0: nothing, or an address that says nothing
 * (unspecified, multicast). */
static int claim_of(const uint8_t *f, size_t len, uint8_t ip[16])
{
	static const uint8_t none[16] = {0};
	size_t o = 12, l4;
	unsigned et, nh;
	int i;

	memset(ip, 0, 16);
	for (i = 0; i < 2; i++) {
		if (o + 2 > len)
			return 0;
		et = get16(f + o);
		if (et != 0x8100 && et != 0x88a8)
			break;
		o += 4;
	}
	if (o + 2 > len)
		return 0;
	et = get16(f + o);
	o += 2;
	if (et == 0x0806) {
		if (o + 28 > len || get16(f + o) != 1 || get16(f + o + 2) != 0x0800 || f[o + 4] != 6 || f[o + 5] != 4)
			return 0;
		memcpy(ip, f + o + 14, 4);
		return (ip[0] | ip[1] | ip[2] | ip[3]) ? 4 : 0;
	}
	if (et != 0x86dd || o + 40 > len || (f[o] >> 4) != 6)
		return 0;
	nh = f[o + 6];
	l4 = o + 40;
	for (i = 0; i < 8; i++) {
		if (nh == 0 || nh == 43 || nh == 60) {		/* hop-by-hop, routing, destination options */
			if (l4 + 8 > len)
				return 0;
			nh = f[l4];
			l4 += ((size_t)f[l4 + 1] + 1) * 8;
		} else if (nh == 44) {				/* fragment */
			if (l4 + 8 > len || (get16(f + l4 + 2) & 0xfff8))
				return 0;
			nh = f[l4];
			l4 += 8;
		} else if (nh == 51) {				/* authentication header */
			if (l4 + 8 > len)
				return 0;
			nh = f[l4];
			l4 += ((size_t)f[l4 + 1] + 2) * 4;
		} else
			break;
	}
	if (nh != 58 || l4 + 24 > len || f[l4 + 1] != 0)
		return 0;
	if (f[l4] == 135)				/* a solicitation: the asker says whose address it comes from */
		memcpy(ip, f + o + 8, 16);
	else if (f[l4] == 136)				/* an advertisement says whose the target is */
		memcpy(ip, f + l4 + 8, 16);
	else
		return 0;
	return memcmp(ip, none, 16) && ip[0] != 0xff ? 6 : 0;
}

static unsigned claim_slot(const uint8_t *ip)
{
	uint32_t h = 2166136261u;
	int i;

	for (i = 0; i < 16; i++)
		h = (h ^ ip[i]) * 16777619u;
	return h & 255;
}

/* A host of our own LAN said it holds this address. */
static void claim_note(lw_node *n, const uint8_t *f, size_t len)
{
	uint8_t ip[16];
	unsigned i;

	if (!claim_of(f, len, ip))
		return;
	i = claim_slot(ip);
	memcpy(n->lan_claim[i].ip, ip, 16);
	n->lan_claim[i].t = n->now;
}

/* A member says it holds an address that a host of our own LAN said it holds a moment ago: refused, and said (once a minute per member: frames are free to forge). The switch's other
 * protections stop a member taking a local host's MAC; this is the address (ADR 0090). */
static int claim_refused(lw_node *n, int pi, const uint8_t *f, size_t len)
{
	uint8_t ip[16];
	unsigned i;
	int k;
	lw_peer *p = &n->peers[pi];

	if (!(k = claim_of(f, len, ip)))
		return 0;
	i = claim_slot(ip);
	if (memcmp(n->lan_claim[i].ip, ip, 16) || n->now - n->lan_claim[i].t >= CLAIM_FOR)
		return 0;
	n->drop_claim++;
	if (n->now - p->claim_t >= 60.0 || p->claim_t == 0) {
		char num[16] = "", a[48];

		p->claim_t = n->now ? n->now : 1.0;
		if (p->number[0])
			fmt_number(num, p->number);
		if (k == 4)
			snprintf(a, sizeof a, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
		else
			lw_hex(a, ip, 16);
		ev_emit(n, "{\"event\":\"address-claim\",\"number\":\"%s\",\"what\":\"%s\",\"address\":\"%s\"}", num, k == 4 ? "ARP" : "neighbour discovery", a);
	}
	return 1;
}

/* Has the other side already carried these bytes this second? Remembered as ours either way. dir: 0 from the LAN, 1 from member `pi`. */
static int seen_before(lw_node *n, const uint8_t *f, size_t len, int dir, int pi)
{
	uint64_t h = frame_hash(f, len);
	struct seen_ent *s;
	int dup;

	if (!n->seen && !(n->seen = calloc(SEEN_N, sizeof *n->seen)))
		return 0;
	s = &n->seen[h % SEEN_N];
	dup = s->h == h && s->dir != dir && n->now - s->t < SEEN_FOR;
	if (dup) {
		n->drop_echo++;
		if (dir == 1)
			mate_note(n, pi);		/* it handed us what came off our LAN */
		else if (s->peer >= 0 && s->peer < n->maxpeers)
			mate_note(n, s->peer);		/* what it handed us is what came off our LAN */
		return 1;				/* the entry stays as it was: the next copy (a third box's) is one too */
	}
	s->h = h;
	s->dir = (uint8_t)dir;
	s->t = n->now;
	s->peer = (int16_t)(dir ? pi : -1);
	return 0;
}

/* A member's frame whose source is a host that spoke on our own LAN a moment ago has come round, whatever its bytes. */
static int echo_from_peer(lw_node *n, const uint8_t *src)
{
	mac_ent *e;

	if ((src[0] & 1) || !(e = mac_find(n, src, 0)) || e->port != -1 || n->now - e->seen >= MAC_STEAL_GUARD)
		return 0;
	n->drop_echo++;
	return 1;
}

void sw_from_local(lw_node *n, const uint8_t *in, size_t len)
{
	uint8_t f[FRAME_MAX];
	mac_ent *e;
	int i;

	if (len < 14 || len > FRAME_MAX) {
		n->drop_garbage++;
		return;
	}
	if (ctrl_frame(in)) {			/* for the wire it came on: neither seen nor sent over the line */
		n->drop_ctrl++;
		return;
	}
	if (n->splice && site_input(n, in, len))	/* another box on this segment announcing itself */
		return;
	n->local_rx++;
	if (n->standby) {			/* a second box on the segment is the one forwarding */
		n->drop_standby++;
		return;
	}

	if (seen_before(n, in, len, 0, -1))		/* what a member of ours has just put on the LAN, come back: another box's copy. Not forwarded, and nothing learned from it */
		return;
	claim_note(n, in, len);			/* the addresses our own hosts hold: a member may not say it holds one */
	memcpy(f, in, len);
	mss_clamp(f, len);
	learn(n, f + 6, -1);
	arp_learn(n, f, len);
	nd_learn(n, f, len);
	dhcp_watch(n, f, len);
	if (arp_suppress(n, f, len) || nd_suppress(n, f, len))
		return;
	if (exit_frame(n, f, len))		/* for the exit gateway: the chosen member's, or nobody's (docs/adr/0056) */
		return;
	if (!(f[0] & 1) && (e = mac_find(n, f, 0))) {
		if (e->port == -1)
			return;			/* local to local: never leaves the site */
		if (n->peers[e->port].used && n->peers[e->port].cur.valid && !is_mate(n, e->port)) {
			to_peer(n, e->port, f, len);
			return;
		}			/* its peer is gone: flood, it may have moved */
	}
	n->flood++;
	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !n->peers[i].is_sb && n->peers[i].cur.valid && !is_mate(n, i) && flood_ok(n, &n->peers[i]))
			to_peer(n, i, f, len);
}

/* ---- the guard: who may speak for the network ---------------------------------------------------------------------------------------------------------------------------------- */

/* What a frame is, if it is something only the network's own servers say: 1 a DHCPv4 reply (UDP 67 to 68), 2 a DHCPv6 reply (547 to 546), 3 a router advertisement or a redirect (ICMPv6 134, 137), 4 an IPv6 frame
 * whose upper layer cannot be found (a chain of extension headers that runs off the frame), which is refused rather than guessed. Priority tags (VLAN 0, and a second one) are looked through; a fragment that is
 * not the first has no header to read and is let go (without its first it cannot be put together). 0: nothing of the kind. */
static int guard_kind(const uint8_t *f, size_t len)
{
	size_t o = 12, l4;
	unsigned et, nh;
	int i;

	for (i = 0; i < 2; i++) {
		if (o + 2 > len)
			return 0;
		et = get16(f + o);
		if (et != 0x8100 && et != 0x88a8)
			break;
		o += 4;
	}
	if (o + 2 > len)
		return 0;
	et = get16(f + o);
	o += 2;
	if (et == 0x0800) {
		if (o + 20 > len || (f[o] >> 4) != 4 || f[o + 9] != 17 || (get16(f + o + 6) & 0x1fff))
			return 0;
		l4 = o + (size_t)(f[o] & 15) * 4;
		if (l4 + 8 > len)
			return 0;
		return get16(f + l4) == 67 && get16(f + l4 + 2) == 68 ? 1 : 0;
	}
	if (et != 0x86dd)
		return 0;
	if (o + 40 > len || (f[o] >> 4) != 6)
		return 0;
	nh = f[o + 6];
	l4 = o + 40;
	for (i = 0; i < 8; i++) {
		if (nh == 0 || nh == 43 || nh == 60) {			/* hop-by-hop, routing, destination options */
			if (l4 + 8 > len)
				return 4;
			nh = f[l4];
			l4 += ((size_t)f[l4 + 1] + 1) * 8;
		} else if (nh == 44) {					/* fragment */
			if (l4 + 8 > len)
				return 4;
			if (get16(f + l4 + 2) & 0xfff8)
				return 0;
			nh = f[l4];
			l4 += 8;
		} else if (nh == 51) {					/* authentication header: length in 4-byte units less two */
			if (l4 + 8 > len)
				return 4;
			nh = f[l4];
			l4 += ((size_t)f[l4 + 1] + 2) * 4;
		} else
			break;
	}
	if (nh == 0 || nh == 43 || nh == 60 || nh == 44 || nh == 51)
		return 4;
	if (nh == 17)
		return l4 + 8 <= len && get16(f + l4) == 547 && get16(f + l4 + 2) == 546 ? 2 : 0;
	if (nh == 58)
		return l4 + 1 <= len && (f[l4] == 134 || f[l4] == 137) ? 3 : 0;
	return 0;
}

/* May this member's frame go to our own side? A frame of those kinds is the network telling us how to be on it: a lease, a router. Only a member we asked for, or one the configuration names, may say it. Without
 * this a member who rang us, or one the mesh joined to us, could answer a machine's DHCP and be its gateway: the switch's own protections (a member cannot take a local host's address) do not stop a *server*. */
int peer_asked(const lw_node *n, const lw_peer *p)
{
	int i;

	static const uint8_t none[32] = {0};

	if (p->asked)
		return 1;
	if (n->line.have && memcmp(n->line.founder, none, 32) && !memcmp(p->key_id, n->line.founder, 32))	/* the founder of the line the person joined: joining it was the choice (ADR 0101) */
		return 1;
	for (i = 0; p->number[0] && i < 8; i++)
		if (!strcmp(n->asked[i], p->number))
			return 1;
	return 0;
}

static int guard_ok(lw_node *n, int pi, const uint8_t *f, size_t len)
{
	const char *kind = n->cfg.port_kind;
	int k, i, on = n->cfg.dhcp_guard == 1 || (n->cfg.dhcp_guard == 0 && (!strcmp(kind, "splice") || !strcmp(kind, "jack") || !strcmp(kind, "member")));
	lw_peer *p = &n->peers[pi];
	static const char *const what[] = { "", "DHCP", "DHCPv6", "a router advertisement", "an IPv6 frame that cannot be read" };

	if (!on || !(k = guard_kind(f, len)) || peer_asked(n, p))
		return 1;
	for (i = 0; i < n->cfg.n_dhcp; i++)
		if (!crypto_verify32(n->cfg.dhcp_servers[i], p->key_id))
			return 1;
	for (i = 0; i < 16; i++)			/* a member we asked for has said this one is on its own segment: the same site, the other box of a pair */
		if (!crypto_verify32(n->vouched[i], p->key_id))
			return 1;
	n->dhcp_guarded++;
	if (n->now - p->guard_t >= 60.0 || p->guard_t == 0) {		/* said at most once a minute per member: frames are free to forge */
		char num[16] = "";

		p->guard_t = n->now ? n->now : 1.0;
		if (p->number[0])
			fmt_number(num, p->number);
		ev_emit(n, "{\"event\":\"dhcp-blocked\",\"number\":\"%s\",\"what\":\"%s\"}", num, what[k]);
	}
	return 0;
}

void sw_from_peer(lw_node *n, int pi, const uint8_t *f, size_t len)
{
	mac_ent *e;

	if (len < 14) {
		n->drop_garbage++;
		return;
	}
	if (f[12] == 0x88 && f[13] == 0xb5) {	/* the boxes of a site talk to each other on their own segment (site.c) and never over the line: a member that holds the line key can
						 * make a valid probe, and a frame like that from the line would put the site's other box in standby (docs/adr/0063) */
		n->drop_garbage++;
		return;
	}
	if (ctrl_frame(f)) {			/* a member's spanning tree is not ours (and a box's own never goes over) */
		n->drop_ctrl++;
		return;
	}
	if (is_mate(n, pi)) {			/* on our own segment: whatever it carries is on the LAN already */
		n->drop_echo++;
		return;
	}
	if (!guard_ok(n, pi, f, len))		/* before anything is learned from it */
		return;
	if (claim_refused(n, pi, f, len))	/* an address a host of our own LAN holds, claimed by a member */
		return;
	if (seen_before(n, f, len, 1, pi) || echo_from_peer(n, f + 6))	/* a frame we already have from the LAN, or one from a host of our own LAN: it has come round */
		return;
	learn(n, f + 6, pi);
	arp_learn(n, f, len);
	nd_learn(n, f, len);
	dhcp_watch(n, f, len);
	if (!(f[0] & 1) && (e = mac_find(n, f, 0)) && e->port >= 0) {
		n->drop_noroute++;		/* split horizon: the sender reached that peer itself */
		return;
	}
	if (n->standby) {
		n->drop_standby++;
		return;
	}
	n->local_tx++;
	port_write(n, f, len);
}
