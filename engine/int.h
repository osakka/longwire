/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LW_INT_H
#define LW_INT_H

#include <netinet/in.h>
#include <sys/socket.h>
#include "lw.h"
#include "proto.h"
#include "../vendor/monocypher/monocypher.h"

#define T_HS_INIT 0x01
#define T_HS_RESP 0x02
#define T_DATA    0x04

#define K_FRAME     0x00
#define K_KEEPALIVE 0x02

#define HS_CHUNK 1212
#define HS_INIT_LEN 2416
#define HS_RESP_LEN 2260
#define WIN_WORDS 64			/* 4096-packet replay window */
#define MAC_SLOTS 8192
#define FRAME_MAX 1514
#define FRAME_SINGLE 1428		/* a frame up to this size travels in one datagram, larger ones in two: 1428 + 29 + 4 (relay) + 28 = 1489 fits PPPoE */
#define LINE_MSS4 1340			/* TCP MSS on the line: a segment with 12 bytes of options is a 1406-byte frame, under FRAME_SINGLE */
#define LINE_MSS6 1320
#define SITE_MEMBERS 4
#define MAXSUBS 8
#define OFFERS 1024			/* bundles offered ahead of a handshake: hashed by key id, so a flood costs bandwidth, not a few packets */
#define HS_SLOTS 512			/* handshake fragments being put together: hashed by source, for the same reason */
#define SRC_BUCKETS 512
#define ED_OFF (1 + 32 + LW_MLKEM_PK)	/* where a bundle keeps its Ed25519 public key */

typedef struct lw_sess {
	int      valid, confirmed, we_init;
	uint32_t rx_idx, tx_idx;
	uint8_t  k_send[32], k_recv[32];
	uint64_t ctr_send, rx_max;
	uint64_t win[WIN_WORDS];
	int      got_any;
	double   born;
} lw_sess;

typedef struct lw_hsi {			/* initiator's pending handshake */
	int      active;
	uint16_t hs_id;
	uint32_t my_idx;
	uint8_t  e[32], ck[32], h[32];
	uint8_t  tag0[16];		/* tag of the first INIT fragment: the associated data of a cookie reply */
	uint8_t  ek[LW_MLKEM_SK];
	uint8_t  msg[HS_INIT_LEN];
	double   sent, next_retry;
	int      tries;
} lw_hsi;

typedef struct frag {		/* frames being put back together, per peer */
	struct { int used; uint16_t id; int total, got; double t; uint8_t buf[FRAME_MAX]; } s[4];
} frag;

typedef struct lw_sas {			/* the committed safety phrase of one call (docs/adr/0040) */
	int      mode;			/* 0 no call of ours made this peer, 1 the committed phrase, 2 the old one (the other end did not answer: it is older, or something drops its messages) */
	int      caller, state;		/* state: 0 waiting, 1 the phrase is known, 2 refused */
	int      auto_;			/* a mesh call to a fellow member: --require-sas does not apply */
	uint8_t  na[16], nb[16], c[32];	/* the caller's nonce and its commitment, the callee's nonce */
	int      have_na, have_nb, have_c;
	uint8_t  bind[32];		/* what both ends of this session share and a node in the middle does not */
	int      tries;
	double   next, born;
	char     words[120];
} lw_sas;

