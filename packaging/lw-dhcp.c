/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * lw-dhcp: the jack's DHCP client (docs/adr/0102). One implementation for the phone and the desktop: the client of engine/member.c, which the phone's adapter runs, runs here on a jack, instead of
 * whichever of dhclient, dhcpcd or udhcpc the machine has. It asks the line's DHCP server for an address and keeps it: asks again every few seconds until one answers, renews, and says so when the lease is lost.
 *
 * Two processes, as the BSDs' dhclient does it. The one that reads what the network sends is nobody: it has given up every privilege and is closed to the few system calls it needs (the engine's
 * sandbox). The one that holds the privilege is as small as can be: it reads one fixed message from the first, makes it pass the same check the phone's client does (a real unicast address of a
 * LAN, a mask of 8 to 30 bits), and puts the address on the jack. With --table (everything through the line, no gateway named) it also points the default route of the jack's own
 * table at the lease's router, a router on the lease's own network (ADR 0108), and back at the member that shares its internet when the lease is gone; never the machine's own default
 * route, no name servers, nothing else.
 *
 *   lw-dhcp [--hostname NAME] [--pidfile FILE] [--state FILE] [--table N] [--foreground] [--no-drop] JACK
 *
 * --no-drop leaves the reading process as it is: for a test inside a namespace in which nobody has no number. The helper that starts this never passes it.
 */
#define _GNU_SOURCE
#include "../engine/all.c"
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>

/* what is needed of the kernel's headers, spelled out so TCC need not parse them */
struct ll { unsigned short family, proto; int ifindex; unsigned short hatype; unsigned char pkttype, halen, addr[8]; };
struct nlh { uint32_t len; uint16_t type, flags; uint32_t seq, pid; };
struct ifa { uint8_t family, prefixlen, flags, scope; uint32_t index; };
struct rta4 { uint16_t len, type; uint32_t v; };
struct note { uint32_t ip, mask, gw; uint8_t bound, pad[3]; };
struct rtm { uint8_t family, dst_len, src_len, tos, table, protocol, scope, type; uint32_t flags; };

#define AF_PACKET_ 17
#define PACKET_OUTGOING_ 4
#define NLM_F_REQUEST_ 1
#define NLM_F_ACK_ 4
#define NLM_F_REPLACE_ 0x100
#define NLM_F_CREATE_ 0x400
#define RTM_NEWADDR_ 20
#define RTM_DELADDR_ 21
#define RTM_NEWROUTE_ 24

static volatile sig_atomic_t term;
static int pfd_;

static void dhcp_on_term(int s)
{
	(void)s;
	term = 1;
}

static void tx_frame(void *ctx, const uint8_t *f, size_t len)
{
	(void)ctx;
	(void)!send(pfd_, f, len, 0);
}

static int die(const char *what)
{
	fprintf(stderr, "lw-dhcp: %s: %s\n", what, strerror(errno));
	return 1;
}

static int read_mac(const char *jack, uint8_t mac[6])		/* the jack's own hardware address, from the kernel (SIOCGIFHWADDR) */
{
	struct { char name[16]; unsigned short fam; unsigned char data[14]; unsigned char pad[8]; } r;
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0), ok;

	if (fd < 0)
		return -1;
	memset(&r, 0, sizeof r);
	snprintf(r.name, sizeof r.name, "%s", jack);
	ok = ioctl(fd, 0x8927, &r) == 0;
	close(fd);
	if (!ok)
		return -1;
	memcpy(mac, r.data, 6);
	return 0;
}

