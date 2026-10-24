/* SPDX-License-Identifier: GPL-3.0-or-later
 * Message ids and small codecs shared by the node and the switchboard. */
#ifndef LW_PROTO_H
#define LW_PROTO_H

#include <stdint.h>
#include <string.h>

#define T_COOKIE 0x03
#define T_RELAY 0x05
#define T_PUNCH 0x06
#define T_ECHO 0x07		/* request: 0x07 nonce(8) padding to 40 bytes; reply: 0x07 nonce(8) ip(4) port(2) */
#define T_HELLO 0x08
#define K_FRAG  0x01		/* a frame too big for one datagram: id(2) part(1) total(2) bytes */
#define K_CTL   0x10

/* CTL messages between peers (inside a session) */
#define M_LINEHELLO 0x01	/* has_line(1) line_id(8) */
#define M_LINE      0x02	/* line_id founder key admit n x (number key_id ed_pub role) */
#define M_MEMBER    0x03	/* number key_id ed_pub role admit */
#define M_EVICT     0x04	/* line_id key_id time evictor sig */
#define M_LINEKEY   0x05	/* line_id key */
#define M_RELAY_REQ 0x06	/* to_key_id(32): please carry my packets to this member */
#define M_RELAY_OK  0x07	/* role(1) peer_key_id(32) route(3) peer_bundle(1249) */
#define M_BYE      0x08		/* leave(1) */
#define M_RELAY_NO  0x0a	/* peer_key_id(32) */
/* node <-> switchboard */
#define M_REGISTER 0x20		/* token_len(1) token */
#define M_NUMBER   0x21		/* number(9) observed(6) */
#define M_HEARTBEAT 0x22	/* ncand(1) cands */
#define M_HBACK    0x23		/* observed(6) */
#define M_DIAL     0x24		/* number(9) ncand cands */
#define M_RING     0x25		/* call_id(8) number(9) bundle ncand cands */
#define M_ANSWER   0x26		/* call_id(8) verdict(1) ncand cands */
#define M_GO       0x27		/* call_id(8) role(1) delay_ms(2) number(9) token(32) bundle ncand cands */
#define M_REJECTED 0x28		/* call_id(8) reason(1) callee number(9) */
#define M_LOOKUP   0x29		/* kind(1: 0 key id, 1 name) query */
#define M_FOUND    0x2a		/* status(1: 0 ok, 1 no directory) kind(1) query_len(1) query n(1) n x (number(9) key_id(32) namelen(1) name) */
#define M_REFUSED  0x2f		/* reason string */

#define V_ACCEPT 0
#define V_REJECT 1
#define V_BUSY   2
#define R_UNKNOWN 1		/* REJECTED reasons */
#define R_OFFLINE 2
#define R_DECLINED 3
#define R_BUSY 4
#define R_POLICY 5
#define R_TIMEOUT 6
#define R_SELF 7

#define CAND_HOST 1
#define CAND_REFL 2
#define CAND_MAPPED 3		/* a port the home router agreed to keep open (NAT-PMP) */
#define CANDS_STRICT 0x80	/* in a candidate list's count byte: the sender is behind a NAT that makes a new mapping per destination */
#define SPRAY_N 256		/* sockets a strict node opens to be found by a probe (docs/PROTOCOL.md section 4) */
#define MAXCANDS 8
#define NUMLEN 9
#define TAG_ADMIT  1		/* optional parts of DIAL, RING and REGISTER: tag(1) len(1) bytes */
#define TAG_NAME   2
#define TAG_LISTED 3
#define TAG_SWITCHBOARD 4	/* REGISTER: the registrant is a switchboard asking the registry for its code */
#define TAG_CODE   5		/* NUMBER: the code the registry assigned to the switchboard, as ASCII digits */
#define LW_NAME_MAX   32
#define CAPS_CALLERID 1		/* NUMBER's flags: what this switchboard offers */
#define CAPS_DIRECTORY 2
#define ADMIT_LEN 145		/* line_id 8, key_id 32, role 1, admitted_by 32, time 8, sig 64 */
#define LINE_MAX 16
#define EVICT_LEN 144		/* line_id 8, key_id 32, time 8, signer 32, sig 64 */
#define MEMBER_LEN 106		/* number 9, key_id 32, ed_pub 32, role 1, home switchboard key id 32 */

typedef struct lw_cand {
	uint8_t  ip[16];	/* 4 bytes used when fam is 4 */
	uint16_t port;		/* host order */
	uint8_t  type;
	uint8_t  fam;		/* 4 or 6 */
} lw_cand;

static inline void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline unsigned get16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static inline void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xffff); }
static inline uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }

/* A candidate on the wire is len(1) type(1) port(2) address(len-3). The length makes every entry skippable, so a
 * reader meets an address family it does not know without losing the entries after it. IPv4 is 7, IPv6 is 19. */
#define CANDWIRE 20		/* the largest entry: a 16-byte address */

static inline int cand_put(uint8_t *p, const lw_cand *c)
{
	int al = c->fam == 6 ? 16 : 4;

	p[0] = (uint8_t)(3 + al);
	p[1] = c->type;
	put16(p + 2, c->port);
	memcpy(p + 4, c->ip, (size_t)al);
	return 4 + al;
}

/* returns the bytes the entry takes, or -1 if it is cut short; *known says whether it was understood */
static inline int cand_get(const uint8_t *p, size_t avail, lw_cand *c, int *known)
{
	size_t l;

	if (avail < 1 || avail < (l = (size_t)p[0] + 1))
		return -1;
	*known = (l == 8 || l == 20) && p[1] >= CAND_HOST && p[1] <= CAND_MAPPED;
	if (*known) {
		memset(c, 0, sizeof *c);
		c->type = p[1];
		c->port = (uint16_t)get16(p + 2);
		c->fam = l == 8 ? 4 : 6;
		memcpy(c->ip, p + 4, l - 4);
	}
	return (int)l;
}

#endif
