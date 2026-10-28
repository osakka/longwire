# Build and install

**Dependencies: hibr, and libc.** The crypto is vendored. There's no OpenSSL, no libsodium, no pkg-config, and no
build system beyond one `Makefile`, in the same spirit as hibr's own ADR 0001. **TCC is the only compiler**, with no
exceptions ([ADR 0021](adr/0021-tcc-is-the-compiler-without-exception.md)). Anything designed and unbuilt carries
`*(planned: M5)*` or similar.

## Building and running today

```
make                                   # tcc, about a second: build/longwire.so, against ../hibr/include/hibr.h
export LONGWIRE_MOD=$PWD/build/longwire.so

hibr scripts/longwire-switchboard.hibr --listen 1877 --registry none     # on the switchboard host
hibr scripts/longwire.hibr up --switchboard HOST:1877/lw1… --splice eth0       # on a node
```

Here the module is built from source and the scripts run from the repository; the packages and the catalogue are below. `make` needs `tcc` and hibr's header directory, `../hibr` by default.

```
make HIBR_SRC=/path/to/hibr  # hibr's source tree, for its headers (not the HIBR the tests use)
make lint                    # TCC's own warnings, as errors, on everything we wrote
make check                   # lint, then all the tests, below
make clean
```

`make check` runs every end-to-end suite (`tests/*.sh`, listed in the Makefile) and the documentation
check (`tests/docs.py`). It needs hibr (built, at `../hibr/build/hibr` or `$HIBR`), python3, and **unprivileged user
and network namespaces** (`unshare -Urn`): the tests build their own network ([ADR 0019](adr/0019-the-tests-build-their-own-network.md)).
Distributions that restrict unprivileged user namespaces (Ubuntu 24.04 and later by default) need that relaxed to run
them.

The module is `module/longwire.c` plus the engine (`engine/all.c`) and `vendor/`, compiled together. The engine is
compiled *separately from* `hibr.h`, which is what keeps it free of hibr: its helper names would otherwise collide with
hibr's headers. The Makefile says `override CC = tcc`, so naming a different compiler on the command line changes nothing; there are no
optimisation flags either. `make lint` (`tcc -Wall -Werror -Wunsupported`) is part of `make check`.

## How it ships

| Component | Source | Ships as | Lands in |
|---|---|---|---|
| 3. module | `module/longwire.c` + `engine/*.c` + `vendor/*` | **deb, rpm, pacman** package `longwire` (depends on `hibr`) | `/usr/lib/hibr/longwire.so` |
| 2. node | `scripts/longwire.hibr` | **hibr catalogue** command `cli/longwire` | `~/.local/share/hibr/cli/`, or `/var/lib/hibr/cli` with `--system` |
| 1. switchboard | `scripts/longwire-switchboard.hibr` | **hibr catalogue** command `cli/longwire-switchboard` | the same |
| 1b. switchboard app | `apps/switchboard.hibr`, its own entry, the catalogue's `switchboard` in the `apps` kind | the desktop's `apps` kind | a desktop scan root |
| 2b. control panel pane | `control-panel/longwire.hibr`, its own entry, the catalogue's `longwire` in the `control-panel` kind | the desktop's `control-panel` kind | a desktop scan root |
| launchers + services | `packaging/launcher.sh` (twice), `packaging/systemd/` | in the package | `/usr/bin/`, `/usr/lib/systemd/system/` |

```
sudo apt install longwire                       # a laptop: the module and the launchers (or dnf / pacman)
catalogue install cli/longwire                  # the node script, from hibr's catalogue
longwire up --switchboard …

sudo apt install longwire                       # a site box
sudo catalogue --system install cli/longwire    # a root-owned script for the service
echo 'ARGS="--switchboard HOST:PORT/KEY --answer known"' | sudo tee /etc/longwire/eth0.conf
sudo systemctl enable --now longwire@eth0       # the node, splicing eth0
```

