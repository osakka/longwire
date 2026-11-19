/* SPDX-License-Identifier: GPL-3.0-or-later
 * What TCC and Android's C library (bionic) need said for the engine to build for Android (docs/adr/0074). Force-included (`tcc -include`) before everything else. */
#include <stddef.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/resource.h>

/* bionic has no getdtablesize() before API 29 and no explicit_bzero() before API 28 */
static inline int getdtablesize(void) { struct rlimit r; return getrlimit(RLIMIT_NOFILE, &r) ? 1024 : (int)r.rlim_cur; }
static inline void explicit_bzero(void *p, size_t n) { volatile unsigned char *v = (volatile unsigned char *)p; while (n--) *v++ = 0; }
