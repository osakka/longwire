# Build and install

**Dependencies: hibr, and libc.** The crypto is vendored. There's no OpenSSL, no libsodium, no pkg-config, and no
build system beyond one `Makefile`, in the same spirit as hibr's own ADR 0001. Anything designed and unbuilt carries
`*(planned: M4)*`.

## Building and running today

```
make                                   # tcc, about a second: build/longwire.so, against ../hibr/include/hibr.h
export LONGWIRE_MOD=$PWD/build/longwire.so

hibr scripts/longwire-switchboard.hibr --listen 0.0.0.0:1877     # on the switchboard host
hibr scripts/longwire.hibr up --switchboard HOST:1877/lw1… --splice eth0       # on a node
```

The module is built from source and the scripts run from the repository; the packages and the catalogue
(below) *(planned: M4)*. `make` needs `tcc` (or `CC=gcc`) and hibr's header directory, `../hibr` by default.

```
make CC=gcc OPT=-O2          # the same source with gcc: about 7× the crypto throughput
make HIBR=/path/to/hibr      # hibr's headers somewhere else
make check                   # all the tests, below
make clean
```

`make check` runs the three end-to-end suites (`tests/m0.sh`, `tests/m1.sh`, `tests/m2.sh`) and the documentation
check (`tests/docs.py`). It needs hibr (built, at `../hibr/build/hibr` or `$HIBR`), python3, and **unprivileged user
and network namespaces** (`unshare -Urn`): the tests build their own network ([ADR 0019](adr/0019-the-tests-build-their-own-network.md)).
Distributions that restrict unprivileged user namespaces (Ubuntu 24.04 and later by default) need that relaxed to run
them.

The module is `module/longwire.c` plus the engine (`engine/all.c`) and `vendor/`, compiled together. The engine is
compiled *separately from* `hibr.h`, which is what keeps it free of hibr: its helper names would otherwise collide with
hibr's headers. Both `make` (TCC) and `make CC=gcc OPT=-O2` build clean (gcc with `-Wall -Wextra`), and the same tests
pass on each.

## How it will ship *(planned: M4)*

| Component | Source | Ships as | Lands in |
|---|---|---|---|
| 3. module | `module/longwire.c` + `engine/*.c` + `vendor/*` | **deb, rpm, pacman, Homebrew** package `longwire` (depends on `hibr`) | `$PREFIX/lib/hibr/longwire.so` |
| 2. node | `scripts/longwire.hibr` (+ `control-panel/lines.hibr` as a `# part:`) *(planned: M4)* | **hibr catalogue** app `longwire` | `~/.config/hibr/apps/`, or `/var/lib/hibr` with `--system` |
| 1. switchboard | `scripts/longwire-switchboard.hibr` | **hibr catalogue** app `longwire-switchboard` | the same |
| launchers + unit | `packaging/bin/longwire`, `packaging/bin/longwire-switchboard`, `packaging/systemd/longwire@.service` *(planned: M4)* | in the package | `/usr/bin/`, `/usr/lib/systemd/system/` |

```
sudo apt install longwire                       # a laptop: the module (or dnf / pacman / brew)
catalogue install longwire                      # the node script, from hibr's catalogue
longwire up --switchboard …

sudo apt install longwire                       # a site box
sudo catalogue --system install longwire        # a root-owned script for the privileged service
longwire setup splice eth0                      # prints the unit; install and enable it
```

`packaging/` will mirror hibr's own (`deb/`, `rpm/`, `arch/`, `homebrew/`, each with a build and a publish script),
publishing to signed repositories the same way, one key for all of them. The reasoning is in
[ADR 0013](adr/0013-packages-for-the-module-the-catalogue-for-the-scripts.md).

The scripts will declare what they need in their headers, so the catalogue checks before it fetches:

```sh
app longwire "One long wire between your sites: dial a number, share a LAN" 1.0
# part: control-panel/lines.hibr
# modules: longwire
# abi: 16
```

**Loading the module.** Scripts use `mod load "${LONGWIRE_MOD:-longwire}"`: an explicit path when `LONGWIRE_MOD` is
set, a search by name otherwise. A service sets it to the module's absolute path, so the privileged moment never
searches `HIBR_MODPATH` or the working directory for code.

## Running a site node (splice)

`longwire setup splice NIC` prints the service unit (`/usr/bin/longwire` is the launcher the package installs
*(planned: M4)*; until then write the paths to `hibr` and to the script there):

