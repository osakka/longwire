/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The phone's adapter (docs/adr/0076): a MAC, DHCP, ARP, and IPv4 wrapped and unwrapped, driven by a script for the far LAN. Nothing is on the wire; what the adapter sends is caught here.
 */
#include "../engine/all.c"

static int bad;
static uint8_t sent[32][1600], up[32][1600];
static size_t sentlen[32], uplen[32];
static int nsent, nup;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

static void tx(void *ctx, const uint8_t *f, size_t len)
{
	(void)ctx;
	if (nsent < 32 && len <= 1600) {
		memcpy(sent[nsent], f, len);
		sentlen[nsent++] = len;
	}
}

static void toos(void *ctx, const uint8_t *p, size_t len)
{
	(void)ctx;
	if (nup < 32 && len <= 1600) {
		memcpy(up[nup], p, len);
		uplen[nup++] = len;
	}
}

#define GW 0xc0a80101u			/* 192.168.1.1 */
#define ME 0xc0a80132u			/* 192.168.1.50 */
#define PEER 0xc0a80114u		/* 192.168.1.20 */
static const uint8_t GWMAC[6] = { 0x52, 0x54, 0, 0x12, 0x34, 0x56 }, PEERMAC[6] = { 0x52, 0x54, 0, 0xaa, 0xbb, 0xcc };

/* the option bytes of a DHCP message we sent, or 0 */
static const uint8_t *opt(const uint8_t *f, size_t len, int code)
{
	const uint8_t *o = f + 14 + 20 + 8 + 236 + 4, *end = f + len;

	while (o < end && *o != 255) {
		if (*o == 0) {
			o++;
			continue;
		}
		if (o[0] == code)
			return o + 2;
		o += 2 + o[1];
	}
	return 0;
}

/* a reply from the LAN's DHCP server to the adapter's last message */
static size_t reply(uint8_t *f, const lw_leaf *m, int type, uint32_t yi, uint32_t mask, uint32_t gw, uint32_t lease, int with_dns)
{
	uint8_t *ip = f + 14, *udp = f + 34, *b = f + 42, *o;
	size_t len;

	memset(f, 0, 600);
	memcpy(f, m->mac, 6);
	memcpy(f + 6, GWMAC, 6);
	put16(f + 12, 0x0800);
	ip[0] = 0x45;
	ip[8] = 64;
	ip[9] = 17;
	put32(ip + 12, GW);
	put32(ip + 16, 0xffffffffu);
	put16(udp, 67);
	put16(udp + 2, 68);
	b[0] = 2;
	b[1] = 1;
	b[2] = 6;
	put32(b + 4, m->xid);
	put32(b + 16, yi);
	memcpy(b + 28, m->mac, 6);
	put32(b + 236, 0x63825363u);
	o = b + 240;
	*o++ = 53; *o++ = 1; *o++ = (uint8_t)type;
	*o++ = 54; *o++ = 4; put32(o, GW); o += 4;
	*o++ = 1; *o++ = 4; put32(o, mask); o += 4;
	*o++ = 3; *o++ = 4; put32(o, gw); o += 4;
	if (with_dns) {
		*o++ = 6; *o++ = 8; put32(o, GW); put32(o + 4, 0x01010101u); o += 8;
		*o++ = 15; *o++ = 8; memcpy(o, "home.lan", 8); o += 8;
		*o++ = 26; *o++ = 2; put16(o, 1500); o += 2;
	}
	*o++ = 51; *o++ = 4; put32(o, lease); o += 4;
	*o++ = 255;
	len = (size_t)(o - f);
	put16(ip + 2, (unsigned)(len - 14));
	put16(udp + 4, (unsigned)(len - 34));
	return len;
}