typedef struct lw_peer {
	int      used;
	int      have_bundle;		/* a peer known only by key id (the switchboard) has none yet */
	uint8_t  bundle[LW_BUNDLE], key_id[32];
	const uint8_t *x_pk, *kem_pk;	/* into bundle */
	struct sockaddr_storage ep;
	socklen_t eplen;
	int      dial;			/* we initiate */
	int      is_sb;			/* this peer is our switchboard: control traffic only, never bridged */
	char     number[NUMLEN + 1];	/* dialed/answered number, "" for static peers */
	lw_sess  cur, prev, next;
	int      sb_registered;		/* (a switchboard) this session has registered a number: it is not a place a stranger is holding */
	double   last_reg, msg_tok, msg_t;	/* (a switchboard) when it last registered, and its budget for control messages */
	lw_hsi   hs;
	uint8_t  last_tai[12];
	frag    *fr;			/* allocated on the first split frame */
	double   bc_tok, bc_t;		/* flood budget for frames we send this peer */
	uint8_t  cookie[16];		/* what the responder gave us when it was under load */
	double   cookie_t;
	double   last_tx, last_rx, last_hs_try, hello_next, touched;
	int      hello_tries;
	int      dialer;		/* we made the call that created this peer */
	int      relayed;		/* every datagram for this peer goes through a relay: peers[via_peer], on route via_route */
	int      via_peer;
	uint32_t via_route;
	int      announced;		/* the relayed session was reported */
	struct sockaddr_storage probe_ep;	/* a direct address to try while relayed */
	socklen_t probe_len;
	double   probe_until, next_direct;
	int      direct_tries;
	uint8_t  via_kid[32];		/* the relay's key id: its peer slot may have been reused */
	double   probe_next, lost_t;
	double   rev_next;		/* when to tell this peer again what the operator has revoked (0: not on the line) */
	int      lh_sent, lh_got, lh_has, linked;	/* the line handshake with this peer */
	uint8_t  lh_id[8];
	int      sfd;			/* a socket of its own, if a path was found by spraying (0: the node's) */
	uint64_t rx_frames, tx_frames, rx_bytes, tx_bytes;
	uint64_t rekeys, drop_auth, drop_replay;
	lw_sas   sas;			/* the phrase of the call that made this peer */
} lw_peer;

typedef struct lw_route {		/* at a relay: a route between two members */
	int      used;
	uint32_t id;			/* the id this route's owner puts on its packets */
	int      owner, remote;		/* peer indexes: who sends on this route, and who it is forwarded to */
	uint32_t remote_id;
	double   last;
	double   tok, tok_t;		/* a per-route budget: a relay is not a free amplifier */
} lw_route;

typedef struct mac_ent {
	uint8_t mac[6];
	int16_t port;			/* -1 local, else peer index; -2 free */
	double  seen;
} mac_ent;

typedef struct hs_slot {		/* handshake fragment reassembly, per source */
	int used; double t;
	struct sockaddr_storage src; socklen_t srclen;
	uint8_t type; uint16_t hs_id; int count, got, cok;
	uint8_t buf[2][HS_CHUNK]; int len[2];
} hs_slot;

typedef struct lw_offer {		/* a bundle a stranger sent ahead of its handshake */
	uint8_t bundle[LW_BUNDLE], key_id[32];
	double  t;
	int     used;
} lw_offer;

/* a call, from either end (one at a time in v1) */
enum { CALL_IDLE, CALL_OUT, CALL_RINGING, CALL_PUNCH, CALL_SETUP, CALL_UP };

typedef struct lw_call {
	int      state, we_caller, peer;
	int      auto_;			/* a mesh call to a fellow member: nobody is asked */
	int      admitted;		/* (a ring) the caller held a valid admit */
	char     name[LW_NAME_MAX + 1];	/* (a ring) the name the caller announced, if the switchboard carried one */
	uint8_t  admit[ADMIT_LEN];	/* (a ring) what it presented, or (a dial) what we presented */
	int      has_admit;
	int      answered;		/* (a ring) a person, or the policy, said yes: nothing connects before that */
	uint8_t  id[8], token[32];
	char     number[NUMLEN + 1];
	uint8_t  bundle[LW_BUNDLE];	/* the other end's, from RING or GO */
	lw_cand  pc[MAXCANDS];
	int      npc;
	int      peer_strict;		/* the other end said its NAT makes a mapping per destination */
	int      sprayed;
	double   probe_last, probe_credit;
	double   deadline, start_at, next_punch, lost_t;
	int      have_verified;
	struct sockaddr_storage verified;
	socklen_t verified_len;
} lw_call;

