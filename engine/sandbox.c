/* SPDX-License-Identifier: GPL-3.0-or-later
 * The sandbox (docs/adr/0062): what the running node can do at all, whatever bug it has. After the node has opened everything it will ever need (sockets,
 * the port, the control socket, its identity) and dropped every capability, the system calls it can make are closed to a list of what the loop needs, and
 * nothing else. No program can be started, no process made, no other process traced, nothing mounted, no memory made executable; sockets only of the four
 * families it uses; ioctl, fcntl and prctl only with the few requests it makes. Anything else ends the process (SECCOMP_RET_KILL_PROCESS), so a bug that is
 * turned into code cannot do anything with it.
 *
 * It is the kernel's, installed once, never lifted (no_new_privs is set). A kernel without seccomp runs without it, and the status says so: the node never
 * claims what it does not have. Written for x86-64 and arm64; elsewhere nothing is installed and the status says that. Confining the files it can reach
 * (Landlock) is not built: the machine this was written on has a kernel without it, so it could not be tried.
 *
 * The constants and structures are spelled out here so TCC need not parse the kernel's headers (as port.c and caps.c do). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "int.h"

/* ---- seccomp ------------------------------------------------------------------------------------------------------------------------------------------ */
struct sbx_insn { unsigned short code; unsigned char jt, jf; unsigned k; };
struct sbx_prog { unsigned short len; struct sbx_insn *filter; };

#define SBX_LD_W_ABS 0x20		/* BPF_LD | BPF_W | BPF_ABS */
#define SBX_JEQ_K    0x15		/* BPF_JMP | BPF_JEQ | BPF_K */
#define SBX_JSET_K   0x45		/* BPF_JMP | BPF_JSET | BPF_K */
#define SBX_RET_K    0x06		/* BPF_RET | BPF_K */
#define SBX_KILL     0x80000000u	/* SECCOMP_RET_KILL_PROCESS */
#define SBX_ALLOW    0x7fff0000u	/* SECCOMP_RET_ALLOW */
#define SBX_MAXPROG  400

#if defined(__x86_64__)
#define SBX_ARCH 0xc000003eu		/* AUDIT_ARCH_X86_64 */
#elif defined(__aarch64__)
#define SBX_ARCH 0xc00000b7u		/* AUDIT_ARCH_AARCH64 */
#endif

#ifdef SBX_ARCH
/* What the loop needs and nothing else. The ones the loop makes most are first (the filter is a list). Verified against a trace of the whole test suite. */
static const int sbx_allow[] = {
	SYS_recvfrom, SYS_sendto, SYS_read, SYS_write, SYS_close, SYS_recvmsg, SYS_sendmsg, SYS_clock_gettime, SYS_getsockname,
	SYS_accept4, SYS_setsockopt, SYS_getsockopt, SYS_bind, SYS_connect, SYS_listen, SYS_getpeername, SYS_shutdown,
	SYS_brk, SYS_munmap, SYS_madvise, SYS_getrandom, SYS_clock_nanosleep, SYS_nanosleep, SYS_gettimeofday,
	SYS_openat, SYS_lseek, SYS_fsync, SYS_fdatasync, SYS_ftruncate, SYS_fstat, SYS_newfstatat, SYS_statx, SYS_fchmod, SYS_fchmodat, SYS_umask,
	SYS_unlinkat, SYS_renameat, SYS_mkdirat, SYS_getuid, SYS_geteuid, SYS_getgid, SYS_getegid, SYS_getpid, SYS_gettid,
	SYS_rt_sigaction, SYS_rt_sigprocmask, SYS_rt_sigreturn, SYS_futex, SYS_exit, SYS_exit_group,
	SYS_ppoll,
#ifdef SYS_poll
	SYS_poll,
#endif
#ifdef SYS_open
	SYS_open, SYS_stat, SYS_lstat, SYS_unlink, SYS_rename, SYS_mkdir, SYS_chmod, SYS_time,
#endif
#ifdef SYS_pread64
	SYS_pread64,
#endif
#ifdef SYS_pwrite64
	SYS_pwrite64,
#endif
#ifdef SYS_readlink
	SYS_readlink,
#endif
#ifdef SYS_access
	SYS_access,
#endif
#ifdef SYS_faccessat
	SYS_faccessat,
#endif
};
#define SBX_NALLOW ((int)(sizeof sbx_allow / sizeof sbx_allow[0]))

static int sbx_n;
static struct sbx_insn sbx_p[SBX_MAXPROG];

static void sbx_emit(unsigned short code, unsigned char jt, unsigned char jf, unsigned k)
{
	sbx_p[sbx_n].code = code;
	sbx_p[sbx_n].jt = jt;
	sbx_p[sbx_n].jf = jf;
	sbx_p[sbx_n].k = k;
	sbx_n++;
}

/* "this system call, only with its argument `arg` one of these values": a block that answers for the call itself (allow or kill) and falls through otherwise. */
static void sbx_arg_in(int nr, int arg, const unsigned *v, int m)
{
	int i;

	sbx_emit(SBX_JEQ_K, 0, (unsigned char)(m + 3), (unsigned)nr);		/* not this call: skip the block (load, m compares, kill, allow) */
	sbx_emit(SBX_LD_W_ABS, 0, 0, (unsigned)(16 + 8 * arg));
	for (i = 0; i < m; i++)
		sbx_emit(SBX_JEQ_K, (unsigned char)(m - 1 - i + 1), 0, v[i]);	/* a match: on to the allow */
	sbx_emit(SBX_RET_K, 0, 0, SBX_KILL);
	sbx_emit(SBX_RET_K, 0, 0, SBX_ALLOW);
	sbx_emit(SBX_LD_W_ABS, 0, 0, 0);						/* the accumulator is the call number again for the next block */
}

