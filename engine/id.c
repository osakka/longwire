/* SPDX-License-Identifier: GPL-3.0-or-later
 * Identity: one 32-byte seed derives every key (docs/CRYPTO.md §2). Also the
 * small shared utilities: clock, randomness, hashing. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>
#include "int.h"
#include "../vendor/monocypher/monocypher-ed25519.h"
#include "../vendor/mlkem768/kem.h"

double lw_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

void lw_rand(void *p, size_t n)
{
	uint8_t *b = p;

	while (n) {
		ssize_t r = getrandom(b, n, 0);

		if (r < 0) {
			if (errno == EINTR)
				continue;
			_exit(111);	/* no entropy: never fall back to a weak source */
		}
		b += r;
		n -= (size_t)r;
	}
}

void lw_h(uint8_t out[32], const void *a, size_t an)
{
	crypto_blake2b(out, 32, a, an);
}

void lw_h2(uint8_t out[32], const void *a, size_t an, const void *b, size_t bn)
{
	crypto_blake2b_ctx c;

	crypto_blake2b_init(&c, 32);
	crypto_blake2b_update(&c, a, an);
	crypto_blake2b_update(&c, b, bn);
	crypto_blake2b_final(&c, out);
}

void lw_hex(char *out, const uint8_t *in, size_t n)
{
	static const char x[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < n; i++) {
		out[2 * i] = x[in[i] >> 4];
		out[2 * i + 1] = x[in[i] & 15];
	}
	out[2 * n] = 0;
}

static void kdf(uint8_t out[32], const uint8_t seed[32], const char *label)
{
	crypto_blake2b_keyed(out, 32, seed, 32, (const uint8_t *)label, strlen(label));
}

void lw_id_derive(lw_id *id)
{
	uint8_t sign_seed[32], coins[64], x_pk[32], pk[LW_MLKEM_PK];
	static const char l1[] = "longwire x25519 v1", l2[] = "longwire ed25519 v1",
			  l3[] = "longwire mlkem768 v1";

	kdf(id->x_sk, id->seed, l1);
	crypto_x25519_public_key(x_pk, id->x_sk);
	kdf(sign_seed, id->seed, l2);
	crypto_blake2b_keyed(coins, 64, id->seed, 32, (const uint8_t *)l3, strlen(l3));
	PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair_derand(pk, id->kem_sk, coins);
	crypto_ed25519_key_pair(id->ed_sk, id->bundle + 1 + 32 + LW_MLKEM_PK, sign_seed);
	id->bundle[0] = 0x01;
	memcpy(id->bundle + 1, x_pk, 32);
	memcpy(id->bundle + 1 + 32, pk, LW_MLKEM_PK);
	lw_h(id->key_id, id->bundle, LW_BUNDLE);
	crypto_wipe(sign_seed, sizeof sign_seed);
	crypto_wipe(coins, sizeof coins);
}

/* The seed file is the identity: mode 0600, created if asked to. */
int lw_id_load(lw_id *id, const char *path, int create)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t r;

	if (fd < 0) {
		if (errno != ENOENT || !create)
			return -1;
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (fd < 0)
			return -1;
		lw_rand(id->seed, 32);
		if (write(fd, id->seed, 32) != 32) {
			close(fd);
			unlink(path);
			return -1;
		}
		close(fd);
	} else {
		r = read(fd, id->seed, 32);
		close(fd);
		if (r != 32)
			return -1;
	}
	lw_id_derive(id);
	return 0;
}

/* KDF1/KDF2 as in docs/CRYPTO.md §3: BLAKE2b keyed with the chaining key. */
void lw_kdf1(uint8_t ck[32], const void *x, size_t xn)
{
	uint8_t t0[32], one = 1;
	static const uint8_t nil[1];

	crypto_blake2b_keyed(t0, 32, ck, 32, x ? x : nil, xn);
	crypto_blake2b_keyed(ck, 32, t0, 32, &one, 1);
	crypto_wipe(t0, 32);
}

