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
#define MAXSUBS 4
#define OFFERS 16

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
	uint8_t  ek[LW_MLKEM_SK];
	uint8_t  msg[HS_INIT_LEN];
	double   sent, next_retry;
	int      tries;
} lw_hsi;

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
	lw_hsi   hs;
	uint8_t  last_tai[12];
	double   last_tx, last_rx, last_hs_try, hello_next, touched;
	int      hello_tries;
	uint64_t rx_frames, tx_frames, rx_bytes, tx_bytes;
	uint64_t rekeys, drop_auth, drop_replay;
} lw_peer;

typedef struct mac_ent {
	uint8_t mac[6];
	int16_t port;			/* -1 local, else peer index; -2 free */
	double  seen;
} mac_ent;

typedef struct hs_slot {		/* handshake fragment reassembly, per source */
	int used; double t;
	struct sockaddr_storage src; socklen_t srclen;
	uint8_t type; uint16_t hs_id; int count, got;
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
	uint8_t  id[8], token[32];
	char     number[NUMLEN + 1];
	uint8_t  bundle[LW_BUNDLE];	/* the other end's, from RING or GO */
	lw_cand  pc[MAXCANDS];
	int      npc;
	double   deadline, start_at, next_punch;
	int      have_verified;
	struct sockaddr_in verified;
} lw_call;

typedef struct pin {
	char    number[NUMLEN + 1];
	uint8_t key_id[32];
} pin;

struct board;

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
	hs_slot  slots[8];
	lw_offer offers[OFFERS];
	uint64_t rx_pkts, drop_garbage, drop_noroute, flood, local_tx, local_rx;
	double   now, started;
	int      rekey_after, keepalive, mtu;
	int      role;			/* 0 node, 1 switchboard */
	/* node */
	int      sb;			/* peer index of our switchboard, -1 none */
	char     number[NUMLEN + 1];
	int      registered;
	double   reg_born;		/* the switchboard session we registered on */
	double   reg_refused;		/* the session on which we were refused: don't ask again until it changes */
	uint64_t reg_count;		/* NUMBER replies received (registrations, including after each rekey) */
	double   next_hb, next_reg;
	lw_cand  refl;			/* what the switchboard saw of us */
	int      have_refl;
	lw_call  call;
	pin     *pins;
	int      npins, cappins;
	uint16_t udp_port;
	/* events and control */
	int      subs[MAXSUBS];
	struct board *bd;
	double   hello_tokens, hello_t;
	double   hs_tokens, hs_t;	/* handshake budget (interim DoS limit until cookies, M3) */
	uint64_t hs_shed;
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
	     const uint8_t peer_key_id[32], const struct sockaddr *dst, socklen_t dl);
void hs_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);

/* sess.c */
uint32_t sess_new_idx(lw_node *n);
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

/* sw.c */
void sw_from_local(lw_node *n, const uint8_t *f, size_t len);
void sw_from_peer(lw_node *n, int pi, const uint8_t *f, size_t len);
int  sw_count(const lw_node *n);

/* port.c */
int  port_open(lw_node *n, char *err, size_t errn);
int  port_read(lw_node *n, uint8_t *buf, size_t cap);
void port_write(lw_node *n, const uint8_t *f, size_t len);

/* ctl.c */
int  ctl_open(lw_node *n, char *err, size_t errn);
void ctl_accept(lw_node *n);
void ctl_close(lw_node *n);
void ev_emit(lw_node *n, const char *fmt, ...);
void ctl_subs_reap(lw_node *n);
void addr_str(const struct sockaddr_storage *ss, char *out, size_t n);
int  lw_resolve(const char *hp, struct sockaddr_storage *ss, socklen_t *sl);

/* call.c: HELLO, candidates, pins, calls, punching (node side) */
void hello_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void hello_offer(lw_node *n, const struct sockaddr *dst, socklen_t dl);
void hello_request(lw_node *n, lw_peer *p);
void punch_input(lw_node *n, const uint8_t *pkt, size_t len, const struct sockaddr *src, socklen_t sl);
void node_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len);
void node_timers(lw_node *n);
int  node_cmd(lw_node *n, const char *line, char *out, size_t cap);
int  node_status_json(lw_node *n, char *o, size_t cap);
int  node_init(lw_node *n, char *err, size_t errn);
void fmt_number(char out[16], const char *num9);
int  parse_number(const char *in, char out[NUMLEN + 1]);	/* 0 = ok, check digit verified */
int  damm_ok(const char *digits, int n);
void mint_number(char out[NUMLEN + 1]);
void phrase_of(char *out, size_t cap, const uint8_t a[32], const uint8_t b[32]);

/* board.c: the switchboard worker */
int  board_open(lw_node *n, char *err, size_t errn);
void board_msg(lw_node *n, lw_peer *p, uint8_t msg, const uint8_t *pl, size_t len);
void board_timers(lw_node *n);
int  board_cmd(lw_node *n, const char *line, char *out, size_t cap);
int  board_status_json(lw_node *n, char *o, size_t cap);

#endif
