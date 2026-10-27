/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A fuzzer for everything a stranger can send, built by TCC with its own bounds checker (tests/fuzz.sh):
 *
 *     tcc -b -g ...        every pointer access in the engine is checked against the object it points into.
 *
 * It includes the engine's single translation unit, makes a switchboard and a node in this process, and feeds their
 * receive paths from the position of an attacker:
 *
 *   raw      datagrams of every type, any length, from many source addresses (what anyone on the internet can send)
 *   hs       a real handshake captured off the wire, then mutated: flipped bytes, cuts, repeats, other fragments
 *   session  a stranger who has completed a handshake (the position of any anonymous node): correctly encrypted
 *            packets carrying control messages (REGISTER, DIAL, ...), split frames and Ethernet frames
 *   cmd      lines on the control socket
 *
 * A bounds error stops the program with TCC's message, and the case being run is printed on the way out. The run is
 * deterministic for a given seed, so a failure repeats: fuzz SEED STAGE ITERATIONS.
 */
#include "../engine/all.c"
#include <signal.h>
#include <sys/stat.h>

static uint64_t rs = 88172645463325252ULL;

static uint32_t rnd(void)
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return (uint32_t)(rs >> 16);
}

static uint32_t rn(uint32_t m) { return m ? rnd() % m : 0; }

static uint8_t rb(void)		/* bytes that sit on the edges of counts and lengths, as often as noise */
{
	switch (rn(8)) {
	case 0: return 0;
	case 1: return 0xff;
	case 2: return 0x80;
	case 3: return 1;
	case 4: return (uint8_t)rn(20);
	case 5: return 0x7f;
	default: return (uint8_t)rnd();
	}
}

static void rfill(uint8_t *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		b[i] = rb();
}

static const char *g_stage = "";
static unsigned long g_iter;
static uint8_t g_last[2200];
static size_t g_lastn;
static int g_lastk;

static void remember(int kind, const uint8_t *b, size_t n)
{
	g_lastk = kind;
	g_lastn = n < sizeof g_last ? n : sizeof g_last;
	memcpy(g_last, b, g_lastn);
}

static void report(void)
{
	size_t i;

	if (!g_stage[0])
		return;
	fprintf(stderr, "fuzz: stopped in stage %s at iteration %lu; last input (kind %d, %zu bytes):\n", g_stage, g_iter, g_lastk, g_lastn);
	for (i = 0; i < g_lastn; i++)
		fprintf(stderr, "%02x", g_last[i]);
	fprintf(stderr, "\n");
}

static void on_sig(int s)
{
	(void)s;
	report();
	_exit(3);
}

/* ---- the nodes ------------------------------------------------------------------ */

static lw_node *mknode(int role, const char *dir, int policy, const char *sb)
{
	lw_cfg c;
	char err[200];
	lw_node *n;

	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", dir);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", dir);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", dir);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.role = role;
	c.maxpeers = 48;
	c.no_nat_pmp = 1;
	c.policy_script = policy;
	if (role)
		snprintf(c.registry, sizeof c.registry, "none");
	if (sb)
		snprintf(c.switchboard, sizeof c.switchboard, "%s", sb);
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "fuzz: cannot open a node: %s\n", err);
		exit(2);
	}
	return n;
}

typedef struct vp {		/* a stranger with a session: the harness holds both ends of its keys */
	lw_peer *p;
	uint32_t idx;
	uint64_t ctr;
	uint8_t  key[32];
} vp;

static void sess_make(lw_node *n, vp *v, int i)
{
	(void)i;
	lw_sess *s = &v->p->cur;
	struct sockaddr_in *a = (struct sockaddr_in *)&v->p->ep;

	lw_rand(v->key, 32);
	memset(s, 0, sizeof *s);
	memcpy(s->k_recv, v->key, 32);
	s->valid = s->confirmed = 1;
	s->rx_idx = v->idx = sess_new_idx(n, v->p);
	s->tx_idx = 0x5000 + (uint32_t)i;
	s->born = n->now;
	v->ctr = 0;
	memset(a, 0, sizeof *a);
	a->sin_family = AF_INET;
	a->sin_addr.s_addr = htonl(0x7f000001);
	a->sin_port = htons((uint16_t)(9 + i));
	v->p->eplen = sizeof *a;
	v->p->last_rx = n->now;
	v->p->last_tx = n->now;
}

static void mkpeers(lw_node *n, vp *v, int k, int first)
{
	int i;

	for (i = 0; i < k; i++) {
		lw_id id;

		memset(&id, 0, sizeof id);
		lw_rand(id.seed, 32);
		lw_id_derive(&id);
		if (!(v[i].p = peer_add(n, id.bundle))) {
			fprintf(stderr, "fuzz: peer table full\n");
			exit(2);
		}
		sess_make(n, &v[i], first + i);
	}
}