/* "this system call, only without the exec bit in its protection argument": no memory is ever made executable after the node is up. */
static void sbx_noexec(int nr, int arg)
{
	sbx_emit(SBX_JEQ_K, 0, 4, (unsigned)nr);
	sbx_emit(SBX_LD_W_ABS, 0, 0, (unsigned)(16 + 8 * arg));
	sbx_emit(SBX_JSET_K, 0, 1, 4);						/* PROT_EXEC */
	sbx_emit(SBX_RET_K, 0, 0, SBX_KILL);
	sbx_emit(SBX_RET_K, 0, 0, SBX_ALLOW);
	sbx_emit(SBX_LD_W_ABS, 0, 0, 0);
}

static int sbx_seccomp(char *err, size_t errn)
{
	static const unsigned fam[] = { 2, 10, 16 };				/* AF_INET, AF_INET6, AF_NETLINK (the list of addresses). No AF_UNIX: the control and port sockets are made before this, and a new one is how a user's session bus is reached (docs/adr/0066) */
	static const unsigned ioc[] = { 0x8927, 0x5421 };		/* SIOCGIFHWADDR, FIONBIO */
	static const unsigned fcn[] = { 1, 2, 3, 4, 1030 };		/* F_GETFD F_SETFD F_GETFL F_SETFL F_DUPFD_CLOEXEC */
	static const unsigned prc[] = { 21, 39 };			/* PR_GET_SECCOMP, PR_GET_NO_NEW_PRIVS: the node can ask what it is */
	struct sbx_prog prog;
	int i;

	sbx_n = 0;
	sbx_emit(SBX_LD_W_ABS, 0, 0, 4);					/* seccomp_data.arch */
	sbx_emit(SBX_JEQ_K, 1, 0, SBX_ARCH);
	sbx_emit(SBX_RET_K, 0, 0, SBX_KILL);
	sbx_emit(SBX_LD_W_ABS, 0, 0, 0);					/* seccomp_data.nr */
	sbx_arg_in(SYS_socket, 0, fam, 3);
	sbx_arg_in(SYS_ioctl, 1, ioc, 2);
	sbx_arg_in(SYS_fcntl, 1, fcn, 5);
	sbx_arg_in(SYS_prctl, 0, prc, 2);
	sbx_noexec(SYS_mmap, 2);
	sbx_noexec(SYS_mprotect, 2);
	for (i = 0; i < SBX_NALLOW; i++)
		sbx_emit(SBX_JEQ_K, (unsigned char)(SBX_NALLOW - i), 0, (unsigned)sbx_allow[i]);	/* the allow is past the rest of the list and the kill */
	sbx_emit(SBX_RET_K, 0, 0, SBX_KILL);
	sbx_emit(SBX_RET_K, 0, 0, SBX_ALLOW);
	if (sbx_n >= SBX_MAXPROG || SBX_NALLOW > 200) {
		snprintf(err, errn, "the system call filter is too long");
		return -1;
	}
	prog.len = (unsigned short)sbx_n;
	prog.filter = sbx_p;
	if (syscall(SYS_seccomp, 1 /* SECCOMP_SET_MODE_FILTER */, 0, &prog) < 0) {
		if (errno == ENOSYS || errno == EINVAL)
			return 0;						/* this kernel has none: said in the status */
		snprintf(err, errn, "seccomp: %s", strerror(errno));
		return -1;
	}
	return 1;
}
#endif

/* Every capability set empty: asked of the kernel, as caps.c does when it clears them. */
static int sbx_caps_empty(void)
{
	struct { unsigned version; int pid; } h = { 0x20080522, 0 };
	struct { unsigned effective, permitted, inheritable; } d[2];
	int i;

	memset(d, 0xff, sizeof d);
	if (syscall(SYS_capget, &h, d) < 0)
		return 0;
	for (i = 0; i < 2; i++)
		if (d[i].effective | d[i].permitted | d[i].inheritable)
			return 0;
	return 1;
}

/* Close both on the running node, once. Not for a node that is only being tried (the engine's own tests call node functions directly). */
int lw_sandbox(lw_node *n, char *err, size_t errn)
{
	int r;

	n->sbx_seccomp = 0;
	n->sbx_done = 1;
	n->sbx_caps = !sbx_caps_empty();
#ifdef SBX_ARCH
	if ((r = sbx_seccomp(err, errn)) < 0)
		return -1;
	n->sbx_seccomp = r;
#endif
	return 0;
}

/* For the status: what is actually in force, asked of the kernel (the node can ask it what it is, and nothing else). */
int sandbox_value(lw_node *n, char *o, size_t cap)
{
	int sec = 0, nnp = 0;

#ifdef SBX_ARCH
	if (n->sbx_done) {
		sec = prctl(21 /* PR_GET_SECCOMP */, 0, 0, 0, 0) == 2;
		nnp = prctl(39 /* PR_GET_NO_NEW_PRIVS */, 0, 0, 0, 0) == 1;
	}
#endif
	return LW_SN(o, cap, "{\"seccomp\":%s,\"no_new_privs\":%s,\"capabilities\":%s,\"uid\":%u,\"installed\":%s}", sec ? "true" : "false",
			nnp ? "true" : "false", n->sbx_caps ? "true" : "false", (unsigned)geteuid(), n->sbx_done ? "true" : "false");
}