void lw_kdf2(uint8_t ck[32], const void *x, size_t xn, uint8_t k[32])
{
	uint8_t t0[32], t1[32], buf[33];
	static const uint8_t nil[1];

	crypto_blake2b_keyed(t0, 32, ck, 32, x ? x : nil, xn);
	buf[0] = 1;
	crypto_blake2b_keyed(t1, 32, t0, 32, buf, 1);
	memcpy(buf, t1, 32);
	buf[32] = 2;
	crypto_blake2b_keyed(k, 32, t0, 32, buf, 33);
	memcpy(ck, t1, 32);
	crypto_wipe(t0, 32);
	crypto_wipe(t1, 32);
	crypto_wipe(buf, 33);
}

void lw_seal0(uint8_t *out, const uint8_t k[32], const void *pt, size_t n, const uint8_t ad[32])
{
	crypto_aead_ctx c;
	static const uint8_t nonce[12];

	crypto_aead_init_ietf(&c, k, nonce);
	crypto_aead_write(&c, out, out + n, ad, 32, pt, n);
}

int lw_open0(uint8_t *pt, const uint8_t k[32], const uint8_t *ct, size_t ctn, const uint8_t ad[32])
{
	crypto_aead_ctx c;
	static const uint8_t nonce[12];

	if (ctn < 16)
		return -1;
	crypto_aead_init_ietf(&c, k, nonce);
	return crypto_aead_read(&c, pt, ct + ctn - 16, ad, 32, ct, ctn - 16);
}

/* The "full number": lw1 + base32(key_id), lowercase, no padding (55 chars). */
static const char B32[] = "abcdefghijklmnopqrstuvwxyz234567";

void lw_address(char out[60], const uint8_t key_id[32])
{
	int i, bits = 0, acc = 0, o = 3;

	memcpy(out, "lw1", 3);
	for (i = 0; i < 32; i++) {
		acc = acc << 8 | key_id[i];
		bits += 8;
		while (bits >= 5) {
			out[o++] = B32[(acc >> (bits - 5)) & 31];
			bits -= 5;
		}
	}
	if (bits)
		out[o++] = B32[(acc << (5 - bits)) & 31];
	out[o] = 0;
}

int lw_address_parse(const char *s, uint8_t key_id[32])
{
	size_t i, l = strlen(s);

	if (l == 64) {
		for (i = 0; i < 64; i++)
			if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
				return -1;
		for (i = 0; i < 32; i++) {
			int a = s[2 * i] <= '9' ? s[2 * i] - '0' : s[2 * i] - 'a' + 10;
			int b = s[2 * i + 1] <= '9' ? s[2 * i + 1] - '0' : s[2 * i + 1] - 'a' + 10;

			key_id[i] = (uint8_t)(a << 4 | b);
		}
		return 0;
	}
	if (l == 55 && !strncmp(s, "lw1", 3)) {
		int bits = 0, acc = 0, o = 0;

		for (i = 3; i < l; i++) {
			const char *c = strchr(B32, s[i]);

			if (!c || !s[i])
				return -1;
			acc = acc << 5 | (int)(c - B32);
			bits += 5;
			if (bits >= 8) {
				if (o >= 32)
					return -1;
				key_id[o++] = (uint8_t)(acc >> (bits - 8));
				bits -= 8;
			}
		}
		return o == 32 ? 0 : -1;
	}
	return -1;
}

/* Ed25519 with the identity's signing key (the bundle carries the public half at offset 1217). */
void lw_sign(const lw_id *id, const uint8_t *m, size_t n, uint8_t sig[64])
{
	crypto_ed25519_sign(sig, id->ed_sk, m, n);
}

int lw_verify(const uint8_t pub[32], const uint8_t *m, size_t n, const uint8_t sig[64])
{
	return crypto_ed25519_check(sig, pub, m, n) == 0;
}
