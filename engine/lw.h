/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * longwire engine API. Plain C99; never includes hibr.h, so the same code
 * can later sit behind JNI. The hibr module (module/longwire.c) is the only
 * caller today.
 */
#ifndef LW_H
#define LW_H

#include <stddef.h>
#include <stdint.h>

#define LW_MLKEM_PK 1184
#define LW_MLKEM_SK 2400
#define LW_MLKEM_CT 1088
#define LW_BUNDLE   1249		/* 0x01 | x25519 32 | mlkem 1184 | ed25519 32 */
#define LW_MAXPEERS 16

typedef struct lw_id {
	uint8_t seed[32];
	uint8_t x_sk[32];
	uint8_t kem_sk[LW_MLKEM_SK];
	uint8_t ed_sk[64];
	uint8_t bundle[LW_BUNDLE];
	uint8_t key_id[32];
} lw_id;

typedef struct lw_peercfg {
	char bundle_path[256];
	char endpoint[128];		/* "host:port", empty = wait for them */
} lw_peercfg;

typedef struct lw_cfg {
	char seed_path[256];
	char ctl_path[256];
	char log_path[256];
	char port_kind[16];		/* "splice" | "jack" | "sock" | "none" */
	char port_arg[256];		/* NIC name | tap name | "mypath:peerpath" */
	char listen[64];		/* "port" or "addr:port" */
	int  rekey_after;		/* seconds, default 120 */
	int  sb_rekey;			/* seconds, default 600: the rekey interval of switchboard sessions */
	int  keepalive;			/* seconds, default 25 */
	int  mtu;			/* port MTU, default 1400 */
	int  npeers;
	lw_peercfg peers[LW_MAXPEERS];
	/* M1: calls and the switchboard */
	int  role;			/* 0 node, 1 switchboard */
	char switchboard[256];		/* "host:port/KEYID" (64 hex or lw1...), nodes only */
	char state_dir[256];		/* pins ("known"), switchboard registry */
	int  cand_loopback;		/* offer 127.x as a candidate: tests and same-host use */
	int  policy_script;		/* switchboard: wait for the script's verdicts */
	int  maxpeers;			/* default 16 (switchboard: 1024) */
	char token[64];			/* presented when registering */
	char registry[200];		/* switchboard: HOST:PORT/KEY of the registry of switchboard codes, or "none" */
	char public_switchboard[100];	/* registry: the key (lw1... or hex) of the switchboard that is given the code 1 */
	long release_after;		/* seconds a number may go unheard before it is released (default one year), and held back after */
	int  issue_codes;		/* this switchboard is a registry: it assigns codes to switchboards */
	char name[40];			/* the name this node announces to those it calls; none if empty */
	int  listed;			/* list that name in the switchboard's directory */
	int  no_caller_id, no_directory;	/* switchboard: refuse to carry names / answer lookups */
	int  relay;			/* relay for line members: 0 only when publicly reachable (auto), 1 always, 2 never */
	int  direct_retry;		/* seconds between tries for a direct path to a relayed member (default 60) */
	char site_key_path[256];	/* splice: shared secret of the boxes on one LAN segment (hex), enables site groups */
} lw_cfg;

typedef struct lw_node lw_node;

/* base32 of a key id: the "full number" (lw1...), docs/CRYPTO.md §2 */
void lw_address(char out[60], const uint8_t key_id[32]);
void lw_nth_code(unsigned long k, char out[10]);			/* the k-th assigned switchboard code of the plan (ADR 0024) */
int  lw_address_parse(const char *s, uint8_t key_id[32]);	/* lw1... or 64 hex; 0 = ok */

/* identity */
int  lw_id_load(lw_id *id, const char *seed_path, int create);
void lw_id_derive(lw_id *id);
void lw_seed_words(char *out, size_t cap, const uint8_t seed[32]);	/* 33 words: 32 and a checksum */
int  lw_seed_parse(const char *text, uint8_t seed[32]);		/* 0 ok, -1 bad words, -2 bad checksum */
void lw_hex(char *out, const uint8_t *in, size_t n);
/* A node: open does everything that needs privilege (sockets, port, ctl
 * socket); serve is the event loop and runs until stopped. */
lw_node *lw_node_open(const lw_cfg *cfg, char *err, size_t errn);
int  lw_node_serve(lw_node *n);
void lw_node_release(lw_node *n);	/* close fds in a process that won't serve */
/* Clear every capability set (effective, permitted, inheritable, ambient). */
int  lw_caps_drop(void);

#endif