/* an IPv4 packet of 40 bytes, src to dst, UDP */
static size_t ippkt(uint8_t *p, uint32_t src, uint32_t dst)
{
	memset(p, 0, 40);
	p[0] = 0x45;
	put16(p + 2, 40);
	p[8] = 64;
	p[9] = 17;
	put32(p + 12, src);
	put32(p + 16, dst);
	put16(p + 20, 5000);
	put16(p + 22, 5001);
	put16(p + 24, 20);
	return 40;
}

static size_t arpf(uint8_t *f, const uint8_t *dmac, const uint8_t *smac, int op, uint32_t sip, const uint8_t *tmac, uint32_t tip)
{
	memset(f, 0, 60);
	memcpy(f, dmac, 6);
	memcpy(f + 6, smac, 6);
	put16(f + 12, 0x0806);
	put16(f + 14, 1);
	put16(f + 16, 0x0800);
	f[18] = 6;
	f[19] = 4;
	put16(f + 20, (unsigned)op);
	memcpy(f + 22, smac, 6);
	put32(f + 28, sip);
	memcpy(f + 32, tmac, 6);
	put32(f + 38, tip);
	return 60;
}

int main(void)
{
	lw_leaf m;
	uint8_t key[32], f[1600], p[1500], zero[6] = { 0 }, bc[6] = { 255, 255, 255, 255, 255, 255 };
	size_t n;
	double t = 100.0;
	unsigned gen;
	int i;

	memset(key, 7, 32);
	leaf_init(&m, key, t);
	check(m.mac[0] == 0x02 && !leaf_bound(&m), "a MAC of its own, locally administered, derived from the key; not bound");
	{
		lw_leaf m2;
		uint8_t k2[32];

		memset(k2, 8, 32);
		leaf_init(&m2, k2, t);
		check(memcmp(m.mac, m2.mac, 6) != 0, "another key, another MAC");
		leaf_init(&m2, key, t + 50);
		check(!memcmp(m.mac, m2.mac, 6), "the same key, the same MAC, whenever");
	}

	/* DISCOVER */
	nsent = 0;
	leaf_tick(&m, t, tx, 0);
	check(nsent == 1 && !memcmp(sent[0], bc, 6) && !memcmp(sent[0] + 6, m.mac, 6) && get16(sent[0] + 34) == 68 && get16(sent[0] + 36) == 67, "it asks the LAN for an address: a DHCP DISCOVER to the broadcast, 68 to 67");
	check(sentlen[0] >= 300 && opt(sent[0], sentlen[0], 53) && *opt(sent[0], sentlen[0], 53) == 1 && !memcmp(sent[0] + 42 + 28, m.mac, 6), "...a full-size BOOTP message, type 1, with its own hardware address");
	{
		unsigned sum = lw_sum16(sent[0] + 14, 20, 0);

		while (sum >> 16)
			sum = (sum & 0xffff) + (sum >> 16);
		check(sum == 0xffff, "...and a valid IP header checksum");
	}
	nsent = 0;
	leaf_tick(&m, t + 1, tx, 0);
	check(nsent == 0, "it does not ask again at once");
	leaf_tick(&m, t + 4.1, tx, 0);
	check(nsent == 1, "...it asks again after four seconds");

	/* hostile offers are refused: a mask that is not one, loopback, a gateway off the link, the wrong xid */
	nsent = 0;
	n = reply(f, &m, D_OFFER, ME, 0xffff00ffu, GW, 3600, 1);
	leaf_from_line(&m, t + 5, f, n, tx, 0, toos, 0);
	n = reply(f, &m, D_OFFER, 0x7f000001u, 0xffffff00u, GW, 3600, 1);
	leaf_from_line(&m, t + 5, f, n, tx, 0, toos, 0);
	n = reply(f, &m, D_OFFER, ME, 0xffffff00u, 0x0a000001u, 3600, 1);
	leaf_from_line(&m, t + 5, f, n, tx, 0, toos, 0);
	n = reply(f, &m, D_OFFER, ME, 0, GW, 3600, 1);
	leaf_from_line(&m, t + 5, f, n, tx, 0, toos, 0);
	n = reply(f, &m, D_OFFER, 0xa9fe0105u, 0xffff0000u, 0, 3600, 1);
	leaf_from_line(&m, t + 5, f, n, tx, 0, toos, 0);
	m.xid ^= 1;
	n = reply(f, &m, D_OFFER, ME, 0xffffff00u, GW, 3600, 1);
	m.xid ^= 1;
	leaf_from_line(&m, t + 5, f, n, tx, 0, toos, 0);
	check(nsent == 0 && m.state == MB_SELECT, "an offer with a mask that is not one, a loopback or link-local address, a gateway off the link, or the wrong transaction is not taken");

	/* a good offer, then the request, then the ack */
	n = reply(f, &m, D_OFFER, ME, 0xffffff00u, GW, 3600, 1);
	leaf_from_line(&m, t + 6, f, n, tx, 0, toos, 0);
	check(nsent == 1 && opt(sent[0], sentlen[0], 53) && *opt(sent[0], sentlen[0], 53) == 3 && get32(opt(sent[0], sentlen[0], 50)) == ME && get32(opt(sent[0], sentlen[0], 54)) == GW,
	      "a good offer is answered with a REQUEST for that address from that server");
	gen = m.gen;
	n = reply(f, &m, D_ACK, ME, 0xffffff00u, GW, 3600, 1);
	leaf_from_line(&m, t + 6.1, f, n, tx, 0, toos, 0);
	check(leaf_bound(&m) && m.ip == ME && m.mask == 0xffffff00u && m.gw == GW && m.gen == gen + 1, "the ACK binds it: the address, the mask and the gateway are the LAN's, and the generation moved");
	check(m.ndns == 2 && m.dns[0] == GW && m.dns[1] == 0x01010101u && !strcmp(m.domain, "home.lan") && m.mtu == 1500 && m.lease == 3600, "...with its resolvers, search domain, MTU and lease");

	/* ARP: asked for our address, we answer, and learn who asked */
	nsent = 0;
	n = arpf(f, bc, GWMAC, 1, GW, zero, ME);
	leaf_from_line(&m, t + 7, f, n, tx, 0, toos, 0);
	check(nsent == 1 && get16(sent[0] + 12) == 0x0806 && get16(sent[0] + 20) == 2 && !memcmp(sent[0], GWMAC, 6) && !memcmp(sent[0] + 22, m.mac, 6) && get32(sent[0] + 28) == ME, "asked who has its address, it answers with its MAC");
	nsent = 0;
	n = arpf(f, bc, PEERMAC, 1, PEER, zero, 0xc0a80199u);
	leaf_from_line(&m, t + 7, f, n, tx, 0, toos, 0);
	check(nsent == 0, "asked about someone else's address, it says nothing");

	/* from the OS: off-link goes to the gateway (whose MAC it learned), on-link asks first */
	nsent = 0;
	n = ippkt(p, ME, 0x08080808u);
	leaf_from_os(&m, t + 8, p, n, tx, 0);
	check(nsent == 1 && !memcmp(sent[0], GWMAC, 6) && !memcmp(sent[0] + 6, m.mac, 6) && get16(sent[0] + 12) == 0x0800 && sentlen[0] == 14 + 40 && !memcmp(sent[0] + 14, p, 40),
	      "a packet for the internet is wrapped toward the gateway's MAC, unchanged");
	nsent = 0;
	n = ippkt(p, ME, PEER);
	leaf_from_os(&m, t + 8, p, n, tx, 0);
	check(nsent == 1 && get16(sent[0] + 12) == 0x0806 && get16(sent[0] + 20) == 1 && get32(sent[0] + 38) == PEER && !memcmp(sent[0], bc, 6), "a packet for a host on the LAN whose MAC it does not know makes it ask (ARP), and waits");
	nsent = 0;
	n = arpf(f, m.mac, PEERMAC, 2, PEER, m.mac, ME);
	leaf_from_line(&m, t + 8.2, f, n, tx, 0, toos, 0);
	check(nsent == 1 && !memcmp(sent[0], PEERMAC, 6) && get16(sent[0] + 12) == 0x0800 && sentlen[0] == 54, "the answer releases the packet that waited");
	nsent = 0;
	n = ippkt(p, ME, PEER);
	leaf_from_os(&m, t + 9, p, n, tx, 0);
	check(nsent == 1 && !memcmp(sent[0], PEERMAC, 6), "...and the next goes straight there");
	nsent = 0;
	n = ippkt(p, ME, 0xc0a801ffu);
	leaf_from_os(&m, t + 9, p, n, tx, 0);
	check(nsent == 1 && !memcmp(sent[0], bc, 6), "the LAN's broadcast address goes to the broadcast MAC");
	nsent = 0;
	n = ippkt(p, ME, 0xe00000fbu);
	leaf_from_os(&m, t + 9, p, n, tx, 0);
	check(nsent == 1 && sent[0][0] == 1 && sent[0][1] == 0 && sent[0][2] == 0x5e && sent[0][5] == 0xfb, "a multicast address goes to its multicast MAC");

	/* what must never leave */
	nsent = 0;
	n = ippkt(p, PEER, 0x08080808u);
	leaf_from_os(&m, t + 9, p, n, tx, 0);
	n = ippkt(p, ME, 0x7f000001u);
	leaf_from_os(&m, t + 9, p, n, tx, 0);
	n = ippkt(p, ME, ME);
	leaf_from_os(&m, t + 9, p, n, tx, 0);
	p[0] = 0x60;
	leaf_from_os(&m, t + 9, p, 40, tx, 0);
	n = ippkt(p, ME, 0x08080808u);
	put16(p + 2, 99);
	leaf_from_os(&m, t + 9, p, n, tx, 0);
	check(nsent == 0 && m.drop_v6 == 1 && m.drop_os >= 4, "a source that is not ours, loopback, ourselves, IPv6 (counted) and a length that lies never leave");

	/* from the line to the OS */
	nup = 0;
	n = ippkt(p, 0x08080808u, ME);
	memcpy(f, m.mac, 6);
	memcpy(f + 6, GWMAC, 6);
	put16(f + 12, 0x0800);
	memcpy(f + 14, p, 40);
	leaf_from_line(&m, t + 10, f, 14 + 40, tx, 0, toos, 0);
	check(nup == 1 && uplen[0] == 40 && !memcmp(up[0], p, 40), "an IPv4 packet for our MAC and address is handed to the OS, unwrapped, exactly");
	nup = 0;
	memset(f + 14 + 12 + 4, 0, 0);
	put32(f + 14 + 16, PEER);
	leaf_from_line(&m, t + 10, f, 14 + 40, tx, 0, toos, 0);
	put32(f + 14 + 16, ME);
	memcpy(f, bc, 6);
	leaf_from_line(&m, t + 10, f, 14 + 40, tx, 0, toos, 0);
	memcpy(f, m.mac, 6);
	put32(f + 14 + 12, 0x7f000001u);
	leaf_from_line(&m, t + 10, f, 14 + 40, tx, 0, toos, 0);
	put32(f + 14 + 12, 0x08080808u);
	put16(f + 14 + 2, 900);
	leaf_from_line(&m, t + 10, f, 14 + 40, tx, 0, toos, 0);
	f[12] = 0x86;
	f[13] = 0xdd;
	leaf_from_line(&m, t + 10, f, 14 + 40, tx, 0, toos, 0);
	check(nup == 0, "a packet for another address, a broadcast, a loopback source, a length that lies, IPv6: nothing reaches the OS");

	/* renewal at T1 and loss of the lease */
	nsent = 0;
	leaf_tick(&m, t + 6.1 + 1801, tx, 0);
	check(nsent == 1 && m.state == MB_RENEW && opt(sent[0], sentlen[0], 53) && *opt(sent[0], sentlen[0], 53) == 3 && get32(sent[0] + 42 + 12) == ME, "at half the lease it renews, as the address it holds");
	gen = m.gen;
	n = reply(f, &m, D_ACK, ME, 0xffffff00u, GW, 7200, 1);
	leaf_from_line(&m, t + 6.1 + 1802, f, n, tx, 0, toos, 0);
	check(m.state == MB_BOUND && m.lease == 7200 && m.gen == gen, "the renewal is taken, and the same address does not move the generation");
	n = reply(f, &m, D_ACK, 0xc0a80140u, 0xffffff00u, GW, 7200, 1);
	m.state = MB_RENEW;
	leaf_from_line(&m, t + 6.1 + 1803, f, n, tx, 0, toos, 0);
	check(m.ip == 0xc0a80140u && m.gen == gen + 1, "a renewal that gives a different address moves it (the app builds its tunnel again)");
	nsent = 0;
	n = reply(f, &m, D_NAK, 0, 0, 0, 0, 0);
	leaf_from_line(&m, t + 2000, f, n, tx, 0, toos, 0);
	check(!leaf_bound(&m) && m.state == MB_INIT && m.gen == gen + 2, "a NAK takes the address away, and the generation says so");
	leaf_tick(&m, t + 2002, tx, 0);
	check(m.state == MB_SELECT && nsent == 1, "...and it asks again");
	leaf_init(&m, key, 0);
	nsent = 0;
	leaf_tick(&m, 0, tx, 0);
	n = reply(f, &m, D_OFFER, ME, 0xffffff00u, GW, 100, 0);
	leaf_from_line(&m, 1, f, n, tx, 0, toos, 0);
	n = reply(f, &m, D_ACK, ME, 0xffffff00u, GW, 100, 0);
	leaf_from_line(&m, 1, f, n, tx, 0, toos, 0);
	leaf_tick(&m, 51, tx, 0);
	leaf_tick(&m, 110, tx, 0);
	check(!leaf_bound(&m), "a lease that runs out unrenewed takes the address away: it is not ours any more");

	/* what it is given on the line is hostile: a thousand frames of noise, and of nearly right ones, and it stands */
	leaf_init(&m, key, 0);
	leaf_tick(&m, 0, tx, 0);
	n = reply(f, &m, D_OFFER, ME, 0xffffff00u, GW, 3600, 1);
	leaf_from_line(&m, 1, f, n, tx, 0, toos, 0);
	n = reply(f, &m, D_ACK, ME, 0xffffff00u, GW, 3600, 1);
	leaf_from_line(&m, 1, f, n, tx, 0, toos, 0);
	for (i = 0; i < 4000; i++) {
		uint8_t g[700];
		size_t l = (size_t)(lw_now() * 1000) % 700, k;

		lw_rand(g, sizeof g);
		l = (size_t)g[0] * 2 % 700 + (i & 3 ? 0 : 14);
		if (i & 1) {			/* a nearly right DHCP reply with its options mangled */
			size_t rl = reply(g, &m, D_ACK, ME, 0xffffff00u, GW, 3600, 1);
			size_t pos = 14 + 20 + 8 + 240 + (size_t)(i / 2) % 40;

			g[pos] = (uint8_t)(i * 7);
			l = rl - (size_t)(i % 5);
		}
		if (i % 3 == 0) {
			memcpy(g, m.mac, 6);
			g[12] = 0x08;
			g[13] = (i & 4) ? 0x00 : 0x06;
		}
		for (k = 0; k < 2; k++)
			leaf_from_line(&m, 2 + i * 0.01, g, l, tx, 0, toos, 0);
		leaf_from_os(&m, 2 + i * 0.01, g + 14, l > 14 ? l - 14 : 0, tx, 0);
		nsent = nup = 0;
	}
	check(1, "four thousand frames of noise and of mangled replies from the line and the OS: it stands (a crash would not get here)");

	printf("%s\n", bad ? "FAILED" : "all leaf checks passed");
	return bad;
}
