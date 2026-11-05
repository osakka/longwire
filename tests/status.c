/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The status of a node that has everything at its limit (docs/adr/0063): 32 peers, 32 calls and a line of 16 members. The writers of JSON never write past their buffer
 * (a would-be length from snprintf was once used as an offset: a stack overrun a member could provoke by adding members), the worst case fits the control socket's
 * buffer, and a buffer that is too small gets a cut answer, not a crash. Built with TCC's bounds checker, which stops any write past the end.
 */
#include "../engine/all.c"

static int bad;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

/* Brackets and braces balance, outside strings: the answer is whole JSON. */
static int balanced(const char *s, int len)
{
	int depth = 0, str = 0, i;

	for (i = 0; i < len; i++) {
		if (s[i] == '\\' && str) {
			i++;
			continue;
		}
		if (s[i] == '"')
			str = !str;
		else if (!str && (s[i] == '{' || s[i] == '['))
			depth++;
		else if (!str && (s[i] == '}' || s[i] == ']'))
			depth--;
	}
	return depth == 0 && !str;
}

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[200];
	lw_node *n;
	static char buf[32768 + 128];
	int i, len;

	if (argc < 2)
		return 2;
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", argv[1]);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", argv[1]);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", argv[1]);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.maxpeers = 32;
	c.no_nat_pmp = 1;
	if (!(n = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		return 2;
	}
	n->now = 1000.0;
	snprintf(n->number, sizeof n->number, "123456789");
	n->registered = 1;
	n->cfg.listen[0] = 0;
	snprintf(n->cfg.name, sizeof n->cfg.name, "x");
	for (i = 0; i < n->maxpeers; i++) {
		lw_peer *p = &n->peers[i];

		p->used = 1;
		lw_rand(p->key_id, 32);
		snprintf(p->number, sizeof p->number, "%09d", 100000000 + i);
		p->cur.valid = 1;
		p->cur.born = 900.0;
		p->sas.mode = 1;
		p->sas.state = 1;
		snprintf(p->sas.words, sizeof p->sas.words, "dune glacier beacon winter topaz mustard oyster tango");
		p->exit_seen = 999.0;
		p->ep.ss_family = AF_INET;
	}
	n->line.have = 1;
	n->line.nm = 16;
	for (i = 0; i < 16; i++) {
		lw_rand(n->line.m[i].key_id, 32);
		snprintf(n->line.m[i].number, sizeof n->line.m[i].number, "%09d", 200000000 + i);
	}
	for (i = 0; i < n->ncalls && i < 32; i++) {
		n->calls[i].state = CALL_UP;
		n->calls[i].peer = i;
		snprintf(n->calls[i].number, sizeof n->calls[i].number, "%09d", 100000000 + i);
	}

	memset(buf, 0x5a, sizeof buf);
	len = node_status_json(n, buf, 32768);
	printf("     (the worst status is %d bytes of 32768; its end: %.60s)\n", len, buf + (len > 70 ? len - 70 : 0));
	check(len > 0 && len < 32768 - 1024, "the worst status fits the control socket's buffer with room to spare");
	check(balanced(buf, len) && buf[len - 1] == '\n', "and is whole JSON");
	{
		int ok = 1;

		for (i = 32768; i < (int)sizeof buf; i++)
			ok &= buf[i] == 0x5a;
		check(ok, "and nothing was written past the buffer");
	}
	check(strstr(buf, "\"road\"") && strstr(buf, "\"sandbox\"") && strstr(buf, "\"exit\"") && strstr(buf, "\"line\"") && strstr(buf, "\"calls\""),
	      "with its tail: sandbox, road, exit and line");

	for (int cap = 64; cap <= 8192; cap = cap * 2 + 17) {		/* a buffer that is too small: a cut answer, never a write past it, and a length that is inside it */
		memset(buf, 0x5a, sizeof buf);
		len = node_status_json(n, buf, (size_t)cap);
		int ok = len >= 0 && len < cap;

		for (i = cap; i < (int)sizeof buf; i++)
			ok &= buf[i] == 0x5a;
		if (!ok) {
			check(0, "a buffer too small for the status gets a cut answer, and nothing is written past it");
			break;
		}
		if (cap > 8000)
			check(1, "a buffer too small for the status gets a cut answer, and nothing is written past it");
	}
	printf(bad ? "FAILED\n" : "all status checks passed\n");
	return bad;
}