/* one correctly encrypted packet from a stranger's session */
static void inject(lw_node *n, vp *v, uint8_t kind, const uint8_t *body, size_t len)
{
	uint8_t buf[12 + 1 + FRAME_MAX + 16], nonce[12];
	crypto_aead_ctx c;

	if (len > FRAME_MAX)
		len = FRAME_MAX;
	if (!v->p->cur.valid)
		sess_make(n, v, 0);
	remember(100 + kind, body, len);
	buf[0] = T_DATA;
	buf[1] = (uint8_t)v->idx;
	buf[2] = (uint8_t)(v->idx >> 8);
	buf[3] = (uint8_t)(v->idx >> 16);
	put64le(buf + 4, v->ctr);
	buf[12] = kind;
	if (len)
		memcpy(buf + 13, body, len);
	nonce_of(nonce, v->ctr);
	crypto_aead_init_ietf(&c, v->key, nonce);
	crypto_aead_write(&c, buf + 12, buf + 12 + 1 + len, buf, 12, buf + 12, 1 + len);
	v->ctr++;
	node_dispatch(n, buf, 12 + 1 + len + 16, (struct sockaddr *)&v->p->ep, v->p->eplen);
}

static void ctl(lw_node *n, vp *v, uint8_t msg, const uint8_t *pl, size_t len, int lie)
{
	uint8_t b[3 + 2048];

	if (len > 2000)
		len = 2000;
	b[0] = msg;
	put16(b + 1, lie ? rnd() & 0xffff : (unsigned)len);
	memcpy(b + 3, pl, len);
	inject(n, v, K_CTL, b, 3 + len);
}

/* ---- generators ----------------------------------------------------------------- */

static size_t gen_cands(uint8_t *o)
{
	size_t k = 0;
	unsigned c = rn(12), i;

	o[k++] = (uint8_t)(c | (rn(2) ? CANDS_STRICT : 0));
	for (i = 0; i < c && k < 200; i++) {
		if (rn(4)) {
			lw_cand cd;

			memset(&cd, 0, sizeof cd);
			cd.fam = rn(3) ? 4 : 6;
			rfill(cd.ip, 16);
			cd.port = (uint16_t)rnd();
			cd.type = (uint8_t)(1 + rn(4));
			k += (size_t)cand_put(o + k, &cd);
		} else {
			unsigned l = rn(24);

			o[k++] = (uint8_t)l;
			rfill(o + k, l);
			k += l;
		}
	}
	return k;
}

static size_t gen_tags(uint8_t *o)
{
	size_t k = 0;
	unsigned t = rn(4), i;

	for (i = 0; i < t && k < 150; i++) {
		static const uint8_t tags[] = { TAG_ADMIT, TAG_NAME, TAG_LISTED, TAG_SWITCHBOARD, TAG_CODE };
		unsigned l = rn(3) == 0 ? rn(200) : rn(3) == 0 ? ADMIT_LEN : rn(40);

		o[k++] = rn(5) ? tags[rn(5)] : rb();
		o[k++] = (uint8_t)l;
		if (rn(6) == 0)
			l = rn(10);			/* a length that lies about what follows */
		rfill(o + k, l);
		k += l;
	}
	return k;
}

static void gen_number(char *num, lw_node *n)
{
	struct board *b = n->bd;
	unsigned i;

	if (b && b->n && rn(3)) {
		memcpy(num, b->regs[rn((uint32_t)b->n)].number, NUMLEN);
		return;
	}
	for (i = 0; i < NUMLEN; i++)
		num[i] = (char)(rn(5) ? '0' + rn(10) : rb());
}

static size_t gen_board_payload(lw_node *n, uint8_t msg, uint8_t *o)
{
	size_t k = 0;
	struct board *b = n->bd;
	unsigned i;

	switch (msg) {
	case M_REGISTER: {
		unsigned tl = rn(12) == 0 ? rb() : rn(70);

		o[k++] = (uint8_t)tl;
		for (i = 0; i < tl; i++)
			o[k++] = rn(2) ? (uint8_t)"\"\\{}\n,:"[rn(7)] : rb();	/* the characters that matter inside JSON */
		if (rn(3)) {
			unsigned nl = rn(40);

			o[k++] = TAG_NAME;
			o[k++] = (uint8_t)nl;
			for (i = 0; i < nl; i++)
				o[k++] = rn(8) ? (uint8_t)('a' + rn(26)) : rb();
		}
		if (rn(3)) {
			o[k++] = TAG_LISTED;
			o[k++] = 0;
		}
		return k + gen_tags(o + k);
	}
	case M_HEARTBEAT:
		return gen_cands(o);
	case M_DIAL:
		gen_number((char *)o, n);
		k = NUMLEN;
		k += gen_cands(o + k);
		return k + gen_tags(o + k);
	case M_ANSWER: {
		int used[32], nu = 0;

		for (i = 0; b && i < 32; i++)
			if (b->calls[i].used)
				used[nu++] = (int)i;
		if (nu && rn(4))
			memcpy(o, b->calls[used[rn((uint32_t)nu)]].id, 8);
		else
			rfill(o, 8);
		o[8] = rn(4) ? (uint8_t)rn(3) : rb();
		return 9 + gen_cands(o + 9);
	}
	case M_LOOKUP: {
		unsigned ql = rn(8) == 0 ? rn(70) : rn(2) ? 32 : 3 + rn(30);

		o[0] = (uint8_t)rn(3);
		rfill(o + 1, ql);
		return 1 + ql;
	}
	default:
		k = rn(5) ? rn(60) : rn(400);
		rfill(o, k);
		return k;
	}
}