typedef struct lw_member {
	uint8_t  key_id[32], ed_pub[32], sb[32];
	char     number[NUMLEN + 1];
	uint8_t  role;
	double   next_try;		/* the mesh maintainer's back-off */
	int      tries;
	double   next_relay;		/* when to ask for a relay again */
} lw_member;

typedef struct lw_evict {
	uint8_t  key_id[32];
	uint64_t time;
} lw_evict;

typedef struct lw_line {
	int      have;
	uint8_t  id[8], founder[32], key[32], prev_key[32];
	double   key_t;			/* when the key last changed: the grace window for the old one */
	uint8_t  authority[32];		/* the operator's public key; all zero on a line members run among themselves */
	int      managed;
	uint8_t  rev[32][EVICT_LEN];	/* the operator's evictions, as signed */
	int      nrev;
	uint8_t  admit[ADMIT_LEN];	/* this node's own */
	lw_member m[LINE_MAX];
	int      nm;
	lw_evict ev[32];
	int      nev;
	double   joined, tick;
	int      said_partial, said_complete;
	int      rr;			/* round-robin cursor over relay candidates */
} lw_line;

typedef struct pin {
	uint8_t sb[32];			/* the switchboard the number belongs to: a number means nothing without one */
	char    number[NUMLEN + 1];
	uint8_t key_id[32];
} pin;

struct board;

#define LW_JNETS 32
typedef struct lw_jnet {		/* a network connected to the jack: address and mask, one family */
	uint8_t fam, net[16], mask[16];
} lw_jnet;

