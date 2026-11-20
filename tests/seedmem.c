/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * A seed handed in from memory (docs/adr/0091): the node's identity is the one that seed gives, no seed file is read or made, the node's own copy of the configured seed is wiped, and the
 * 33 words round-trip. This is what a phone's engine does with the seed its keystore holds.
 */
#include "../engine/all.c"
#include <sys/stat.h>

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[200], path[300], words[600];
	lw_node *n;
	lw_id want;
	uint8_t seed[32], back[32], zero[32] = {0};
	struct stat st;
	int i;

	if (argc < 2)
		return 2;
	for (i = 0; i < 32; i++)
		seed[i] = (uint8_t)(i * 7 + 3);
	memset(&want, 0, sizeof want);
	memcpy(want.seed, seed, 32);
	lw_id_derive(&want);
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", argv[1]);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", argv[1]);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", argv[1]);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.maxpeers = 4;
	c.no_nat_pmp = 1;
	memcpy(c.seed_mem, seed, 32);
	c.has_seed_mem = 1;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		return 2;
	}
	check(!memcmp(n->id.key_id, want.key_id, 32), "the node's identity is the one the seed gives");
	snprintf(path, sizeof path, "%s/seed", argv[1]);
	check(stat(path, &st) != 0, "no seed file was made");
	check(!memcmp(n->cfg.seed_mem, zero, 32), "the node's copy of the configured seed is wiped");
	lw_seed_words(words, sizeof words, seed);
	check(!lw_seed_parse(words, back) && !memcmp(back, seed, 32), "the 33 words give the same seed back");
	words[0] = (char)(words[0] == 'a' ? 'b' : 'a');
	check(lw_seed_parse(words, back) != 0, "a slip in the words is caught");
	printf(bad ? "\nFAILED\n" : "\nall seedmem checks passed\n");
	return bad;
}