static size_t gen_node_payload(lw_node *n, uint8_t msg, uint8_t *o)
{
	size_t k;

	(void)n;
	switch (msg) {
	case M_NUMBER:
		k = rn(4) ? NUMLEN + 6 + rn(8) : rn(30);
		rfill(o, k);
		memcpy(o, "123456782", 9);
		return k;
	case M_RING:
		k = 8 + NUMLEN + LW_BUNDLE;
		rfill(o, k);
		o[0] = 1;
		memcpy(o + 8, "123456782", NUMLEN);
		o[8 + NUMLEN] = 1;
		if (rn(4))
			k = rn(k + 40);
		return k + (rn(2) ? gen_cands(o + k) : 0);
	case M_GO:
		k = 11 + NUMLEN + 32 + LW_BUNDLE;
		rfill(o, k);
		o[8] = (uint8_t)rn(2);
		if (rn(4))
			k = rn(k + 40);
		return k + (rn(2) ? gen_cands(o + k) : 0);
	case M_FOUND: {
		unsigned ql = rn(40), nres = rn(10), i;

		k = 0;
		o[k++] = (uint8_t)rn(3);
		o[k++] = (uint8_t)rn(3);
		o[k++] = (uint8_t)ql;
		rfill(o + k, ql);
		k += ql;
		o[k++] = (uint8_t)nres;
		for (i = 0; i < nres && k < 700; i++) {
			unsigned nl = rn(40);

			rfill(o + k, NUMLEN + 32);
			k += NUMLEN + 32;
			o[k++] = (uint8_t)nl;
			rfill(o + k, nl);
			k += nl;
		}
		return rn(5) ? k : rn(k + 1);
	}
	case M_LINE: case M_MEMBER: case M_EVICT: case M_LINEKEY: case M_LINEHELLO: case M_RELAY_REQ: case M_RELAY_OK: case M_RELAY_NO: case M_BYE:
		k = rn(3) ? (msg == M_LINE ? 8 + 32 + ADMIT_LEN + 1 + rn(5) * MEMBER_LEN : msg == M_RELAY_OK ? 36 + LW_BUNDLE : rn(200)) : rn(1500);
		if (msg == M_LINE && rn(2) == 0)
			k = 8 + 32 + ADMIT_LEN + 1 + (rn(40)) * MEMBER_LEN;	/* a member count far past 16 */
		if (k > 1900)
			k = 1900;
		rfill(o, k);
		return k;
	default:
		k = rn(100);
		rfill(o, k);
		return k;
	}
}

static const uint8_t board_msgs[] = { M_REGISTER, M_REGISTER, M_HEARTBEAT, M_DIAL, M_DIAL, M_ANSWER, M_LOOKUP, M_LOOKUP,
				      M_NUMBER, M_RING, M_GO, M_FOUND, M_REFUSED, M_LINE, M_BYE };
static const uint8_t node_msgs[] = { M_NUMBER, M_NUMBER, M_HBACK, M_RING, M_RING, M_GO, M_GO, M_REJECTED, M_FOUND, M_FOUND,
				     M_REFUSED, M_LINEHELLO, M_LINE, M_LINE, M_MEMBER, M_EVICT, M_LINEKEY, M_RELAY_REQ, M_RELAY_OK,
				     M_RELAY_NO, M_BYE };

static void gen_frame(uint8_t *f, size_t *len)
{
	static const uint8_t macs[4][6] = { { 2, 0, 0, 0, 0, 1 }, { 2, 0, 0, 0, 0, 2 }, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, { 0x33, 0x33, 0, 0, 0, 1 } };
	size_t n = rn(6) == 0 ? rn(70) : 14 + rn(rn(3) ? 120 : 1500);
	unsigned kind = rn(7);

	if (n > FRAME_MAX)
		n = FRAME_MAX;
	rfill(f, n);
	if (n >= 14) {
		memcpy(f, macs[rn(4)], 6);
		memcpy(f + 6, macs[rn(2)], 6);
		switch (kind) {
		case 0: f[12] = 0x08; f[13] = 0x06; if (n >= 22) { f[14] = 0; f[15] = 1; f[16] = 8; f[17] = 0; f[18] = 6; f[19] = 4; f[20] = 0; f[21] = (uint8_t)(1 + rn(2)); } break;
		case 1: f[12] = 0x08; f[13] = 0x00; if (n >= 15) f[14] = 0x45 + (uint8_t)rn(3); if (n >= 24) f[23] = 17; break;
		case 2: f[12] = 0x86; f[13] = 0xdd; if (n >= 55) { f[20] = 58; f[54] = (uint8_t)(rn(2) ? 135 : 136); } break;
		case 3: f[12] = 0x81; f[13] = 0x00; break;
		case 4: f[12] = 0x08; f[13] = 0x00; if (n >= 50) { f[14] = 0x45; f[23] = 17; f[36] = 0; f[37] = (uint8_t)(rn(2) ? 67 : 68); } break;
		default: break;
		}
	}
	*len = n;
}