struct lw_node {
	lw_cfg   cfg;
	lw_id    id;
	uint8_t  mac1_key[32];		/* H("mac1" | my key_id) */
	int      udp, port_fd, ctl_fd, stop;
	int      port_is_sock;
	char     peer_sock[256];	/* sock port: where frames go */
	lw_peer *peers;
	int      maxpeers;
	mac_ent  macs[MAC_SLOTS];
	hs_slot  slots[HS_SLOTS];
	uint64_t hsalt;			/* per boot: where a source lands in slots and srcl is not for an attacker to choose */
	int      idxbits;		/* the low bits of a session index say which peer it is for */
	lw_offer offers[OFFERS];
	uint64_t relay_fwd, relay_drop, relay_routes;
	uint64_t rx_pkts, drop_garbage, drop_noroute, flood, local_tx, local_rx;
	double   now, started;
	int      rekey_after, keepalive, mtu;
	int      sb_rekey;			/* rekey interval of a switchboard session, on both ends (default 600) */
	int      role;			/* 0 node, 1 switchboard */
	/* node */
	int      sb;			/* peer index of our switchboard, -1 none */
	char     number[NUMLEN + 1];
	int      registered;
	double   reg_born;		/* the switchboard session we registered on */
	double   reg_refused;		/* the session on which we were refused: don't ask again until it changes */
	uint64_t reg_count;		/* NUMBER replies received (registrations, including after each rekey) */
	char     reg_why[160];		/* why the switchboard last refused to register us, "" if it has not (already escaped for JSON) */
	double   next_hb, next_reg;
	lw_cand  refl;			/* what the switchboard saw of us */
	int      have_refl;
	lw_call *calls;			/* the call table: a line is many calls at once */
	int      ncalls;
	lw_line  line;
	char     home_code[10];		/* the code the registry assigned (to our switchboard, or if we are one, to us); empty if none */
	uint8_t  sb_caps;		/* what the switchboard offers: CAPS_* */
	struct { int active, kind; uint8_t key[32]; double t; } look;	/* a lookup in flight: dialing a full address waits on it */
	lw_route routes[64];
	int      rx_fd;			/* the socket the datagram being handled came in on: replies go out the same way */
	int      spray_fd[SPRAY_N], nspray;
	int      rx_relayed;		/* the datagram being handled came out of a relay: it says nothing about the peer's address */
	pin     *pins;
	int      npins, cappins;
	uint16_t udp_port;
	struct {				/* NAT-PMP (pmp.c) */
		uint8_t  gw[4], ip[4];
		int      have_ip, tries;
		uint16_t port;			/* the external port the router gave us, 0 for none */
		double   next, expire;
	} pmp;
	int      udp6;
	uint16_t udp6_port;	/* the IPv6 socket, if there is one: -1 otherwise */
	char     uudev[32];		/* the device it is tied to */
	char     tie[32];		/* everything through the line: the device all our sockets are tied to (docs/adr/0045) */
	int      roamed;		/* that device is no longer the road: the jack was let go (docs/adr/0046) */
	int      uudp;			/* the underlay socket (under.c): -1 not opened yet, -2 none to be had */
	lw_jnet  jnet[LW_JNETS];	/* the networks connected to our jack */
	int      njnet;
	double   jnet_at;
	char     jnet_dev[32];
	int      udp2;			/* a switchboard's second port (udp_port + 1): answers ECHO, so a node can tell its NAT's mapping type */
	int      nat;			/* 0 not measured, 1 easy (same mapping toward both ports), 2 strict (a new mapping per destination) */
	uint8_t  echo_nonce[8];
	int      echo_tries;
	double   next_echo;
	/* M2: the switch's extras and the site group */
	double   dhcp_say;		/* when a dhcp-conflict was last announced */
	struct { uint32_t ip; double t, said; } dhcp[4];	/* servers whose offers were seen lately (ARCHITECTURE section 3) */
	uint64_t dhcp_conflicts;
	uint64_t frames_split, frames_joined, drop_flood, arp_answered, drop_standby, resegmented, drop_oversize, cand_skipped;
	double cand_skip_said;		/* when a skipped address was last announced */
	uint16_t frag_id;
	struct { uint32_t ip; uint8_t mac[6]; double t; } arp[256];
	struct { uint8_t ip[16]; uint8_t mac[6]; double t; } nd[256];	/* IPv6 neighbours learned from neighbour discovery */
	int      splice;			/* the local port is a NIC on a LAN segment */
	uint8_t  nic_mac[6];
	uint8_t  site_key[32], site_key_prev[32];
	double   site_key_prev_t;
	int      have_site_key, standby, gratuitous;
	struct { uint8_t key_id[32]; double seen; int used; } site[SITE_MEMBERS];
	double   site_probe_t, site_since, site_sync_t, site_sync_sent;
	uint8_t  site_sync[64][6];	/* remote hosts the active box knows, so a standby can announce them when it takes over */
	int      site_sync_n;
	/* events and control */
	int      subs[MAXSUBS];
	int      subs_owner[MAXSUBS];	/* the watcher runs as the user the worker runs as: it may be told what a group member may not */
	struct board *bd;
	struct banset *bn;			/* strikes and bans (ban.c) */
	uint64_t strikes_given, bans_issued, net_bans_issued, bans_refused, bans_suppressed;
	double   hello_tokens, hello_t;
	struct { struct sockaddr_storage a; socklen_t l; double tok, t; } hello_src[16];	/* a budget for each asker, too */
	double   hs_tokens, hs_t;	/* handshake budget: past half spent, the node is under load and wants cookies */
	uint64_t hs_shed, cookies_sent, cookies_got, hs_nocookie, hs_badmac2;
	int      cookie_ok;		/* the INIT being consumed carried valid cookie MACs on every fragment */
	uint8_t  csecret[2][32];	/* cookie secret now and before the last rotation */
	double   csecret_t;
	struct { uint64_t id; double tok, t; } srcl[SRC_BUCKETS];	/* per-source budget for cookie holders, under load */
};

double lw_now(void);
void   lw_rand(void *p, size_t n);
void   lw_h(uint8_t out[32], const void *a, size_t an);
void   lw_h2(uint8_t out[32], const void *a, size_t an, const void *b, size_t bn);

