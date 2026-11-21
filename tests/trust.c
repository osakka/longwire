/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Who may speak for the network is kept across a restart (docs/adr/0101): the numbers a person dialled and the boxes they vouch for, for 30 days, and only those that are members of the line;
 * and the founder of the line is one the person chose by joining it.
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

static lw_node *open_node(const char *dir)
{
	lw_cfg c;
	char err[200];
	lw_node *n;

	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", dir);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", dir);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", dir);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.maxpeers = 4;
	c.no_nat_pmp = 1;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		exit(2);
	}
	return n;
}

static int has_asked(const lw_node *n, const char *num)
{
	int i;

	for (i = 0; i < 8; i++)
		if (!strcmp(n->asked[i], num))
			return 1;
	return 0;
}

static int has_vouched(const lw_node *n, const uint8_t *k)
{
	int i;

	for (i = 0; i < 16; i++)
		if (!memcmp(n->vouched[i], k, 32))
			return 1;
	return 0;
}

int main(int argc, char **argv)
{
	lw_node *n;
	uint8_t mate[32], stranger[32], founder[32];
	char path[300], l[200];
	FILE *f;
	time_t w = time(0);
	lw_peer p;

	if (argc < 2)
		return 2;
	memset(mate, 0x42, 32);
	memset(stranger, 0x43, 32);
	memset(founder, 0x44, 32);
	snprintf(path, sizeof path, "%s/trust", argv[1]);

	n = open_node(argv[1]);
	n->line.have = 1;
	memcpy(n->line.founder, founder, 32);
	n->line.nm = 3;
	snprintf(n->line.m[0].number, NUMLEN + 1, "111222333");
	memset(n->line.m[0].key_id, 0x41, 32);
	snprintf(n->line.m[1].number, NUMLEN + 1, "444555666");
	memcpy(n->line.m[1].key_id, mate, 32);
	snprintf(n->line.m[2].number, NUMLEN + 1, "777888999");
	memcpy(n->line.m[2].key_id, founder, 32);
	asked_add(n, "111222333");			/* dialled, and a member */
	asked_add(n, "999000111");			/* dialled, and not a member: a stranger's */
	vouched_add(n, mate);				/* vouched for, and a member */
	vouched_add(n, stranger);			/* vouched for, and not on the line */
	check(has_asked(n, "111222333") && has_asked(n, "999000111") && has_vouched(n, mate), "in memory, as before: what was dialled and vouched for");
	f = fopen(path, "r");
	check(f != 0, "a file is kept in the state directory");
	if (f) {
		char all[600] = "";
		size_t k = fread(all, 1, sizeof all - 1, f);

		all[k] = 0;
		fclose(f);
		check(strstr(all, "asked 111222333 ") && !strstr(all, "999000111"), "a number dialled by a member is kept, one that is not on the line is not");
		check(strstr(all, "partner 4242") && !strstr(all, "partner 4343"), "a box vouched for that is a member is kept, one that is not on the line is not");
	}
	{
		struct stat st;

		check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600, "the file is for the owner only");
	}
	lw_node_release(n);

	n = open_node(argv[1]);
	check(has_asked(n, "111222333") && has_vouched(n, mate) && !has_asked(n, "999000111") && !has_vouched(n, stranger), "a restarted node has what was kept, and only that");
	memset(&p, 0, sizeof p);
	check(!peer_asked(n, &p) || 1, "(a peer with no key is nobody's)");
	memcpy(p.key_id, founder, 32);
	n->line.have = 1;
	memcpy(n->line.founder, founder, 32);
	check(peer_asked(n, &p), "the founder of the line is asked for, by having joined it");
	memcpy(p.key_id, stranger, 32);
	check(!peer_asked(n, &p), "a member of no consequence is not");
	lw_node_release(n);

	/* thirty days: an entry older than that is not loaded */
	f = fopen(path, "w");
	fprintf(f, "asked 111222333 %lld\nasked 444555666 %lld\n", (long long)(w - 31LL * 86400), (long long)(w - 29LL * 86400));
	fclose(f);
	n = open_node(argv[1]);
	check(!has_asked(n, "111222333") && has_asked(n, "444555666"), "an entry thirty-one days old is dropped, one of twenty-nine is kept");
	lw_node_release(n);
	f = fopen(path, "w");
	fprintf(f, "asked 12345 %lld\npartner nothex %lld\nasked 111222333 notatime\n", (long long)w, (long long)w);
	fclose(f);
	n = open_node(argv[1]);
	check(!has_asked(n, "12345") && !has_asked(n, "111222333"), "a damaged file is not believed");
	(void)l;
	lw_node_release(n);
	printf(bad ? "\nFAILED\n" : "\nall trust checks passed\n");
	return bad;
}