/* ---- stages --------------------------------------------------------------------- */

static void rand_src(struct sockaddr_storage *ss, socklen_t *sl)
{
	memset(ss, 0, sizeof *ss);
	if (rn(5)) {
		struct sockaddr_in *a = (struct sockaddr_in *)ss;

		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0x7f000100 + rn(64));
		a->sin_port = htons((uint16_t)(1024 + rn(40)));
		*sl = sizeof *a;
	} else {
		struct sockaddr_in6 *a = (struct sockaddr_in6 *)ss;

		a->sin6_family = AF_INET6;
		rfill((uint8_t *)&a->sin6_addr, 16);
		a->sin6_port = htons((uint16_t)(1024 + rn(40)));
		*sl = sizeof *a;
	}
}

static void tick(lw_node *n)
{
	n->now += (double)rn(30) / 10.0;
	if (n->role)
		board_timers(n);
	else
		node_timers(n);
	sess_timers(n);
	site_timers(n);
}

static void stage_raw(lw_node *sb, lw_node *nd, unsigned long iters)
{
	static const uint8_t types[] = { T_DATA, 0, T_COOKIE, T_PUNCH, T_ECHO, T_HELLO, T_HS_INIT, T_HS_RESP, T_RELAY, T_HS_INIT, T_HS_RESP, 0x20, 0xff };
	uint8_t pkt[2048];
	unsigned long i;

	g_stage = "raw";
	for (i = 0; i < iters; i++) {
		struct sockaddr_storage ss;
		socklen_t sl;
		size_t len;
		lw_node *n = rn(2) ? sb : nd;

		g_iter = i;
		len = rn(3) == 0 ? 1 + rn(40) : rn(4) == 0 ? 1 + rn(2047) : 1 + rn(120);
		if (rn(6) == 0) {					/* the exact sizes the protocol uses */
			static const size_t sz[] = { HS_CHUNK + 20, HS_CHUNK + 21, 40, 41, 29, 30, 12 + 1 + 16, 1 + 8 + 6, 7, 8, 12 };

			len = sz[rn(sizeof sz / sizeof *sz)];
		}
		rfill(pkt, len);
		pkt[0] = types[rn(sizeof types)];
		if (pkt[0] == T_RELAY && len > 4 && rn(2))
			pkt[1 + rn(3)] = types[rn(sizeof types)];	/* a relay packet wrapped around another packet */
		rand_src(&ss, &sl);
		remember(1, pkt, len);
		node_dispatch(n, pkt, len, (struct sockaddr *)&ss, sl);
		if (i % 400 == 0)
			tick(n);
	}
}

static void stage_session(lw_node *sb, vp *sv, int nsv, lw_node *nd, vp *nv, int nnv, unsigned long iters)
{
	uint8_t o[2100], f[FRAME_MAX + 8];
	unsigned long i;

	g_stage = "session";
	for (i = 0; i < iters; i++) {
		size_t k, len;
		unsigned which = rn(10);

		g_iter = i;
		if (which < 4) {					/* a stranger talking to the switchboard */
			vp *v = &sv[rn((uint32_t)nsv)];
			uint8_t m = rn(8) ? board_msgs[rn(sizeof board_msgs)] : rb();

			k = gen_board_payload(sb, m, o);
			ctl(sb, v, m, o, k, rn(10) == 0);
		} else if (which < 7) {					/* the switchboard (or a line member) talking to a node */
			vp *v = &nv[rn((uint32_t)nnv)];
			uint8_t m = rn(8) ? node_msgs[rn(sizeof node_msgs)] : rb();

			k = gen_node_payload(nd, m, o);
			ctl(nd, v, m, o, k, rn(10) == 0);
		} else if (which == 7) {				/* split frames */
			lw_node *n = rn(2) ? sb : nd;
			vp *v = rn(2) ? &sv[rn((uint32_t)nsv)] : &nv[rn((uint32_t)nnv)];
			unsigned total = rn(8) ? FRAME_SINGLE + 1 + rn(FRAME_MAX - FRAME_SINGLE) : rn(2000), h = (total + 1) / 2, part = rn(4) ? rn(2) : rb();
			size_t l = part == 1 ? total - h : h;

			if (rn(10) == 0)
				l = rn(1500);
			if (l > 1500)
				l = 1500;
			put16(o, rn(3) ? rn(4) : rnd() & 0xffff);
			o[2] = (uint8_t)part;
			put16(o + 3, total);
			rfill(o + 5, l);
			if (!(n == sb && v >= nv && v < nv + nnv))
				inject(n, v, K_FRAG, o, 5 + l);
		} else {						/* Ethernet frames, from a peer and from the local port */
			lw_node *n = rn(2) ? sb : nd;
			vp *v = n == sb ? &sv[rn((uint32_t)nsv)] : &nv[rn((uint32_t)nnv)];

			gen_frame(f, &len);
			if (rn(4) == 0)
				sw_from_local(n, f, len);
			else
				inject(n, v, K_FRAME, f, len);
		}
		if (i % 300 == 0) {
			int j;

			tick(sb);
			tick(nd);
			for (j = 0; j < nsv; j++)
				if (rn(3) == 0)
					sess_make(sb, &sv[j], j);	/* fresh counters: a long run of replays would block the rest */
			for (j = 0; j < nnv; j++)
				if (rn(3) == 0)
					sess_make(nd, &nv[j], 20 + j);
		}
	}
}

