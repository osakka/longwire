/* SPDX-License-Identifier: GPL-3.0-or-later
 * A test tool, not shipped: sign an operator's admit with any dates, which the real tool will not do (it makes admits that last days). Used by tests/expiry.sh to make
 * an admit that ends in seconds.   mkadmit OPERATORSEED LINEID(16 hex) KEYID(hex or lw1...) ISSUED EXPIRES   (seconds since 1970; a leading + is seconds from now) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../engine/all.c"

static uint64_t when(const char *s)
{
	return *s == '+' ? (uint64_t)time(0) + strtoull(s + 1, 0, 10) : *s == '-' ? (uint64_t)time(0) - strtoull(s + 1, 0, 10) : strtoull(s, 0, 10);
}

int main(int argc, char **argv)
{
	lw_id op;
	uint8_t line[8], kid[32], a[ADMIT_LEN];
	char hex[ADMIT_LEN * 2 + 1];
	unsigned v;
	int i;

	if (argc != 6 || strlen(argv[2]) != 16)
		return 2;
	memset(&op, 0, sizeof op);
	if (lw_id_load(&op, argv[1], 0) || lw_address_parse(argv[3], kid))
		return 2;
	for (i = 0; i < 8; i++) {
		if (sscanf(argv[2] + 2 * i, "%2x", &v) != 1)
			return 2;
		line[i] = (uint8_t)v;
	}
	uint8_t m[8 + ADM_SIGNED];

	memcpy(a, kid, 32);
	a[32] = 0;
	lw_operator_pub(&op, a + ADM_BY);
	put64(a + ADM_ISSUED, when(argv[4]));
	put64(a + ADM_EXPIRES, when(argv[5]));
	memcpy(m, line, 8);
	memcpy(m + 8, a, ADM_SIGNED);
	lw_sign(&op, m, sizeof m, a + ADM_SIGNED);
	lw_hex(hex, a, ADMIT_LEN);
	printf("%s\n", hex);
	return 0;
}
