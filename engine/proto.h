/* SPDX-License-Identifier: GPL-3.0-or-later
 * Message ids and small codecs shared by the node and the switchboard. */
#ifndef LW_PROTO_H
#define LW_PROTO_H

#include <stdint.h>
#include <string.h>

#define T_COOKIE 0x03
#define T_RELAY 0x05
#define T_PUNCH 0x06
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
#define M_REJECTED 0x28		/* call_id(8) reason(1) */
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
#define MAXCANDS 8
#define NUMLEN 9
#define ADMIT_LEN 145		/* line_id 8, key_id 32, role 1, admitted_by 32, time 8, sig 64 */
#define LINE_MAX 16
#define MEMBER_LEN 74		/* number 9, key_id 32, ed_pub 32, role 1 */

typedef struct lw_cand {
	uint8_t  ip[4];
	uint16_t port;		/* host order */
	uint8_t  type;
} lw_cand;

static inline void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline unsigned get16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

static inline int cand_put(uint8_t *p, const lw_cand *c)
{
	memcpy(p, c->ip, 4);
	put16(p + 4, c->port);
	p[6] = c->type;
	return 7;
}

static inline void cand_get(const uint8_t *p, lw_cand *c)
{
	memcpy(c->ip, p, 4);
	c->port = (uint16_t)get16(p + 4);
	c->type = p[6];
}

#endif
