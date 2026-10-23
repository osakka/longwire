# Build and install

## The promise

```
sudo apt install longwire          # the module (also rpm, pacman, brew)
catalogue install longwire         # the node script, from hibr's catalogue
longwire                           # your number
```

**Dependencies: hibr, and libc.** The crypto is vendored. There's no OpenSSL, no libsodium, no pkg-config, and no
build system beyond one `Makefile`, in the same spirit as hibr's ADR 0001.

## How it ships ([DECISIONS D13](DECISIONS.md#d13))

| Component | Source | Ships as | Lands in |
|---|---|---|---|
| 3. module | `module/longwire.c` + `engine/*.c` + `vendor/*` | **deb, rpm, pacman, Homebrew** package `longwire` (depends on `hibr`) | `$PREFIX/lib/hibr/longwire.so` |
| 2. node | `scripts/longwire.hibr` (+ `control-panel/lines.hibr` as a `# part:`) | **hibr catalogue** app `longwire` | `~/.config/hibr/apps/`, or `/var/lib/hibr` with `--system` |
| 1. switchboard | `scripts/longwire-switchboard.hibr` | **hibr catalogue** app `longwire-switchboard` | the same |
| launchers + unit | `packaging/bin/longwire`, `packaging/bin/longwire-switchboard`, `packaging/systemd/longwire@.service` | in the package | `/usr/bin/`, `/usr/lib/systemd/system/` |

```
# a laptop
sudo apt install longwire             # or dnf / pacman / brew
catalogue install longwire
longwire

# a site box
sudo apt install longwire
sudo catalogue --system install longwire      # root-owned script for the privileged service
sudo longwire setup splice eth0               # writes the drop-in, enables longwire@eth0
```

`packaging/` mirrors hibr's own (`deb/`, `rpm/`, `arch/`, `homebrew/`, each with `build-*.sh` and `publish.sh`),
publishing to signed repositories in the same way. One key signs all of them, and the packages hold the same files,
byte for byte.

**Script headers** declare what they need, so the catalogue checks before it fetches:

```sh
app longwire "One long wire between your sites: dial a number, share a LAN" 1.0
# part: control-panel/lines.hibr
# modules: longwire
# abi: 16
```

**Loading the module.** Scripts use `mod load "${LONGWIRE_MOD:-longwire}"`. The splice unit sets
`LONGWIRE_MOD` to the packaged absolute path, so the privileged moment never searches `HIBR_MODPATH` or the working
directory for code. Interactive use resolves by name as usual.

## Running a site node (splice)

```ini
# /usr/lib/systemd/system/longwire@.service — shipped in the package; instance = interface
[Service]
AmbientCapabilities=CAP_NET_RAW
Environment=LONGWIRE_MOD=/usr/lib/hibr/longwire.so
ExecStart=/usr/bin/longwire up --splice %i
Restart=always
# User= comes from the drop-in that `longwire setup splice` writes
```