/* The address on the jack: add (replacing) or remove, by rtnetlink; the kernel makes the network route of an address by itself. 0 if it was done. */
static int nl_addr(int nl, int index, int add, uint32_t ip, uint32_t mask)
{
	struct { struct nlh h; struct ifa a; struct rta4 r[3]; } m;
	uint8_t ack[256];
	ssize_t n;
	int prefix = 0, i;
	uint32_t bc = ip | ~mask;
	static uint32_t seq = 1;

	for (i = 31; i >= 0 && (mask >> i & 1); i--)
		prefix++;
	memset(&m, 0, sizeof m);
	m.h.len = sizeof m;
	m.h.type = add ? RTM_NEWADDR_ : RTM_DELADDR_;
	m.h.flags = NLM_F_REQUEST_ | NLM_F_ACK_ | (add ? (NLM_F_CREATE_ | NLM_F_REPLACE_) : 0);
	m.h.seq = seq++;
	m.a.family = 2;
	m.a.prefixlen = (uint8_t)prefix;
	m.a.index = (uint32_t)index;
	m.r[0].len = 8; m.r[0].type = 2; m.r[0].v = htonl(ip);		/* IFA_LOCAL */
	m.r[1].len = 8; m.r[1].type = 1; m.r[1].v = htonl(ip);		/* IFA_ADDRESS */
	m.r[2].len = 8; m.r[2].type = 4; m.r[2].v = htonl(bc);		/* IFA_BROADCAST */
	if (send(nl, &m, sizeof m, 0) != (ssize_t)sizeof m)
		return -1;
	if ((n = recv(nl, ack, sizeof ack, 0)) < (ssize_t)(sizeof(struct nlh) + 4) || ((struct nlh *)ack)->type != 2)
		return -1;
	return *(int *)(ack + sizeof(struct nlh)) == 0 ? 0 : -1;	/* NLMSG_ERROR with error 0 is the acknowledgement */
}

/* The default route of the jack's own table (ADR 0108): through the lease's router when the line's network has one, and back to the way out through the member that
 * shares its internet (169.254.77.1, which the helper set) when the lease is gone. Replaced, never added beside: one default route in the table. 0 if it was done. */
static int nl_default(int nl, int index, int table, uint32_t gw, int onlink)
{
	struct { struct nlh h; struct rtm r; struct rta4 a[3]; } m;
	uint8_t ack[256];
	ssize_t n;
	static uint32_t seq = 1000;

	memset(&m, 0, sizeof m);
	m.h.len = sizeof m;
	m.h.type = RTM_NEWROUTE_;
	m.h.flags = NLM_F_REQUEST_ | NLM_F_ACK_ | NLM_F_CREATE_ | NLM_F_REPLACE_;
	m.h.seq = seq++;
	m.r.family = 2;
	m.r.table = table < 256 ? (uint8_t)table : 252;	/* RT_TABLE_COMPAT: the real number goes in RTA_TABLE */
	m.r.protocol = 3;					/* RTPROT_BOOT, as `ip route` makes it */
	m.r.scope = 0;						/* RT_SCOPE_UNIVERSE */
	m.r.type = 1;						/* RTN_UNICAST */
	m.r.flags = onlink ? 4 : 0;				/* RTNH_F_ONLINK */
	m.a[0].len = 8; m.a[0].type = 5; m.a[0].v = htonl(gw);	/* RTA_GATEWAY */
	m.a[1].len = 8; m.a[1].type = 4; m.a[1].v = (uint32_t)index;	/* RTA_OIF */
	m.a[2].len = 8; m.a[2].type = 15; m.a[2].v = (uint32_t)table;	/* RTA_TABLE */
	if (send(nl, &m, sizeof m, 0) != (ssize_t)sizeof m)
		return -1;
	if ((n = recv(nl, ack, sizeof ack, 0)) < (ssize_t)(sizeof(struct nlh) + 4) || ((struct nlh *)ack)->type != 2)
		return -1;
	return *(int *)(ack + sizeof(struct nlh)) == 0 ? 0 : -1;
}

static void say_state(const char *path, const char *text)
{
	char tmp[300];
	FILE *f;

	if (!path)
		return;
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	if ((f = fopen(tmp, "w"))) {
		fprintf(f, "%s\n", text);
		if (!fclose(f))
			(void)!rename(tmp, path);
		else
			unlink(tmp);
	}
}