/* id.c: Noise-style key derivation and the AEAD used in handshakes (nonce 0) */
void lw_kdf1(uint8_t ck[32], const void *x, size_t xn);
void lw_kdf2(uint8_t ck[32], const void *x, size_t xn, uint8_t k[32]);
void lw_seal0(uint8_t *out, const uint8_t k[32], const void *pt, size_t n, const uint8_t ad[32]);
int  lw_open0(uint8_t *pt, const uint8_t k[32], const uint8_t *ct, size_t ctn, const uint8_t ad[32]);

/* hs.c */
int  hs_init_build(lw_node *n, lw_peer *p);
int  hs_init_consume(lw_node *n, const uint8_t *m, size_t len, uint16_t hs_id, const struct sockaddr *src, socklen_t sl);
int  hs_resp_consume(lw_node *n, const uint8_t *m, size_t len, uint16_t hs_id, const struct sockaddr *src, socklen_t sl);
void hs_send(lw_node *n, int type, uint16_t hs_id, const uint8_t *m, size_t len,
	     const uint8_t peer_key_id[32], const struct sockaddr *dst, socklen_t dl,
	     const uint8_t *cookie, uint8_t *tag0_out, lw_peer *to);
void hs_start(lw_node *n, lw_peer *p);
void cookie_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void hs_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);

/* sess.c */
uint32_t sess_new_idx(lw_node *n, const lw_peer *p);
unsigned offer_home(const lw_node *n, const uint8_t kid[32]);		/* where a stranger's offered bundle waits: a keyed hash, per boot */
void sess_set_keys(lw_sess *s, int we_init, const uint8_t ck[32], uint32_t rx_idx, uint32_t tx_idx, double now);
int  sess_send(lw_node *n, lw_peer *p, uint8_t kind, const uint8_t *body, size_t len);
void sess_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void sess_timers(lw_node *n);
const char *sess_state(const lw_peer *p);
/* peers */
lw_peer *peer_add(lw_node *n, const uint8_t bundle[LW_BUNDLE]);		/* NULL when the table is full */
lw_peer *peer_add_keyid(lw_node *n, const uint8_t key_id[32]);		/* bundle comes later, by HELLO */
lw_peer *peer_by_key(lw_node *n, const uint8_t key_id[32]);
lw_peer *peer_for_unknown(lw_node *n, const uint8_t key_id[32]);	/* switchboard: a stranger that offered itself */
void peer_drop(lw_node *n, lw_peer *p);
int  ctl_msg_send(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *payload, size_t len);

/* site.c: boxes sharing one LAN segment elect one to forward */
int  site_init(lw_node *n, char *err, size_t errn);
void site_timers(lw_node *n);
int  site_input(lw_node *n, const uint8_t *f, size_t len);	/* 1 = a probe, consumed */
const char *site_role(const lw_node *n);
int  site_members(const lw_node *n);

/* tagged optional parts: tag(1) len(1) bytes, one after another to the end of the message */
static inline int tlv_put(uint8_t *o, uint8_t tag, const void *v, size_t n)
{
	o[0] = tag;
	o[1] = (uint8_t)n;
	if (n)
		memcpy(o + 2, v, n);
	return (int)(2 + n);
}

static inline const uint8_t *tlv_find(const uint8_t *p, size_t len, uint8_t tag, size_t *n)
{
	while (len >= 2) {
		size_t l = p[1];

		if (l + 2 > len)
			return 0;
		if (p[0] == tag) {
			*n = l;
			return p + 2;
		}
		p += l + 2;
		len -= l + 2;
	}
	return 0;
}

int  name_ok(const uint8_t *s, size_t n);			/* UTF-8, no control characters, at most LW_NAME_MAX bytes */
void json_esc(char *out, size_t cap, const char *in);		/* a string safe to put between JSON quotes */