`longwire setup splice eth0` writes the drop-in (`User=`) and enables `longwire@eth0`. The systemd unit is the
supported path. Without systemd, any launcher that starts the process as that user with an ambient `CAP_NET_RAW`
does the same job (`setpriv`, `capsh`, OpenRC's `capabilities=`), and the exact invocation is documented per init
system once it's been tested. The capability lives only until `lw start` has opened the packet socket, and then it's
cleared in every process (ARCHITECTURE §2).

Jack and shell modes need no service and no privilege: `longwire up` in a terminal, under `hold`, or as a systemd
*user* unit.

## Building from source

```
make                        # tcc, ~1 s: build/longwire.so, against ../hibr/include/hibr.h
make CC=gcc OPT=-O2         # same source, ~7× the crypto throughput
make HIBR=/path/to/hibr     # hibr's headers somewhere else
make check                  # the end-to-end tests: needs hibr, python3 and unprivileged user namespaces
```

The module is `module/longwire.c` plus the engine (`engine/all.c`) and `vendor/`, compiled together. The engine is
compiled *separately from* `hibr.h`, which is what keeps it free of hibr: its helper names would otherwise collide
with hibr's headers. Both `make` (TCC) and `make CC=gcc OPT=-O2` build clean, gcc with `-Wall -Wextra`, and the same
tests pass on each. The M1 tests build a miniature internet (two NATs and a public network) from unprivileged network
namespaces, with a userspace NAT router written in Python, so they need `unshare -Urn` to work; distributions that
restrict unprivileged user namespaces (Ubuntu 24.04 and later by default) need that relaxed to run them.

## Vendored code

| Directory | Upstream | Tested at | TCC changes |
|---|---|---|---|
| `vendor/monocypher/` | Monocypher (incl. `optional/monocypher-ed25519.c`), CC0 / BSD-2 | `f5f0a19`. Pin the next tagged 4.x release | none |
| `vendor/mlkem768/` | PQClean `crypto_kem/ml-kem-768/clean` + `common/fips202.c`, public domain | `0586a82` | `compat.h` replaced by a 5-line shim (below) |

```c
/* vendor/mlkem768/compat.h — PQClean's compiler check doesn't know TCC */
#ifndef PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_VLA(__t,__x,__s) __t __x[__s]
#define PQCLEAN_PREVENT_BRANCH_HACK(b) __asm__("" : "+r"(b) : );
#endif
```

Vendored code is updated only deliberately. *(Planned, not written yet: `tools/vendor.sh` fetches a pinned commit, verifies its
hash, reapplies the shim and runs the known-answer tests)* (NIST KATs for ML-KEM, RFC 8439 vectors, RFC 7748 and RFC 8032 vectors).

## TCC constraints we design around

| TCC reality | Consequence |
|---|---|
| no `__int128` | Use 32-bit-limb arithmetic. Monocypher and PQClean `clean` already do (verified). |
| no SIMD intrinsics, little optimisation | Bulk crypto runs at about 290 Mbit/s per core. x86-64 assembly paths (TCC's assembler handles x86; arm64 is still to be verified) come later; the gcc build is there for speed now. |
| can't parse some glibc headers (e.g. `regex.h`, as hibr found) | Only use plain POSIX/Linux headers: `sys/socket.h`, `linux/if_packet.h`, `linux/if_tun.h`, `sched.h`, `linux/capability.h`. |
| no sanitizers | ASan/UBSan, fuzzing and coverage run on the gcc/clang build in CI, as hibr does. |

Measured in the design spike (x86_64, TCC 0.9.27): the complete crypto test harness linked to **134 KB** under TCC.
A static gcc `-Os` build of the same harness was 736 KB. `tcc … -run` compiled and ran it from source in one step.

## Platforms

| Platform | Attach modes | Notes |
|---|---|---|
| **Linux** x86_64 / arm64 | splice, jack, shell | the reference platform; wherever hibr runs |
| **FreeBSD / OpenBSD** | splice (BPF), jack (tap) | `net.link.tap.user_open=1`; BPF device permissions for splice. Depends on hibr's own BSD support |
| **macOS** | splice (BPF, wired), member (utun) | hibr ships through Homebrew. BPF injection works with `/dev/bpf*` access, as Wireshark's ChmodBPF grants. macOS has no L2 tap without a system extension |
| **Android** | member | later: `engine/` compiled with the NDK inside a small Kotlin `VpnService` app, with the UDP socket `protect()`ed. The only place without hibr |
| **Windows**, **iOS** | member | later |

## The module and hibr's ABI

Verified in the design spike: a module built out-of-tree in this repository against `../hibr/include/hibr.h`
loads with `mod load` in hibr 0.99.302 at **ABI 16**. Nothing in the hibr tree has to change. When hibr's ABI
moves, only `module/longwire.c` needs attention. `engine/` doesn't include `hibr.h` and doesn't care.