/* The reading process: all it knows is the jack's frames in, and what it says to its parent. */
static int client(int sp, const uint8_t mac[6], const char *host, int drop)
{
	static lw_leaf m;
	static lw_node sbx;
	uint8_t key[32] = {0}, buf[2048];
	struct pollfd pf;
	struct note last, now_n;
	char err[200] = "";
	double now;

	if (drop) {
		struct passwd *pw = getpwnam("nobody");
		unsigned uid = pw ? pw->pw_uid : 65534, gid = pw ? pw->pw_gid : 65534;

		if (setgroups(0, 0) || setgid(gid) || setuid(uid) || getuid() == 0)
			return die("cannot give up the privilege");
	}
	now = lw_now();
	leaf_init(&m, key, now);
	memcpy(m.mac, mac, 6);					/* the jack's own address: what the OS would have sent */
	if (host && *host)
		leaf_hostname(&m, host);
	if (prctl(38 /* PR_SET_NO_NEW_PRIVS */, 1, 0, 0, 0))		/* without it a process that is not root may not install a filter */
		return die("no_new_privs");
	if (lw_sandbox(&sbx, err, sizeof err) < 0)
		return die("cannot close the system calls");
	memset(&last, 0, sizeof last);
	{
		struct note ready;

		memset(&ready, 0, sizeof ready);
		ready.bound = 2;					/* "I am up and closed in": the parent says it is searching only now */
		if (write(sp, &ready, sizeof ready) != (ssize_t)sizeof ready)
			return 1;
	}
	pf.fd = pfd_;
	pf.events = POLLIN;
	while (!term) {
		struct ll from;
		socklen_t fl = sizeof from;
		ssize_t n;

		if (poll(&pf, 1, 1000) > 0 && (pf.revents & POLLIN)) {
			now = lw_now();
			n = recvfrom(pfd_, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
			if (n > 0 && from.pkttype != PACKET_OUTGOING_)
				leaf_dhcp_input(&m, now, buf, (size_t)n, tx_frame, 0);
		}
		now = lw_now();
		leaf_dhcp_tick(&m, now, tx_frame, 0);
		memset(&now_n, 0, sizeof now_n);
		now_n.bound = leaf_bound(&m) ? 1 : 0;
		now_n.ip = now_n.bound ? m.ip : 0;
		now_n.mask = now_n.bound ? m.mask : 0;
		now_n.gw = now_n.bound ? m.gw : 0;
		if (memcmp(&now_n, &last, sizeof now_n)) {
			last = now_n;
			if (write(sp, &now_n, sizeof now_n) != (ssize_t)sizeof now_n)
				return 1;		/* the parent is gone */
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *jack = 0, *host = 0, *pidfile = 0, *state = 0;
	int i, fg = 0, drop = 1, nl, sp[2], index, ready[2], table = 0;
	uint8_t mac[6];
	struct ll ll;
	pid_t kid = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--hostname") && i + 1 < argc)
			host = argv[++i];
		else if (!strcmp(argv[i], "--pidfile") && i + 1 < argc)
			pidfile = argv[++i];
		else if (!strcmp(argv[i], "--state") && i + 1 < argc)
			state = argv[++i];
		else if (!strcmp(argv[i], "--foreground"))
			fg = 1;
		else if (!strcmp(argv[i], "--table") && i + 1 < argc)		/* everything through the line: the jack's own table, whose default route follows the lease's router */
			table = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--no-drop"))
			drop = 0;
		else if (argv[i][0] != '-' && !jack)
			jack = argv[i];
		else {
			fprintf(stderr, "usage: lw-dhcp [--hostname NAME] [--pidfile FILE] [--state FILE] [--table N] [--foreground] JACK\n");
			return 2;
		}
	}
	if (!jack || !*jack || strlen(jack) > 15 || strspn(jack, "abcdefghijklmnopqrstuvwxyz0123456789") != strlen(jack)) {
		fprintf(stderr, "lw-dhcp: the jack is a name of letters and digits, like lw0\n");
		return 2;
	}
	if (!(index = (int)if_nametoindex(jack)) || read_mac(jack, mac))
		return die(jack);
	if ((pfd_ = socket(AF_PACKET_, SOCK_RAW | SOCK_CLOEXEC, htons(0x0800))) < 0)
		return die("packet socket");
	memset(&ll, 0, sizeof ll);
	ll.family = AF_PACKET_;
	ll.proto = htons(0x0800);
	ll.ifindex = index;
	if (bind(pfd_, (struct sockaddr *)&ll, sizeof ll))
		return die("bind");
	if ((nl = socket(16, SOCK_RAW | SOCK_CLOEXEC, 0)) < 0)
		return die("netlink socket");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sp) || pipe(ready))
		return die("socketpair");
	if (!fg) {					/* a daemon: the process that was started returns once the one that stays has written its pid file */
		pid_t d = fork();

		if (d < 0)
			return die("fork");
		if (d > 0) {
			char c;

			close(ready[1]);
			return read(ready[0], &c, 1) == 1 ? 0 : 1;
		}
		setsid();
		(void)!chdir("/");
		i = open("/dev/null", O_RDWR);
		if (i >= 0) {
			dup2(i, 0);
			dup2(i, 1);
			dup2(i, 2);
		}
	}
	if (pidfile) {
		FILE *f = fopen(pidfile, "w");

		if (f) {
			fprintf(f, "%d\n", (int)getpid());
			fclose(f);
		}
	}
	if (!fg)
		(void)!write(ready[1], "1", 1);
	signal(SIGTERM, dhcp_on_term);
	signal(SIGINT, dhcp_on_term);
	signal(SIGPIPE, SIG_IGN);

	kid = fork();
	if (kid < 0)
		return die("fork");
	if (!kid)
		_exit(client(sp[1], mac, host, drop));
	close(sp[1]);
	close(pfd_);

	/* the privileged side: one message at a time from the child, checked, then applied */
	{
		struct pollfd pf;
		uint32_t have_ip = 0, have_mask = 0, have_gw = 0;
		int status;

		pf.fd = sp[0];
		pf.events = POLLIN;
		say_state(state, "starting");
		while (!term) {
			struct note nt;
			ssize_t r;

			if (poll(&pf, 1, 1000) <= 0) {
				if (waitpid(kid, &status, WNOHANG) == kid)
					break;
				continue;
			}
			if ((r = read(sp[0], &nt, sizeof nt)) != (ssize_t)sizeof nt)
				break;			/* the child is gone */
			if (nt.bound == 2) {
				say_state(state, "searching");
			} else if (nt.bound && lease_sane(nt.ip, nt.mask, 0)) {
				if (have_ip && (have_ip != nt.ip || have_mask != nt.mask))
					nl_addr(nl, index, 0, have_ip, have_mask);
				if (!nl_addr(nl, index, 1, nt.ip, nt.mask)) {
					char t[120];
					uint32_t g = nt.gw && lease_sane(nt.ip, nt.mask, nt.gw) ? nt.gw : 0;

					have_ip = nt.ip;
					have_mask = nt.mask;
					if (table > 0 && g != have_gw) {	/* the way out is the lease's router, or the member that shares when the lease names none */
						if (!(g ? nl_default(nl, index, table, g, 0) : nl_default(nl, index, table, 0xa9fe4d01, 1)))
							have_gw = g;
					}
					snprintf(t, sizeof t, "bound %u.%u.%u.%u", nt.ip >> 24, (nt.ip >> 16) & 255, (nt.ip >> 8) & 255, nt.ip & 255);
					if (table > 0 && have_gw)
						snprintf(t + strlen(t), sizeof t - strlen(t), " via %u.%u.%u.%u", have_gw >> 24, (have_gw >> 16) & 255, (have_gw >> 8) & 255, have_gw & 255);
					say_state(state, t);
				}
			} else if (!nt.bound && have_ip) {			/* the lease is lost: the address is not ours, nor the way out through its router */
				nl_addr(nl, index, 0, have_ip, have_mask);	/* first: the kernel takes every IPv4 route of a device off with its last address */
				have_ip = have_mask = 0;
				if (have_gw && !nl_default(nl, index, table, 0xa9fe4d01, 1))
					have_gw = 0;
				say_state(state, "searching");
			}
		}
		if (have_ip)
			nl_addr(nl, index, 0, have_ip, have_mask);
		if (have_gw || (table > 0 && have_ip))			/* after the address: its removal takes the device's routes with it */
			nl_default(nl, index, table, 0xa9fe4d01, 1);
		say_state(state, term ? "stopped" : "failed: the client ended");		/* an end that nobody asked for is said, not hidden */
	}
	kill(kid, SIGTERM);
	waitpid(kid, 0, 0);
	if (pidfile)
		unlink(pidfile);
	return 0;
}