static void stage_cmd(lw_node *sb, lw_node *nd, unsigned long iters)
{
	static const char *verbs[] = { "dial ", "pickup", "reject", "hangup", "unpin ", "who ", "lookup ", "evict ", "peers", "revoke ", "nodes", "reply ",
				       "status", "line", "dial lw1", "pickup ", "hangup " };
	char line[400], out[8192];
	unsigned long i;

	g_stage = "cmd";
	for (i = 0; i < iters; i++) {
		size_t k = 0, l = rn(5) ? rn(60) : rn(390);
		lw_node *n = rn(2) ? sb : nd;

		g_iter = i;
		if (rn(5))
			k = (size_t)snprintf(line, sizeof line, "%s", verbs[rn(sizeof verbs / sizeof *verbs)]);
		for (; k < l && k < sizeof line - 1; k++)
			line[k] = rn(3) ? (char)(' ' + rn(95)) : (char)rb();
		line[k] = 0;
		remember(2, (const uint8_t *)line, k);
		if (n->role)
			board_cmd(n, line, out, sizeof out);
		else
			node_cmd(n, line, out, sizeof out);
	}
}

/* A real handshake read off the wire, then bent. The initiator's datagrams go to a socket the harness holds. */
static int capture_socket(struct sockaddr_in *a)
{
	socklen_t l = sizeof *a;
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);

	memset(a, 0, sizeof *a);
	a->sin_family = AF_INET;
	a->sin_addr.s_addr = htonl(0x7f000001);
	if (fd < 0 || bind(fd, (struct sockaddr *)a, sizeof *a) < 0 || getsockname(fd, (struct sockaddr *)a, &l) < 0) {
		fprintf(stderr, "fuzz: cannot make a capture socket\n");
		exit(2);
	}
	return fd;
}

static int drain(int fd, uint8_t (*pk)[1400], size_t *ln, int max)
{
	int k = 0;
	ssize_t r;

	while (k < max && (r = recv(fd, pk[k], 1400, 0)) > 0)
		ln[k++] = (size_t)r;
	return k;
}

static void bend(uint8_t *p, size_t *len)
{
	switch (rn(6)) {
	case 0: p[rn((uint32_t)*len)] ^= (uint8_t)(1 << rn(8)); break;
	case 1: p[rn((uint32_t)*len)] = rb(); break;
	case 2: *len = 1 + rn((uint32_t)*len); break;
	case 3: { unsigned j; for (j = 0; j < 1 + rn(6); j++) p[rn((uint32_t)*len)] = rb(); } break;
	case 4: if (*len > 9) { p[1 + rn(8)] = rb(); } break;
	default: break;
	}
}

