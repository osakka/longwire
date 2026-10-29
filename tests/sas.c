/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The committed safety phrase (docs/adr/0040), one end at a time: what the callee does with a commit, a reveal that is right, a reveal that is not,
 * a commit made for another caller, silence, and what a node in the middle of two sessions ends up with. The engine is included whole; the peers
 * are made by hand with a loopback socket for the sends.
 */
#include "../engine/all.c"

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

static lw_node *mknode(const char *dir, int require)
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
	c.maxpeers = 8;
	c.no_nat_pmp = 1;
	c.require_sas = require;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		exit(2);
	}
	n->now = 1000.0;
	return n;
}

static lw_peer *mkpeer(lw_node *n, int slot, int sink, int caller)
{
	lw_peer *p = &n->peers[slot];
	struct sockaddr_in a;
	socklen_t al = sizeof a;

	memset(p, 0, sizeof *p);
	p->used = 1;
	lw_rand(p->key_id, 32);
	memcpy(p->number, "123456789", NUMLEN + 1);
	p->cur.valid = 1;
	p->cur.born = n->now;
	lw_rand(p->cur.k_send, 32);
	lw_rand(p->cur.k_recv, 32);
	getsockname(sink, (struct sockaddr *)&a, &al);
	memcpy(&p->ep, &a, sizeof a);
	p->eplen = sizeof a;
	p->sas.mode = 1;
	p->sas.caller = caller;
	p->sas.born = n->now;
	sas_bind_of(p->sas.bind, &p->cur);
	return p;
}

int main(int argc, char **argv)
{
	lw_node *n, *n2;
	lw_peer *p;
	uint8_t na[16], nb[16], cm[32], m[40], other[32];
	char dir[64], dir2[64], w1[120], w2[120];
	int sink = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a;
	lw_sas s1, s2;
	int i, differ;

	if (argc < 3)
		return 2;
	snprintf(dir, sizeof dir, "%s", argv[1]);
	snprintf(dir2, sizeof dir2, "%s", argv[2]);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bind(sink, (struct sockaddr *)&a, sizeof a);
	n = mknode(dir, 0);

	/* the callee: a commit, then the right reveal */
	p = mkpeer(n, 0, sink, 0);
	lw_rand(na, 16);
	sas_commit(cm, na, p->key_id);
	m[0] = 0; memcpy(m + 1, cm, 32);
	sas_input(n, p, m, 33);
	check(p->sas.have_c && p->sas.have_nb && p->sas.state == 0, "the callee keeps a commit and picks its own nonce");
	memcpy(nb, p->sas.nb, 16);
	sas_input(n, p, m, 33);
	check(!memcmp(nb, p->sas.nb, 16), "and says the same nonce again when the commit is repeated (the caller repeats until it hears)");
	m[0] = 2; memcpy(m + 1, na, 16);
	sas_input(n, p, m, 17);
	check(p->sas.state == 1, "the right reveal gives the phrase");
	{
		lw_sas ref;

		memset(&ref, 0, sizeof ref);
		memcpy(ref.na, na, 16);
		memcpy(ref.nb, nb, 16);
		memcpy(ref.bind, p->sas.bind, 32);
		sas_words(&ref, p->key_id, n->id.key_id);
		check(!strcmp(ref.words, p->sas.words) && ref.state == 1, "which is what both nonces, both key ids and the session give");
		for (i = 0, differ = 0; p->sas.words[i]; i++)
			differ += p->sas.words[i] == ' ';
		check(differ == 7, "eight words");
	}
	sas_input(n, p, m, 17);
	check(p->sas.state == 1 && p->used, "a repeated reveal changes nothing");

	/* a reveal that is not what was committed to */
	p = mkpeer(n, 1, sink, 0);
	sas_commit(cm, na, p->key_id);
	m[0] = 0; memcpy(m + 1, cm, 32);
	sas_input(n, p, m, 33);
	lw_rand(nb, 16);
	m[0] = 2; memcpy(m + 1, nb, 16);
	sas_input(n, p, m, 17);
	check(!p->used, "a reveal that does not match the commitment ends the call");

	/* a commit made for another caller: the same nonce, but the key id the callee believes is not the one committed to */
	p = mkpeer(n, 2, sink, 0);
	lw_rand(other, 32);
	sas_commit(cm, na, other);
	m[0] = 0; memcpy(m + 1, cm, 32);
	sas_input(n, p, m, 33);
	m[0] = 2; memcpy(m + 1, na, 16);
	sas_input(n, p, m, 17);
	check(!p->used, "a commitment bound to another caller's key does not verify: it cannot be passed on to a different callee");

	/* a message the wrong end has no business with */
	p = mkpeer(n, 3, sink, 1);
	m[0] = 0; memcpy(m + 1, cm, 32);
	sas_input(n, p, m, 33);
	check(!p->sas.have_c, "a caller ignores a commit");
	p = mkpeer(n, 4, sink, 0);
	m[0] = 2; memcpy(m + 1, na, 16);
	sas_input(n, p, m, 17);
	check(p->used && p->sas.state == 0, "a callee that was never given a commit ignores a reveal");
	sas_input(n, p, m, 5);
	sas_input(n, p, m, 0);
	check(p->used, "and short messages are ignored");

	/* silence: the old phrase after five seconds, unless --require-sas */
	p = mkpeer(n, 5, sink, 0);
	n->now = 1004.0;
	sas_timers(n);
	check(p->sas.mode == 1 && p->sas.state == 0, "within five seconds the callee still waits for a commit");
	n->now = 1006.0;
	sas_timers(n);
	check(p->sas.mode == 2 && p->sas.state == 1 && p->sas.words[0], "after five seconds of silence it is the old phrase, said to be old");
	n2 = mknode(dir2, 1);
	p = mkpeer(n2, 0, sink, 0);
	n2->now = 1006.0;
	sas_timers(n2);
	check(!p->used, "with --require-sas, silence ends the call instead");
	p = mkpeer(n2, 1, sink, 0);
	p->sas.auto_ = 1;
	n2->now = 1012.0;
	sas_timers(n2);
	check(p->used && p->sas.mode == 2, "but a mesh call to a fellow member (signed in by the line) is not asked");

	/* a node in the middle: two sessions, two binds, and nonces fixed before the other was known */
	memset(&s1, 0, sizeof s1); memset(&s2, 0, sizeof s2);
	lw_rand(na, 16); lw_rand(nb, 16);
	memcpy(s1.na, na, 16); memcpy(s1.nb, nb, 16); memcpy(s2.na, na, 16); memcpy(s2.nb, nb, 16);
	lw_rand(s1.bind, 32); lw_rand(s2.bind, 32);
	lw_rand(other, 32);
	sas_words(&s1, other, n->id.key_id);
	sas_words(&s2, other, n->id.key_id);
	snprintf(w1, sizeof w1, "%s", s1.words);
	snprintf(w2, sizeof w2, "%s", s2.words);
	check(strcmp(w1, w2) != 0, "the same nonces and keys but another session give another phrase: a node in the middle cannot show both ends the same");

	if (bad)
		printf("FAILED\n");
	else
		printf("all sas checks passed\n");
	return bad;
}
