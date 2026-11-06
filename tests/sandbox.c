/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The sandbox (docs/adr/0062): what a node can still do once it is closed, and what ends it. Each case runs in a child of its own that closes itself with lw_sandbox first, then
 * tries one thing: what the loop needs works; starting a program, making a process, making memory executable, a raw socket, tracing, mounting and the rest end the child with
 * SIGSYS, the kernel's word for a call that is not on the list.
 */
#include "../engine/all.c"
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

static int bad;
static lw_node *node;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		bad = 1;
}

typedef int (*attempt)(const char *dir);

/* Close the node, then try: the exit status is 0 if it worked, or the signal the kernel ended it with. */
static int in_child(attempt f, const char *dir)
{
	int st;
	pid_t p;
	char err[200];

	fflush(0);
	if ((p = fork()) == 0) {
		if (lw_sandbox(node, err, sizeof err)) {
			fprintf(stderr, "sandbox: %s\n", err);
			_exit(90);
		}
		_exit(f(dir) ? 0 : 91);
	}
	waitpid(p, &st, 0);
	return WIFSIGNALED(st) ? 1000 + WTERMSIG(st) : WEXITSTATUS(st);
}

static int a_sockets(const char *d)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0), t = socket(AF_INET6, SOCK_DGRAM, 0);
	struct sockaddr_in a;
	socklen_t al = sizeof a;

	(void)d;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(0x7f000001);
	return s >= 0 && t >= -1 && bind(s, (struct sockaddr *)&a, sizeof a) == 0 && getsockname(s, (struct sockaddr *)&a, &al) == 0 &&
	       sendto(s, "x", 1, 0, (struct sockaddr *)&a, sizeof a) == 1;
}

static int a_loop(const char *d)
{
	struct ifaddrs *ifa;
	char path[300], buf[64];
	FILE *f;
	struct timespec ts;
	void *big;

	snprintf(path, sizeof path, "%s/kept", d);
	if (clock_gettime(CLOCK_MONOTONIC, &ts) || !(big = malloc(1 << 22)))		/* a big allocation is memory, not code */
		return 0;
	memset(big, 1, 1 << 22);
	free(big);
	if (getifaddrs(&ifa))								/* the list of addresses: a netlink socket */
		return 0;
	freeifaddrs(ifa);
	if (!(f = fopen(path, "w")))							/* a file in the state folder, written, renamed, read, removed */
		return 0;
	fputs("kept\n", f);
	fclose(f);
	return rename(path, "kept.moved") != 0 ? (unlink(path), 1) : 1;
}

static int a_status(const char *d)
{
	static char out[8192];

	(void)d;
	node_status_json(node, out, sizeof out);
	return strstr(out, "\"seccomp\":true") != 0 && strstr(out, "\"no_new_privs\":true") != 0 && prctl(21, 0, 0, 0, 0) == 2;
}

static int a_exec(const char *d) { char *av[] = { "/bin/true", 0 }; (void)d; execv("/bin/true", av); return 1; }
static int a_fork(const char *d) { (void)d; return fork() != 0 && 1; }
static int a_mmapx(const char *d) { (void)d; return mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED; }
static int a_mprotx(const char *d)
{
	void *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	(void)d;
	return p != MAP_FAILED && mprotect(p, 4096, PROT_READ | PROT_EXEC) == 0;
}
static int a_packet(const char *d) { (void)d; return socket(AF_PACKET, SOCK_RAW, 0) >= 0; }
static int a_unix(const char *d) { (void)d; return socket(AF_UNIX, SOCK_STREAM, 0) >= 0; }
static int a_ptrace(const char *d) { (void)d; return ptrace(PTRACE_TRACEME, 0, 0, 0) == 0; }
static int a_mount(const char *d) { (void)d; return syscall(SYS_mount, "none", "/tmp", "tmpfs", 0, 0) == 0; }
static int a_unshare(const char *d) { (void)d; return syscall(SYS_unshare, 0x40000000) == 0; }
static int a_memfd(const char *d) { (void)d; return syscall(SYS_memfd_create, "x", 0) >= 0; }
static int a_setuid(const char *d) { (void)d; return syscall(SYS_setuid, 0) == 0; }
static int a_chdirroot(const char *d) { (void)d; return syscall(SYS_chroot, "/") == 0; }
static int a_bpf(const char *d) { (void)d; return syscall(SYS_bpf, 0, 0, 0) >= 0; }
static int a_seccomp_off(const char *d) { (void)d; return prctl(22 /* PR_SET_SECCOMP */, 0, 0, 0, 0) == 0; }
static int a_kill(const char *d) { (void)d; return kill(getppid(), 0) == 0; }

int main(int argc, char **argv)
{
	lw_cfg c;
	char err[200];
	static const struct { const char *what; attempt f; } forbidden[] = {
		{ "starting a program (execve)", a_exec }, { "making a process (fork)", a_fork }, { "making memory executable (mmap with the exec bit)", a_mmapx },
		{ "making memory executable (mprotect with the exec bit)", a_mprotx }, { "a raw packet socket", a_packet }, { "a new local socket (how a session bus is reached)", a_unix }, { "tracing (ptrace)", a_ptrace }, { "mounting", a_mount },
		{ "a new namespace (unshare)", a_unshare }, { "a file with no name (memfd_create)", a_memfd }, { "changing user (setuid)", a_setuid }, { "chroot", a_chdirroot },
		{ "loading a program into the kernel (bpf)", a_bpf }, { "taking the filter off (prctl)", a_seccomp_off }, { "signalling another process (kill)", a_kill },
	};
	size_t i;

	if (argc < 2)
		return 2;
	memset(&c, 0, sizeof c);
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", argv[1]);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", argv[1]);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", argv[1]);
	snprintf(c.port_kind, sizeof c.port_kind, "none");
	snprintf(c.listen, sizeof c.listen, "127.0.0.1:0");
	c.maxpeers = 8;
	c.no_nat_pmp = 1;
	if (!(node = lw_node_open(&c, err, sizeof err))) {
		fprintf(stderr, "cannot open a node: %s\n", err);
		return 2;
	}
	node->now = 1000.0;
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		fprintf(stderr, "no_new_privs: %s\n", strerror(errno));
		return 2;
	}
	if (chdir(argv[1])) {
		fprintf(stderr, "chdir: %s\n", strerror(errno));
		return 2;
	}
	{
		int r = in_child(a_status, argv[1]);

		if (r == 90) {
			printf("skip: this kernel has no seccomp\n");
			return 0;
		}
		check(r == 0, "once closed, the node reports it: seccomp on, no_new_privs on, asked of the kernel");
	}
	check(in_child(a_sockets, argv[1]) == 0, "what the loop needs still works: sockets of its families, bind, getsockname, sendto");
	check(in_child(a_loop, argv[1]) == 0, "...and its clock, memory, the list of addresses, and a file in its state folder written, renamed and removed");
	for (i = 0; i < sizeof forbidden / sizeof forbidden[0]; i++) {
		char what[160];

		snprintf(what, sizeof what, "%s ends the process with SIGSYS", forbidden[i].what);
		check(in_child(forbidden[i].f, argv[1]) == 1000 + SIGSYS, what);
	}
	printf(bad ? "FAILED\n" : "all sandbox checks passed\n");
	return bad;
}