/* relay.c: carrying a pair's packets through a third member (docs/PROTOCOL.md §5) */
void relay_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void relay_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len);
int  relay_request(lw_node *n, const uint8_t member_kid[32], int *cursor);	/* ask a connected member to relay to this one */
void peer_heard_direct(lw_node *n, lw_peer *p, const struct sockaddr *src, socklen_t sl);
void relay_peer_gone(lw_node *n, int idx);
void relay_timers(lw_node *n);
void node_dispatch(lw_node *n, const uint8_t *buf, size_t r, const struct sockaddr *ss, socklen_t sl);
const lw_member *line_member(lw_node *n, const uint8_t kid[32]);
void peer_out(lw_node *n, lw_peer *p, const uint8_t *buf, size_t len);	/* send to a peer by whatever path it has */
int  node_public(lw_node *n);
int  relay_offered(lw_node *n);
int  relay_live(const lw_node *n);							/* is this node reachable directly from the internet */

/* sw.c */
/* spray.c */
int  peer_fd(const lw_node *n, const lw_peer *p);
int  ufd(const lw_node *n, int family);			/* the socket that reaches an address of this family */
void cand_sa(const lw_cand *c, struct sockaddr_storage *ss, socklen_t *sl);
void sa_cand(const struct sockaddr *sa, lw_cand *c);
void spray_close(lw_node *n);
void spray_reap(lw_node *n);
int  spray_is(const lw_node *n, int fd);
/* ban.c */
void strike(lw_node *n, lw_peer *p, double w, const char *why);
int  ban_active(lw_node *n, const uint8_t kid[32]);
int  ban_net_blocked(lw_node *n, const struct sockaddr_storage *ep);
void ban_timers(lw_node *n);
void ban_free(lw_node *n);
int  ban_json(lw_node *n, char *o, size_t cap);
int  ban_clear(lw_node *n, const char *arg);
int  board_known_key(lw_node *n, const uint8_t kid[32]);
/* under.c */
void jnets_refresh(lw_node *n);
void jnets_fresh(lw_node *n);
int  jack_net_has(const lw_node *n, int fam, const uint8_t *ip);
int  jack_net_sa(const lw_node *n, const struct sockaddr *sa);
int  under_fd(lw_node *n);
int  under_tie(lw_node *n, char *err, size_t errn);
int  tie_fd(const lw_node *n, int fd);
ssize_t usend(lw_node *n, int fd, const void *b, size_t len, const struct sockaddr *sa, socklen_t sl);
void spray_adopt(lw_node *n, lw_peer *p);
void spray_step(lw_node *n, lw_call *c);
void punch_pkt(lw_node *n, const lw_call *c, uint8_t pk[33]);
void sw_from_local(lw_node *n, const uint8_t *f, size_t len);
void sw_from_peer(lw_node *n, int pi, const uint8_t *f, size_t len);
int  sw_count(const lw_node *n);
int  resegment(lw_node *n, const uint8_t *frame, size_t len);
void l4_csum_fix(uint8_t *f, size_t len);
unsigned lw_sum16(const uint8_t *p, size_t n, unsigned sum);
int  sw_remote_macs(lw_node *n, uint8_t out[][6], int max);	/* MACs learned behind peers */
void sw_gratuitous(lw_node *n);
void sw_fragment(lw_node *n, int pi, const uint8_t *body, size_t len);

/* port.c */
int  port_open(lw_node *n, char *err, size_t errn);
int  port_read(lw_node *n, uint8_t *buf, size_t cap);
void port_write(lw_node *n, const uint8_t *f, size_t len);

/* ctl.c */
int  ctl_open(lw_node *n, char *err, size_t errn);
void ctl_accept(lw_node *n);
void ctl_close(lw_node *n);
void ev_emit_owner(lw_node *n, const char *fmt, ...);
void ev_emit(lw_node *n, const char *fmt, ...);
void ctl_subs_reap(lw_node *n);
int num_wire_ok(const uint8_t *s);			/* nine digits: the only shape a number has on the wire, in a file, in an event */
void net_str(const struct sockaddr_storage *ss, char *out, size_t n);
void addr_str(const struct sockaddr_storage *ss, char *out, size_t n);
int  lw_resolve(const char *hp, struct sockaddr_storage *ss, socklen_t *sl);

