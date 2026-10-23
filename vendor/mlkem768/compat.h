/* PQClean's compiler check doesn't know TCC; see docs/BUILD.md */
#ifndef PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_VLA(__t,__x,__s) __t __x[__s]
#define PQCLEAN_PREVENT_BRANCH_HACK(b) __asm__("" : "+r"(b) : );
#endif
