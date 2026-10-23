/* SPDX-License-Identifier: GPL-3.0-or-later
 * Site groups: two or more splice boxes on one LAN segment. Each announces
 * itself with a probe frame (EtherType 0x88B5, never forwarded over the line,
 * authenticated by a secret the boxes share). The one with the lowest key id
 * forwards; the others stay connected but quiet, and take over within a few
 * seconds when it goes silent (docs/ARCHITECTURE.md §4). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "int.h"

#define PROBE_LEN 75			/* 14 header + "LWSP" + version + key id 32 + wall seconds 8 + mac 16 */
#define PROBE_EVERY 1.0
#define SILENCE 3.5			/* a member unheard this long is gone */
#define LISTEN 3.0			/* a new box listens this long before it forwards anything */

void site_init(lw_node *n)
{
	char buf[80];
	FILE *f;
	size_t r;
	int i;

	if (!n->splice || !n->cfg.site_key_path[0])
		return;
	if (!(f = fopen(n->cfg.site_key_path, "r")))
		return;
	r = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[r] = 0;
	if (r < 64)
		return;
	for (i = 0; i < 32; i++) {
		unsigned v;

		if (sscanf(buf + 2 * i, "%2x", &v) != 1)
			return;
		n->site_key[i] = (uint8_t)v;
	}
	n->have_site_key = 1;
	n->standby = 1;			/* listen first: forwarding while another box does is a loop */
	n->site_since = n->now;
}

static void probe_mac(lw_node *n, const uint8_t *p, uint8_t out[16])
{
	crypto_blake2b_keyed(out, 16, n->site_key, 32, p, PROBE_LEN - 16);
}

int site_input(lw_node *n, const uint8_t *f, size_t len)
{
	uint8_t mac[16];
	uint64_t t = 0;
	int i, slot = -1, k, sync;
	size_t plen;

	if (len < 14 || get16(f + 12) != 0x88B5)
		return 0;
	sync = len >= 14 + 6 + 32 + 8 + 16 && !memcmp(f + 14, "LWSY\x01", 5);
	if (!n->have_site_key || (!sync && (len != PROBE_LEN || memcmp(f + 14, "LWSP\x01", 5))))
		return 1;			/* something we can't use is still not ours to forward */
	plen = sync ? 14 + 6 + 32 + 8 + (size_t)f[19] * 6 + 16 : PROBE_LEN;
	if (len != plen || (sync && f[19] > 64))
		return 1;
	crypto_blake2b_keyed(mac, 16, n->site_key, 32, f, plen - 16);
	if (crypto_verify16(mac, f + plen - 16))
		return 1;
	for (k = 0; k < 8; k++)
		t = t << 8 | f[(sync ? 14 + 6 + 32 : 14 + 5 + 32) + k];
	if ((int64_t)t - (int64_t)time(0) > 30 || (int64_t)time(0) - (int64_t)t > 30)
		return 1;			/* a replay of something old */
	if (!crypto_verify32(sync ? f + 20 : f + 19, n->id.key_id))
		return 1;
	if (sync) {				/* the active box's list of remote hosts */
		if (n->standby) {
			n->site_sync_n = f[19];
			memcpy(n->site_sync, f + 14 + 6 + 32 + 8, (size_t)f[19] * 6);
			n->site_sync_t = n->now;
		}
		return 1;
	}
	for (i = 0; i < SITE_MEMBERS; i++) {
		if (n->site[i].used && !memcmp(n->site[i].key_id, f + 19, 32)) {
			slot = i;
			break;
		}
		if (slot < 0 && (!n->site[i].used || n->now - n->site[i].seen > SILENCE))
			slot = i;
	}
	if (slot >= 0) {
		memcpy(n->site[slot].key_id, f + 19, 32);
		n->site[slot].seen = n->now;
		n->site[slot].used = 1;
	}
	return 1;
}

int site_members(const lw_node *n)
{
	int i, c = 0;

	for (i = 0; i < SITE_MEMBERS; i++)
		if (n->site[i].used && n->now - n->site[i].seen <= SILENCE)
			c++;
	return c;
}

const char *site_role(const lw_node *n)
{
	if (!n->have_site_key)
		return "alone";
	return n->standby ? "standby" : "active";
}

void site_timers(lw_node *n)
{
	int i, lower = 0, was = n->standby;
	uint8_t p[PROBE_LEN];
	time_t t = time(0);

	if (!n->splice || !n->have_site_key)
		return;
	if (n->now - n->site_probe_t >= PROBE_EVERY) {
		memset(p, 0xff, 6);
		memcpy(p + 6, n->nic_mac, 6);
		put16(p + 12, 0x88B5);
		memcpy(p + 14, "LWSP\x01", 5);
		memcpy(p + 19, n->id.key_id, 32);
		for (i = 0; i < 8; i++)
			p[51 + i] = (uint8_t)((uint64_t)t >> (56 - 8 * i));
		probe_mac(n, p, p + PROBE_LEN - 16);
		port_write(n, p, PROBE_LEN);
		n->site_probe_t = n->now;
	}
	for (i = 0; i < SITE_MEMBERS; i++)
		if (n->site[i].used && n->now - n->site[i].seen <= SILENCE && memcmp(n->site[i].key_id, n->id.key_id, 32) < 0)
			lower = 1;
	if (n->now - n->site_since < LISTEN)
		return;
	n->standby = lower;			/* the lowest key id among the live boxes forwards */
	if (!n->standby && n->now - n->site_sync_sent >= 2.0) {		/* share what we know about the remote hosts */
		uint8_t sy[14 + 6 + 32 + 8 + 64 * 6 + 16];
		int cnt = sw_remote_macs(n, (uint8_t (*)[6])(sy + 14 + 6 + 32 + 8), 64);
		size_t sl = 14 + 6 + 32 + 8 + (size_t)cnt * 6 + 16;

		if (cnt) {
			memset(sy, 0xff, 6);
			memcpy(sy + 6, n->nic_mac, 6);
			put16(sy + 12, 0x88B5);
			memcpy(sy + 14, "LWSY\x01", 5);
			sy[19] = (uint8_t)cnt;
			memcpy(sy + 20, n->id.key_id, 32);
			for (i = 0; i < 8; i++)
				sy[52 + i] = (uint8_t)((uint64_t)t >> (56 - 8 * i));
			crypto_blake2b_keyed(sy + sl - 16, 16, n->site_key, 32, sy, sl - 16);
			port_write(n, sy, sl);
		}
		n->site_sync_sent = n->now;
	}
	if (was && !n->standby) {
		/* Taking over: the LAN's switches still point the remote hosts at the box that went quiet. A frame
		 * from each of them, sent from here, moves them. (Addresses the old box shared with us, then the
		 * hosts we have seen ARP.) */
		if (n->now - n->site_sync_t < 15.0) {
			int j;

			for (j = 0; j < n->site_sync_n; j++) {
				uint8_t r[14 + 4];

				memset(r, 0xff, 6);
				memcpy(r + 6, n->site_sync[j], 6);
				put16(r + 12, 0x88B5);
				memcpy(r + 14, "LWRL", 4);
				port_write(n, r, sizeof r);
			}
		}
		sw_gratuitous(n);
		ev_emit(n, "{\"event\":\"site\",\"role\":\"active\",\"members\":%d}", site_members(n) + 1);
	} else if (!was && n->standby)
		ev_emit(n, "{\"event\":\"site\",\"role\":\"standby\",\"members\":%d}", site_members(n) + 1);
}
