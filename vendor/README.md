# Vendored code

Third-party sources compiled into the module, unmodified except where noted. Both are built by TCC like everything else.

| Directory | Upstream | Revision | Licence | Local changes |
|---|---|---|---|---|
| `monocypher/` | https://github.com/LoupVaillant/Monocypher (`src/monocypher.[ch]`, `src/optional/monocypher-ed25519.[ch]`, `LICENCE.md`) | `f5f0a19` (2026-10-20) | CC0-1.0 or BSD-2-Clause | none |
| `mlkem768/` | https://github.com/PQClean/PQClean (`crypto_kem/ml-kem-768/clean`, and `common/fips202.[ch]`, `common/randombytes.[ch]`) | `0586a82` (2026-08-04) | public domain / CC0 | `compat.h` is replaced by a five-line shim, because PQClean's compiler check does not recognise TCC |

The shim:

```c
#ifndef PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_VLA(__t,__x,__s) __t __x[__s]
#define PQCLEAN_PREVENT_BRANCH_HACK(b) __asm__("" : "+r"(b) : );
#endif
```

Updating is deliberate: fetch the new revision, reapply the shim, run `make check`. Known-answer tests and a script that
verifies a fetched revision *(planned: M8)*; until then correctness rests on upstream's own tests.
