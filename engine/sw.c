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
		if (!sess_send(n, p, K_FRAME, f, len))
			p->tx_frames++;
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
void sw_fragment(lw_node *n, int pi, const uint8_t *b, size_t len)
{
	lw_peer *p = &n->peers[pi];
	unsigned id, part, total, h;
	int i, slot = -1;

	if (len < 6 || len > 5 + FRAME_MAX)
		return;
	id = get16(b);
	part = b[2];
	total = get16(b + 3);
	h = (total + 1) / 2;
	if (part > 1 || total <= FRAME_SINGLE || total > FRAME_MAX || len - 5 != (part ? total - h : h))
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
		p->fr->s[slot].id = (uint16_t)id;
		p->fr->s[slot].total = (int)total;
		p->fr->s[slot].t = n->now;
	}
	if (p->fr->s[slot].total != (int)total)
		return;
	memcpy(p->fr->s[slot].buf + (part ? h : 0), b + 5, len - 5);
	p->fr->s[slot].got |= 1 << part;
	if (p->fr->s[slot].got == 3) {
		p->fr->s[slot].used = 0;
		n->frames_joined++;
		p->rx_frames++;
		p->rx_bytes += total;
		sw_from_peer(n, pi, p->fr->s[slot].buf, total);
	}
}

/* ---- the switch ------------------------------------------------------------------ */

void sw_from_local(lw_node *n, const uint8_t *in, size_t len)
{
	uint8_t f[FRAME_MAX];
	mac_ent *e;
	int i;

	if (len < 14 || len > FRAME_MAX) {
		n->drop_garbage++;
		return;
	}
	if (n->splice && site_input(n, in, len))	/* another box on this segment announcing itself */
		return;
	n->local_rx++;
	if (n->standby) {			/* a second box on the segment is the one forwarding */
		n->drop_standby++;
		return;
	}
	memcpy(f, in, len);
	mss_clamp(f, len);
	learn(n, f + 6, -1);
	arp_learn(n, f, len);
	if (arp_suppress(n, f, len))
		return;
	if (!(f[0] & 1) && (e = mac_find(n, f, 0))) {
		if (e->port == -1)
			return;			/* local to local: never leaves the site */
		if (n->peers[e->port].used && n->peers[e->port].cur.valid) {
			to_peer(n, e->port, f, len);
			return;
		}			/* its peer is gone: flood, it may have moved */
	}
	n->flood++;
	for (i = 0; i < n->maxpeers; i++)
		if (n->peers[i].used && !n->peers[i].is_sb && n->peers[i].cur.valid && flood_ok(n, &n->peers[i]))
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
	arp_learn(n, f, len);
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