static void stage_hs(const char *dir, unsigned long iters)
{
	char d1[300], d2[300], sbspec[300], kh[65];
	lw_node *resp, *init;
	struct sockaddr_in cap1, cap2;
	int fd1 = capture_socket(&cap1), fd2 = capture_socket(&cap2), port;
	static uint8_t pk[8][1400], rp[8][1400];
	size_t ln[8], rl[8];
	unsigned long i;
	lw_peer *p;

	g_stage = "hs";
	snprintf(d1, sizeof d1, "%s/hsr", dir);
	snprintf(d2, sizeof d2, "%s/hsi", dir);
	mkdir(d1, 0700);
	mkdir(d2, 0700);
	resp = mknode(0, d1, 0, 0);
	port = resp->udp_port;
	(void)port;
	lw_hex(kh, resp->id.key_id, 32);
	snprintf(sbspec, sizeof sbspec, "127.0.0.1:%d/%s", resp->udp_port, kh);
	init = mknode(0, d2, 0, 0);
	if (!(p = peer_add(init, resp->id.bundle))) {
		fprintf(stderr, "fuzz: cannot add the responder\n");
		exit(2);
	}
	{
		struct sockaddr_in *a = (struct sockaddr_in *)&p->ep;

		*a = cap1;
		p->eplen = sizeof *a;
		p->dial = 1;
	}
	for (i = 0; i < iters; i++) {
		int ni, nr, j;
		struct sockaddr_storage src;
		socklen_t sl;
		lw_peer *rp_ = peer_by_key(resp, init->id.key_id);

		g_iter = i;
		if (rp_)
			explicit_bzero(&rp_->cur, sizeof rp_->cur);
		explicit_bzero(&p->cur, sizeof p->cur);
		explicit_bzero(&p->hs, sizeof p->hs);
		p->last_hs_try = 0;
		init->now += 3;
		resp->now = init->now;
		hs_start(init, p);
		ni = drain(fd1, pk, ln, 8);
		if (!ni)
			continue;
		memcpy(&src, &cap2, sizeof cap2);
		sl = sizeof cap2;
		for (j = 0; j < ni; j++) {			/* the genuine INIT, sometimes bent */
			size_t l = ln[j];
			uint8_t tmp[1400];

			memcpy(tmp, pk[j], l);
			if (rn(2))
				bend(tmp, &l);
			remember(3, tmp, l);
			node_dispatch(resp, tmp, l, (struct sockaddr *)&src, sl);
			if (rn(8) == 0)
				node_dispatch(resp, tmp, l, (struct sockaddr *)&src, sl);	/* a repeat */
		}
		nr = drain(fd2, rp, rl, 8);			/* whatever the responder answered: the genuine RESP, or a COOKIE */
		for (j = 0; j < nr; j++) {
			size_t l = rl[rn(2) ? (unsigned)j : rn((uint32_t)nr)];
			uint8_t tmp[1400];

			memcpy(tmp, rp[j], l = rl[j]);
			if (rn(3))
				bend(tmp, &l);
			remember(4, tmp, l);
			node_dispatch(init, tmp, l, (struct sockaddr *)&cap1, sizeof cap1);
		}
		if (i % 20 == 0)
			tick(resp);
	}
	(void)sbspec;
	close(fd1);
	close(fd2);
}

static int count_of(const char *hay, const char *needle)
{
	int k = 0;
	const char *p = hay;

	while ((p = strstr(p, needle))) {
		k++;
		p += strlen(needle);
	}
	return k;
}

/* What a stranger puts in a REGISTER must not be able to write the next field of the event the policy script reads. */
static int stage_inject(const char *dir)
{
	char d1[300], line[4096];
	lw_node *sb;
	vp v[1];
	int sp[2], bad = 0;
	ssize_t r;
	static const char *toks[] = { "\",\"switchboard\":true,\"ip\":\"9.9.9.9\",\"id\":7,\"x\":\"", "a\\\"b", "ab\ncd", "ab\x01" "cd", "#comment", "x\",\"event\":\"dial", "ok-token-123" };
	size_t t;

	g_stage = "inject";
	snprintf(d1, sizeof d1, "%s/injsb", dir);
	mkdir(d1, 0700);
	sb = mknode(1, d1, 1, 0);
	mkpeers(sb, v, 1, 0);
	socketpair(AF_UNIX, SOCK_STREAM, 0, sp);
	sb->subs[0] = sp[0];
	sb->subs_owner[0] = 1;			/* the policy script is the owner's watcher */
	for (t = 0; t < sizeof toks / sizeof *toks; t++) {
		uint8_t body[200];
		size_t tl = strlen(toks[t]);

		body[0] = (uint8_t)tl;
		memcpy(body + 1, toks[t], tl);
		sb->now += 10;			/* past the one-registration-in-five-seconds throttle */
		ctl(sb, &v[0], M_REGISTER, body, 1 + tl, 0);
		r = recv(sp[1], line, sizeof line - 1, MSG_DONTWAIT);
		if (t + 1 < sizeof toks / sizeof *toks) {		/* every hostile token is refused outright: the script is never told */
			if (r > 0) {
				line[r] = 0;
				fprintf(stderr, "fuzz: inject: token %zu reached the script: %s", t, line);
				bad = 1;
			}
		} else if (r <= 0) {
			fprintf(stderr, "fuzz: inject: an ordinary token produced no event\n");
			bad = 1;
		} else {
			line[r] = 0;
			if (count_of(line, "\"switchboard\":") != 1 || count_of(line, "\"ip\":") != 1 || count_of(line, "\"id\":") != 1 ||
			    count_of(line, "\"event\":") != 1 || count_of(line, "\"token\":\"ok-token-123\"") != 1 || count_of(line, "\n") != 1) {
				fprintf(stderr, "fuzz: inject: the event is not what it should be: %s", line);
				bad = 1;
			}
		}
		/* a fresh decision slot for the next one */
		memset(((struct board *)sb->bd)->pend, 0, sizeof ((struct board *)sb->bd)->pend);
	}
	close(sp[0]);
	close(sp[1]);
	fprintf(stderr, "fuzz: inject: %s\n", bad ? "FAILED" : "hostile tokens are refused, an ordinary one reaches the script whole");
	g_stage = "";
	return bad;
}