/* call.c: HELLO, candidates, pins, calls, punching (node side) */
void hello_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void hello_offer(lw_node *n, const struct sockaddr *dst, socklen_t dl);
void hello_request(lw_node *n, lw_peer *p);
void echo_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void echo_serve(lw_node *n);
void pmp_timers(lw_node *n);
void pmp_input(lw_node *n, const uint8_t *p, size_t len, const struct sockaddr *src, socklen_t sl);
void punch_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void node_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len);
void node_timers(lw_node *n);
int  node_cmd(lw_node *n, const char *line, char *out, size_t cap);
int  call_dial(lw_node *n, const char *num9, int auto_, char *err, size_t errn);
void sas_timers(lw_node *n);
void call_peer_up(lw_node *n, lw_peer *p, int dialer);	/* a call came up: the line handshake starts */
int  call_for_number(lw_node *n, const char *num9);	/* is there a call (any state) with this number */
const uint8_t *peer_ed_pub(const lw_peer *p);
lw_peer *peer_by_number(lw_node *n, const char *num9);
int  node_status_json(lw_node *n, char *o, size_t cap);
int  node_replay(lw_node *n, char *o, size_t cap);	/* what a new event subscriber missed: our registration state */
int  node_init(lw_node *n, char *err, size_t errn);
void fmt_number(char out[16], const char *num9);
int  sb_connect(lw_node *n, const char *spec, char *err, size_t errn);	/* the switchboard (or registry) this node registers with */
void registration_timers(lw_node *n);
void home_kid(const lw_node *n, uint8_t out[32]);			/* key id of this node's switchboard (zeros if none) */
void sb_code(const uint8_t kid[32], char out[9]);			/* the derived code, 0 and six digits: every switchboard has, derived from its key */
void full_number(const lw_node *n, const char *local, char *out, size_t cap);	/* "+731042 312-884-076" */
int  parse_full(const lw_node *n, const char *in, char out[NUMLEN + 1], char *err, size_t errn);	/* local, or code + local */
int  parse_number(const char *in, char out[NUMLEN + 1]);	/* 0 = ok, check digit verified */
int  damm_ok(const char *digits, int n);
void mint_number(char out[NUMLEN + 1]);
void phrase_of(char *out, size_t cap, const uint8_t a[32], const uint8_t b[32]);

/* line.c: lines of more than two nodes (docs/PROTOCOL.md §6) */
void line_init(lw_node *n);
int  line_have(const lw_node *n);
int  line_admit_for(lw_node *n, uint8_t out[ADMIT_LEN]);	/* our own admit, to present when dialing; 0 if none */
int  line_admit_ok(lw_node *n, const uint8_t *admit, const uint8_t caller_kid[32]);
int  line_evicted(lw_node *n, const uint8_t kid[32]);
int  line_presented(lw_node *n, const char *number, uint8_t out[ADMIT_LEN]);	/* the admit that number's call carried */
void line_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len);
void line_call_up(lw_node *n, lw_peer *p);
void line_timers(lw_node *n);
int  line_cmd(lw_node *n, const char *cmd, char *out, size_t cap);
int  line_status_json(lw_node *n, char *o, size_t cap);
int  line_value(lw_node *n, char *o, size_t cap);
int  line_replay(lw_node *n, char *o, size_t cap);
void line_leave(lw_node *n);
void line_member_left(lw_node *n, const uint8_t kid[32]);
void call_drop_peer(lw_node *n, lw_peer *p, const char *why);	/* end every call on this peer and drop it */
void call_drop_all(lw_node *n, const char *why);
void lw_sign(const lw_id *id, const uint8_t *m, size_t n, uint8_t sig[64]);
int  lw_verify(const uint8_t pub[32], const uint8_t *m, size_t n, const uint8_t sig[64]);
void site_derive(lw_node *n);					/* the site secret from the line key */

/* board.c: the switchboard worker */
int  board_open(lw_node *n, char *err, size_t errn);
void board_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len);
void board_timers(lw_node *n);
int  board_cmd(lw_node *n, const char *line, char *out, size_t cap);
int  board_status_json(lw_node *n, char *o, size_t cap);

#endif