`make packages` builds all three into `build/pkg`: the deb first (`packaging/deb/build-deb.sh`, from `make install` into a
staging directory), then the rpm and the pacman package from the deb's own files (`packaging/rpm/build-rpm.sh`,
`packaging/arch/build-pkg.sh`), so they cannot differ in what they hold. It needs `dpkg-deb`, `fakeroot`, `rpmbuild` and
`makepkg`, and no root; the module itself is built with TCC. `tests/pkg.sh` builds them and checks them. A package depends on
hibr at `HIBR_MIN` (the oldest release the suite has been run against), and its version is `VERSION`. The reasoning is in
[ADR 0013](adr/0013-packages-for-the-module-the-catalogue-for-the-scripts.md) and
[ADR 0028](adr/0028-one-deb-and-the-others-from-it.md). The three are published in hibr's own signed repositories, which also carry hibr (set one up as hibr's README says, then
`apt install longwire`, `dnf install longwire` or `pacman -S longwire`). A Homebrew formula is *(planned: M5)*, and waits for macOS.

**The services.** `longwire-switchboard.service` runs a switchboard: put `ARGS="--listen 1877 --registry none"` (or your
registry) in `/etc/longwire/switchboard.conf` and enable it. `longwire@NIC.service` runs a spliced site node on `NIC` with the
arguments in `/etc/longwire/NIC.conf`; it holds `CAP_NET_RAW` and nothing else. Both run as a transient user with a state
directory of their own (`/var/lib/longwire-switchboard`, `/var/lib/longwire`: the identity lives there), and both start the
launchers, so the scripts must have been installed for the machine with `catalogue --system`. The scripts are commands, not desktop apps, so they are published under the catalogue's `cli` kind
([ADR 0029](adr/0029-commands-are-their-own-catalogue-kind.md)). hibr's `catalogue` has the kind from 0.99.315, and the two bundles are published,
with the pane and the app.

**The group `longwire`.** The package makes a group of that name, and both services give their control socket to it (mode 0660, in
`/run/longwire/NIC/ctl` and `/run/longwire-switchboard/ctl`). A person in the group can use the services from the hibr desktop (the pane
and the switchboard application find those sockets themselves) and from the command line (`LW_SOCK=/run/longwire/eth0/ctl longwire peers`); join
with `sudo usermod -aG longwire NAME` and log in again. Being in it means being allowed to dial, hang up and evict on that
node: it never reaches a key ([ADR 0032](adr/0032-the-services-share-their-control-socket-with-a-group.md)). The `longwire setup splice` unit below runs as you, so your own pane already reaches its socket and it needs no group. Not run: the units under a live systemd.

The scripts declare what they need in their headers, so the catalogue checks before it fetches:

```sh
app longwire "One long wire between your sites: dial a number, share a LAN" 0.5.15
# modules: longwire
```

**Loading the module.** Scripts use `mod load "${LONGWIRE_MOD:-longwire}"`: an explicit path when `LONGWIRE_MOD` is
set, a search by name otherwise. The unit that `longwire setup splice` prints sets it to the module's absolute path
(`/usr/lib/hibr/longwire.so` unless `LONGWIRE_MOD` is already set when you run it), so the privileged moment never
searches `HIBR_MODPATH` or the working directory for code.

**Trying the pane.** From a checkout, as an ordinary user: `mkdir -p ~/.config/hibr/control-panel && ln -s $PWD/control-panel/longwire.hibr
~/.config/hibr/control-panel/`, start the desktop with `HIBR_MODPATH=$PWD/build`, and choose Refresh Desktop; the pane is
called Longwire, under Apps. It drives a node you started yourself (`longwire up …`, or its own Start row) or, if you have none, a service node
whose socket is under `/run/longwire` and whose group (`longwire`) you are in. Its form is a saved connection ([ADR 0034](adr/0034-a-connection-is-a-profile.md)): a profile,
the switchboard, a token, and Turn on, which makes the jack (a password, once) and starts the node in the background.

**Trying the menu bar item and the strip module.** The same way again, from the checkout: `mkdir -p ~/.config/hibr/menubar ~/.config/hibr/control-strip`,
`ln -s $PWD/menubar/longwire.hibr ~/.config/hibr/menubar/` and `ln -s $PWD/control-strip/longwire.hibr ~/.config/hibr/control-strip/`, then Refresh Desktop. A telephone
appears on the bar ([ADR 0035](adr/0035-longwire-on-the-menu-bar.md)); Control Panel > Longwire > Longwire on the Menu Bar turns it. From the catalogue, installing
the bundle `longwire` installs the pane, the item and the module together.

