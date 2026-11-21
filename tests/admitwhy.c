/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Why a ring from a member of the line is not let in by itself (docs/adr/0100): the node says so in the ring event. Each way an admit can fail is made here, and the reason must be the right one.
 */
#include "../engine/all.c"

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
	char err[200];
	lw_node *n;
	uint8_t a[ADMIT_LEN], kid[32], other[32], signer[32];

	if (argc < 2)
		return 2;
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", argv[1]);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", argv[1]);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", argv[1]);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.maxpeers = 4;
	c.no_nat_pmp = 1;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		return 2;
	}
	memset(kid, 0x11, 32);
	memset(other, 0x22, 32);
	memset(signer, 0x33, 32);
	/* a line of this node, the caller (kid) and a signer (signer) on its record, the founder someone else */
	n->line.have = 1;
	n->line.managed = 0;
	memset(n->line.id, 0x44, sizeof n->line.id);
	memset(n->line.founder, 0x55, 32);
	n->line.nm = 2;
	memcpy(n->line.m[0].key_id, kid, 32);
	memcpy(n->line.m[1].key_id, signer, 32);
	memset(n->line.m[1].ed_pub, 0x66, 32);			/* a key that did not sign this admit */

	check(!strcmp(line_why(n, 0, kid), "no admit came with the call"), "a member that sends no admit: the reason says no admit came");
	check(!strcmp(line_why(n, 0, other), ""), "a caller that is not on the line has nothing explained (a stranger)");
	memset(a, 0, sizeof a);
	memcpy(a, other, 32);
	check(!strcmp(admit_why(n, a, kid), "the admit is for another node"), "an admit made for another node");
	memcpy(a, kid, 32);
	memcpy(a + ADM_BY, other, 32);
	put64(a + ADM_ISSUED, 1000);
	put64(a + ADM_EXPIRES, 1000 + ADMIT_MAX_LIFE + 1);
	check(!strcmp(admit_why(n, a, kid), "the admit is out of time (its lifetime, or this machine's clock)"), "an admit that lasts longer than an admit may");
	put64(a + ADM_EXPIRES, 1000 + ADMIT_MAX_LIFE);
	check(!strcmp(admit_why(n, a, kid), "the member that signed the admit is not on this node's record of the line"), "signed by someone who is not on the record");
	memcpy(a + ADM_BY, kid, 32);
	check(!strcmp(admit_why(n, a, kid), "the member signed its own admit and is not this node's recorded founder"), "a self-admit by one that is not the recorded founder");
	memcpy(a + ADM_BY, signer, 32);
	check(!strcmp(admit_why(n, a, kid), "the signature on the admit does not verify against the signer's key on record"), "signed by a member on the record, with a signature that does not verify");
	check(line_why(n, a, kid)[0] != 0, "and a member that is on the line has it said in the ring event");
	printf(bad ? "\nFAILED\n" : "\nall admitwhy checks passed\n");
	return bad;
}
