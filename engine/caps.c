/* SPDX-License-Identifier: GPL-3.0-or-later
 * After the port is open nothing needs privilege again: clear every
 * capability set and forbid regaining any (docs/ARCHITECTURE.md §2). */
#define _GNU_SOURCE
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "lw.h"

struct hdr { unsigned version; int pid; };
struct dat { unsigned effective, permitted, inheritable; };

#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_CLEAR_ALL 4
#endif
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

int lw_caps_drop(void)
{
	struct hdr h = { 0x20080522, 0 };	/* _LINUX_CAPABILITY_VERSION_3 */
	struct dat d[2];
	int i;

	memset(d, 0, sizeof d);
	prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0);	/* EINVAL on old kernels: fine */
	if (syscall(SYS_capset, &h, d) < 0)
		return -1;
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
		return -1;
	h.version = 0x20080522;
	memset(d, 0xff, sizeof d);
	if (syscall(SYS_capget, &h, d) < 0)
		return -1;
	for (i = 0; i < 2; i++)
		if (d[i].effective | d[i].permitted | d[i].inheritable)
			return -1;
	return 0;
}