**Trying the switchboard app.** The same way, with `apps` in place of `control-panel`: `ln -s $PWD/apps/switchboard.hibr ~/.config/hibr/apps/`,
then Refresh Desktop; it is called Switchboard, on the hibr menu. It reads the switchboard you started as the same user
(`~/.longwire/switchboard/ctl`), or the service's (`/run/longwire-switchboard/ctl`) if you are in the group `longwire`; press `s` to name another socket.

## Running a site node (splice)

`longwire setup splice NIC` prints the service unit (`/usr/bin/longwire` is the launcher the package installs
(the package installs it; if you run from a checkout write the paths to `hibr` and to the script there):

```ini
[Unit]
Description=longwire site node (splice on eth0)
After=network-online.target

[Service]
User=you
# CAP_NET_RAW opens the packet socket. 'lw start' clears every capability the moment it is open.
AmbientCapabilities=CAP_NET_RAW
# the module by absolute path, so the privileged moment never searches the environment for code
Environment=LONGWIRE_MOD=/usr/lib/hibr/longwire.so
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

Jack mode needs no service, and no privilege but the creation of the tap, which `longwire jack` (or `longwire on`, or the Control Panel's Turn on) does through
a small root helper after one password ([ADR 0034](adr/0034-a-connection-is-a-profile.md)). Then `longwire on` runs it in the background, or `longwire up --jack lw0` in a terminal,
under `hold`, or as a systemd *user* unit.

## Vendored code

| Directory | Upstream | Revision (also in [vendor/README.md](../vendor/README.md)) | TCC changes |
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
| no SIMD intrinsics, little optimisation | The cipher runs at about 290 Mbit/s per core. More speed comes from the code, not another compiler: x86-64 assembly paths that TCC's assembler builds (arm64 is still to be verified) *(planned: M7)*. |
| can't parse some glibc headers (e.g. `regex.h`, as hibr found) | Only plain POSIX headers are included. The few Linux-specific constants (packet sockets, tun, capabilities) are spelled out in `engine/port.c` and `engine/caps.c`, so TCC never has to parse the kernel's headers. |
| no sanitizers | `-Wall -Werror` always on (`make lint`), small parsers, the end-to-end tests, and fuzzing with a harness TCC builds *(planned: M8)*. |

Measured in the design spike (x86_64, TCC 0.9.27): the complete crypto test harness linked to **134 KB**, and
`tcc … -run` compiled and ran it from source in one step.

## Platforms

| Platform | Attach modes | Notes |
|---|---|---|
| **Linux** x86_64 | splice, jack | the reference platform, wherever hibr runs. jack is written but untested (no `/dev/net/tun` on the build machine). arm64 *(planned)* |
| **Linux**, unprivileged | shell | *(planned: M5)* |
| **FreeBSD / OpenBSD** | splice (BPF), jack (tap) | `net.link.tap.user_open=1`; BPF device permissions for splice *(planned: M5)* |
| **macOS** | splice (BPF, wired), member (utun) | BPF injection works with `/dev/bpf*` access, as Wireshark's ChmodBPF grants; macOS has no L2 tap without a system extension *(planned: M5)* |
| **Android** | member | `engine/` built by TCC for arm64 into a library a small Kotlin `VpnService` app loads, the UDP socket `protect()`ed; the only place without hibr. Whether TCC can build an Android-loadable library is unverified and comes first *(planned: M6)* |
| **Windows**, **iOS** | member | *(planned)* |

## The module and hibr's ABI

A module built out of tree in this repository against `../hibr/include/hibr.h` loads with `mod load` in hibr 0.99.x
at **ABI 16**. Nothing in the hibr tree has to change. When hibr's ABI moves, only `module/longwire.c` needs
attention; `engine/` doesn't include `hibr.h` and doesn't care.