```ini
[Unit]
Description=longwire site node (splice on eth0)
After=network-online.target

[Service]
User=you
# CAP_NET_RAW opens the packet socket. 'lw start' clears every capability the moment it is open.
AmbientCapabilities=CAP_NET_RAW
ExecStart=/usr/bin/longwire up --splice eth0
Restart=always

[Install]
WantedBy=multi-user.target
```

The systemd unit is the supported path. Without systemd, any launcher that starts the process as that user with an
ambient `CAP_NET_RAW` does the same job (`setpriv`, `capsh`, OpenRC's `capabilities=`); the exact invocation for each
init system is documented once it has been tested *(planned: M5)*. The capability lives only until `lw start` has
opened the packet socket, and then it's cleared in every process
([ARCHITECTURE §2](ARCHITECTURE.md#2-attaching-a-node-ports)).

Jack mode needs no service and no privilege after the one-time tap: `longwire up --jack lw0` in a terminal, under
`hold`, or as a systemd *user* unit.

## Vendored code

| Directory | Upstream | Tested at | TCC changes |
|---|---|---|---|
| `vendor/monocypher/` | Monocypher (incl. `optional/monocypher-ed25519.c`), CC0 / BSD-2 | `f5f0a19` | none |
| `vendor/mlkem768/` | PQClean `crypto_kem/ml-kem-768/clean` + `common/fips202.c`, public domain | `0586a82` | `compat.h` replaced by a 5-line shim (below) |

```c
/* vendor/mlkem768/compat.h — PQClean's compiler check doesn't know TCC */
#ifndef PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_COMMON_COMPAT_H
#define PQCLEAN_VLA(__t,__x,__s) __t __x[__s]
#define PQCLEAN_PREVENT_BRANCH_HACK(b) __asm__("" : "+r"(b) : );
#endif
```

Vendored code is updated only deliberately. A script that fetches a pinned commit, verifies its hash, reapplies the
shim and runs the known-answer tests (NIST vectors for ML-KEM, RFC 8439, RFC 7748 and RFC 8032) *(planned: M8)*.

## TCC constraints we design around

| TCC reality | Consequence |
|---|---|
| no `__int128` | Use 32-bit-limb arithmetic. Monocypher and PQClean `clean` already do (verified). |
| no SIMD intrinsics, little optimisation | The cipher runs at about 290 Mbit/s per core. x86-64 assembly paths (TCC's assembler handles x86; arm64 is still to be verified) *(planned: M7)*; the gcc build is there for speed now. |
| can't parse some glibc headers (e.g. `regex.h`, as hibr found) | Only plain POSIX headers are included. The few Linux-specific constants (packet sockets, tun, capabilities) are spelled out in `engine/port.c` and `engine/caps.c`, so TCC never has to parse the kernel's headers. |
| no sanitizers | Sanitizers, fuzzing and coverage on the gcc/clang build *(planned: M8)*. |

Measured in the design spike (x86_64, TCC 0.9.27): the complete crypto test harness linked to **134 KB** under TCC. A
static gcc `-Os` build of the same harness was 736 KB. `tcc … -run` compiled and ran it from source in one step.

## Platforms

| Platform | Attach modes | Notes |
|---|---|---|
| **Linux** x86_64 | splice, jack | the reference platform, wherever hibr runs. jack is written but untested (no `/dev/net/tun` on the build machine). arm64 *(planned)* |
| **Linux**, unprivileged | shell | *(planned: M5)* |
| **FreeBSD / OpenBSD** | splice (BPF), jack (tap) | `net.link.tap.user_open=1`; BPF device permissions for splice *(planned: M5)* |
| **macOS** | splice (BPF, wired), member (utun) | BPF injection works with `/dev/bpf*` access, as Wireshark's ChmodBPF grants; macOS has no L2 tap without a system extension *(planned: M5)* |
| **Android** | member | `engine/` compiled with the NDK inside a small Kotlin `VpnService` app, the UDP socket `protect()`ed; the only place without hibr *(planned: M6)* |
| **Windows**, **iOS** | member | *(planned)* |

## The module and hibr's ABI

A module built out of tree in this repository against `../hibr/include/hibr.h` loads with `mod load` in hibr 0.99.302
at **ABI 16**. Nothing in the hibr tree has to change. When hibr's ABI moves, only `module/longwire.c` needs
attention; `engine/` doesn't include `hibr.h` and doesn't care.
