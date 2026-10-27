/* Reads hex lines, prints Monocypher's answers (built by TCC, as shipped). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"

static size_t unhex(const char *s, uint8_t *o)
{
	size_t n, i;
	if (s[0] == '-') return 0;
	n = strlen(s) / 2;
	for (i = 0; i < n; i++) { unsigned v; sscanf(s + 2 * i, "%2x", &v); o[i] = (uint8_t)v; }
	return n;
}
static void hex(const uint8_t *b, size_t n) { size_t i; for (i = 0; i < n; i++) printf("%02x", b[i]); printf("\n"); }

int main(void)
{
	static char l[1 << 16], a[1 << 15], b[1 << 15], c[1 << 15], d[1 << 15];
	static uint8_t A[1 << 14], B[1 << 14], C[1 << 14], D[1 << 14], O[1 << 15];
	char op;

	while (fgets(l, sizeof l, stdin)) {
		a[0] = b[0] = c[0] = d[0] = 0;
		if (sscanf(l, "%c %s %s %s %s", &op, a, b, c, d) < 2) continue;
		size_t na = unhex(a, A), nb = unhex(b, B), nc = unhex(c, C), nd = unhex(d, D);
		if (op == 'X') { crypto_x25519(O, A, B); hex(O, 32); }
		else if (op == 'P') { crypto_x25519_public_key(O, A); hex(O, 32); }
		else if (op == 'H') { crypto_blake2b_keyed(O, 32, A, na, B, nb); hex(O, 32); }
		else if (op == 'h') { crypto_blake2b_keyed(O, 16, A, na, B, nb); hex(O, 16); }
		else if (op == 'A') { crypto_aead_ctx x; crypto_aead_init_ietf(&x, A, B); crypto_aead_write(&x, O, O + nd, C, nc, D, nd); hex(O, nd + 16); }
		else if (op == 'E') { uint8_t sk[64], pk[32], sg[64]; crypto_ed25519_key_pair(sk, pk, A); crypto_ed25519_sign(sg, sk, B, nb); hex(pk, 32); hex(sg, 64); (void)nc;(void)nd; }
	}
	return 0;
}