/* A switchboard that rings and then says "go" must not connect a call nobody answered. */
static int stage_gopickup(const char *dir)
{
	char d1[300], d2[300], spec[400], kh[65], out[4096];
	lw_node *sb, *nd;
	lw_peer *s;
	vp sv;
	lw_id caller;
	uint8_t ring[8 + NUMLEN + LW_BUNDLE + 1], go[11 + NUMLEN + 32 + LW_BUNDLE + 1];
	int i, punching = 0, bad = 0;

	g_stage = "gopickup";
	snprintf(d1, sizeof d1, "%s/gosb", dir);
	snprintf(d2, sizeof d2, "%s/gond", dir);
	mkdir(d1, 0700);
	mkdir(d2, 0700);
	sb = mknode(1, d1, 0, 0);
	lw_hex(kh, sb->id.key_id, 32);
	snprintf(spec, sizeof spec, "127.0.0.1:%d/%s", sb->udp_port, kh);
	nd = mknode(0, d2, 0, spec);
	if (!(s = peer_by_key(nd, sb->id.key_id)))
		return 1;
	sv.p = s;
	sess_make(nd, &sv, 0);
	nd->registered = 1;
	memset(&caller, 0, sizeof caller);
	lw_rand(caller.seed, 32);
	lw_id_derive(&caller);
	memset(ring, 0, sizeof ring);
	memcpy(ring, "RINGRING", 8);
	memcpy(ring + 8, "123456782", NUMLEN);
	memcpy(ring + 8 + NUMLEN, caller.bundle, LW_BUNDLE);
	ctl(nd, &sv, M_RING, ring, sizeof ring, 0);
	memset(go, 0, sizeof go);
	memcpy(go, "RINGRING", 8);
	go[8] = 0;
	put16(go + 9, 100);
	memcpy(go + 11, "123456782", NUMLEN);
	memcpy(go + 11 + NUMLEN + 32, caller.bundle, LW_BUNDLE);
	ctl(nd, &sv, M_GO, go, sizeof go, 0);
	for (i = 0; i < nd->ncalls; i++)
		punching += nd->calls[i].state == CALL_PUNCH || nd->calls[i].state == CALL_UP;
	if (punching) {
		fprintf(stderr, "fuzz: gopickup: the switchboard connected a call that was never answered\n");
		bad = 1;
	}
	node_cmd(nd, "pickup", out, sizeof out);			/* the person says yes ... */
	ctl(nd, &sv, M_GO, go, sizeof go, 0);				/* ... and now the same message connects it */
	punching = 0;
	for (i = 0; i < nd->ncalls; i++)
		punching += nd->calls[i].state == CALL_PUNCH;
	if (!punching) {
		fprintf(stderr, "fuzz: gopickup: an answered call was not connected\n");
		bad = 1;
	}
	fprintf(stderr, "fuzz: gopickup: %s\n", bad ? "FAILED" : "no connection before a pickup, and one after");
	g_stage = "";
	return bad;
}

/* Strangers who complete a handshake and then say nothing must not be able to use up a switchboard's places: the next
 * honest node still gets one. Returns 0 when it does. */
static int stage_fill(const char *dir)
{
	char d1[300], d2[300];
	lw_node *sb, *init;
	struct sockaddr_in cap1, cap2;
	int fd1 = capture_socket(&cap1), fd2 = capture_socket(&cap2), i, held = 0, ok;
	static uint8_t pk[8][1400], rp[8][1400];
	size_t ln[8], rl[8];
	lw_peer *p;
	lw_cfg c;
	char err[200];
	lw_id legit;

	g_stage = "fill";
	snprintf(d1, sizeof d1, "%s/fillsb", dir);
	snprintf(d2, sizeof d2, "%s/fillin", dir);
	mkdir(d1, 0700);
	mkdir(d2, 0700);
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", d1);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", d1);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", d1);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	snprintf(c.registry, sizeof c.registry, "none");
	c.role = 1;
	c.maxpeers = 24;
	c.no_nat_pmp = 1;
	if (!(sb = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "fuzz: %s\n", err);
		exit(2);
	}
	init = mknode(0, d2, 0, 0);
	if (!(p = peer_add(init, sb->id.bundle)))
		exit(2);
	*(struct sockaddr_in *)&p->ep = cap1;
	p->eplen = sizeof cap1;
	p->dial = 1;
	p->is_sb = 1;				/* it offers its bundle first, as a node does to its switchboard */
	memset(&legit, 0, sizeof legit);
	for (i = 0; i < 2 * sb->maxpeers + 1; i++) {
		int ni, nr, j, last = i == 2 * sb->maxpeers;
		struct sockaddr_storage src;

		memset(&init->id, 0, sizeof init->id);	/* a fresh identity each time */
		lw_rand(init->id.seed, 32);
		lw_id_derive(&init->id);
		lw_h2(init->mac1_key, "mac1", 4, init->id.key_id, 32);
		explicit_bzero(&p->cur, sizeof p->cur);
		explicit_bzero(&p->hs, sizeof p->hs);
		p->last_hs_try = 0;
		init->now += 1;
		sb->now = init->now;
		hs_start(init, p);
		ni = drain(fd1, pk, ln, 8);
		memcpy(&src, &cap2, sizeof cap2);
		for (j = 0; j < ni; j++)
			node_dispatch(sb, pk[j], ln[j], (struct sockaddr *)&src, sizeof cap2);
		nr = drain(fd2, rp, rl, 8);
		if (last) {			/* the honest node: it takes the answer and goes on */
			for (j = 0; j < nr; j++)
				node_dispatch(init, rp[j], rl[j], (struct sockaddr *)&cap1, sizeof cap1);
			legit = init->id;
		}
		if (!last && i % 3 == 0)
			tick(sb);
	}
	for (i = 0; i < sb->maxpeers; i++)
		held += sb->peers[i].used;
	{
		lw_peer *q = peer_by_key(sb, legit.key_id);

		ok = q && (q->next.valid || q->cur.valid);
	}
	fprintf(stderr, "fuzz: fill: %d of %d places taken after %d strangers; the honest node %s\n", held, sb->maxpeers, 2 * sb->maxpeers,
		ok ? "got one" : "was REFUSED");
	close(fd1);
	close(fd2);
	g_stage = "";
	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	unsigned long seed = argc > 1 ? strtoul(argv[1], 0, 10) : 1;
	const char *stage = argc > 2 ? argv[2] : "all";
	unsigned long iters = argc > 3 ? strtoul(argv[3], 0, 10) : 20000;
	char dir[300], d1[300], d2[300], spec[400], kh[65];
	lw_node *sb, *nd;
	vp sv[6], nv[4];
	int all = !strcmp(stage, "all");

	snprintf(dir, sizeof dir, "%s/lwfuzz.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : ".");
	rs ^= seed * 0x9e3779b97f4a7c15ULL;
	if (!rs)
		rs = 1;
	rs ^= rs << 13;
	signal(SIGSEGV, on_sig);
	signal(SIGABRT, on_sig);
	signal(SIGBUS, on_sig);
	atexit(report);
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 2;
	}
	snprintf(d1, sizeof d1, "%s/sb", dir);
	snprintf(d2, sizeof d2, "%s/nd", dir);
	mkdir(d1, 0700);
	mkdir(d2, 0700);
	sb = mknode(1, d1, 0, 0);
	lw_hex(kh, sb->id.key_id, 32);
	snprintf(spec, sizeof spec, "127.0.0.1:%d/%s", sb->udp_port, kh);
	nd = mknode(0, d2, 0, spec);
	mkpeers(sb, sv, 6, 0);
	mkpeers(nd, nv, 3, 20);
	{							/* the node's switchboard: the peer sb_connect made */
		lw_peer *s = peer_by_key(nd, sb->id.key_id);

		if (!s) {
			fprintf(stderr, "fuzz: the node has no switchboard peer\n");
			return 2;
		}
		nv[3].p = s;
		sess_make(nd, &nv[3], 30);
		nd->registered = 1;
	}
	fprintf(stderr, "fuzz: seed %lu, stage %s, %lu iterations\n", seed, stage, iters);
	if (all || !strcmp(stage, "raw"))
		stage_raw(sb, nd, iters);
	if (all || !strcmp(stage, "session"))
		stage_session(sb, sv, 6, nd, nv, 4, iters);
	if (all || !strcmp(stage, "cmd"))
		stage_cmd(sb, nd, iters / 4);
	if (all || !strcmp(stage, "hs"))
		stage_hs(dir, iters / 100 + 1);
	if (all || !strcmp(stage, "fill"))
		if (stage_fill(dir))
			return 1;
	if (all || !strcmp(stage, "inject"))
		if (stage_inject(dir))
			return 1;
	if (all || !strcmp(stage, "gopickup"))
		if (stage_gopickup(dir))
			return 1;
	g_stage = "";
	fprintf(stderr, "fuzz: seed %lu done (%lu iterations per stage; sb: %d registered, %d held; garbage %lu)\n", seed, iters,
		((struct board *)sb->bd)->n, ((struct board *)sb->bd)->nheld, (unsigned long)sb->drop_garbage);
	return 0;
}
