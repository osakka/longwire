# Decisions

**Accepted** means you said so. **Proposed** is the default the docs are written to, which you haven't objected to
yet; any of these can still be changed cheaply now. Nothing is open right now.

| # | Decision | Status |
|---|---|---|
| D1 | Name: **longwire** | Accepted |
| D2 | L2 software switch: full-mesh trunks, split horizon, no routing protocol | Accepted |
| D3 | Whole sites first (splice); single machines are required too (jack, shell, member) | Accepted |
| D4 | A node never runs as root; any privilege is a one-time grant | Proposed |
| D5 | Hybrid X25519 + ML-KEM-768 with standard primitives, not exotic ones | Proposed |
| D6 | v1 throughput target is about 300 Mbit/s under TCC | Accepted |
| D7 | Three components, all hibr: switchboard script, node script, module | Accepted |
| D8 | Hard NATs are relayed by line members; the switchboard never relays | Proposed |
| D9 | The first ring always asks; keys are pinned after that | Proposed |
| D10 | The switchboard allocates numbers bound to keys; a full address is also available | Proposed |
| D11 | v1 has one switchboard per deployment and one line per node | Proposed |
| D12 | License: GPL-3.0-or-later (confirmed "or later") | Accepted |
| D13 | Distribution: the module as deb/rpm/pacman/brew packages; the hibr scripts through hibr's catalogue | Accepted |
| D14 | A default public switchboard, operated by you | Accepted |
| D15 | Policy in scripts, security checks in C: the engine emits events and takes verdicts; pins and the handshake budget stay in C | Accepted (built in M1) |

---

<a id="d1"></a>**D1. The name is longwire.** You proposed it. A long wire is one cable stretched between sites, which
is exactly what the product does; it's also the simplest antenna there is, a strand of wire that reaches anywhere.
The rest of the vocabulary comes from the telephone exchange (see [BRAND](BRAND.md)). A web search found no VPN or
mesh project using the name. A trademark and domain search is still needed before a public launch (O3).

<a id="d2"></a>**D2. An L2 software switch.** Every node is a switch port. Sites form a full mesh, and split horizon
removes loops between sites without spanning tree. Two splice boxes on one segment are caught by line probes and
become an HA pair. This is what makes "a client is a server" literally true.

<a id="d3"></a>**D3. Sites first, single machines too.** You said the primary case is bridging whole sites, but that
single machines (phones, laptops) have to work. So splice is the flagship mode, and member mode exists so devices
that only offer an L3 tunnel still show up on the LAN as real Ethernet hosts with a DHCP lease from the far side.

<a id="d4"></a>**D4. Never root.** Your brief said no interfaces, no complication and no root. Full L2 is impossible
without *some* privilege, so the privilege is granted once and held as narrowly as possible:
- splice: `CAP_NET_RAW`, granted by the service manager as an ambient capability to the node's process only. `lw
  start` clears it, along with every other capability, the moment the packet socket is open. No interface is
  created at all. hibr itself is never given a capability (that would go against hibr's ADR 0016).
- jack: a tap device you own.
- shell: user namespaces, with no privilege at all.

<a id="d5"></a>**D5. Boring primitives.** You asked for "crazy efficient, not mainstream" encryption. The design
pushes back on the "not mainstream" part: new primitives are how VPNs get broken. The efficiency comes from the
protocol instead: no TLS, no negotiation, a 29-byte data overhead and a handshake every 2 minutes. AEGIS-256 is
planned as an optional fast suite.

<a id="d6"></a>**D6. About 300 Mbit/s for v1.** Measured: TCC does about 290 Mbit/s per core for
ChaCha20-Poly1305 on 1400-byte packets, and gcc -O3 about 2 Gbit/s. You said "300 for starters is fine", so speed
work is M7.

<a id="d7"></a>**D7. Three components, all hibr.** You decided this: (1) the switchboard is a hibr script, (2) the node
(client and server, with its UI) is a hibr script, and (3) the engine is a hibr module. There's nothing else, and
hibr is installed everywhere, routers included. Earlier drafts had a standalone binary; it's gone.

How the pieces fit:

- **C handles bytes and keys; hibr makes the decisions.** The module's C does sockets, crypto, the switch and NAT
  traversal. The scripts decide who is let in, which numbers are handed out, and what the user sees.
- **The packet path runs in a C worker forked by the module,** the same shape as hibr's `hold` server. The
  interpreter never touches a packet, so a busy script or desktop can't slow traffic.
- **Splice privilege comes from the service manager** (an ambient `CAP_NET_RAW`) and is cleared by `lw start`. hibr
  is never given a file capability, which keeps hibr's ADR 0016 intact. See D4.
- **The engine's C (`engine/`) still never includes `hibr.h`.** That isn't a fourth component, just discipline: when
  Android comes, the same C compiles into the app.
- **The module tracks `HIBR_ABI`; the engine doesn't.** When hibr's ABI moves, only `module/longwire.c` changes.

What it costs, accepted: every node and every switchboard needs hibr installed, including small routers. There is
no single file to `tcc -run`.

Verified: an out-of-tree module built here against `../hibr/include/hibr.h` loads at ABI 16, with no change to hibr.

<a id="d8"></a>**D8. Relays are line members.** Your brief says connectivity never touches the switchboard once a
call is up. Some NAT pairs (both endpoint-dependent, or CGNAT on both sides) can't be punched. Instead of failing,
those calls are relayed by a public member of the same line. The relay sees only sizes and timing, because the
traffic stays end-to-end encrypted between the two endpoints, and the switchboard stays out of the data path.

<a id="d9"></a>**D9. The first ring asks.** Dialing puts the caller on your LAN, so nobody gets in without a pick-up
(or an admit from a member, which is how a party line builds itself). Keys are pinned after the first call. A 6-word
safety phrase defends that first call against a lying switchboard.

<a id="d10"></a>**D10. Allocated numbers.** Your brief has the switchboard hand out the number, tied to a key or a
token, so that only that token has that number. Numbers derived from the key were considered and rejected: a short
derived number can be ground out by brute force, and a long one isn't a phone number any more. So the switchboard
allocates 9 digits (8 random plus a check digit) and binds each one to a key, first come, for good. Tokens can
reserve a number. The full address `lw1…` is available when you want to trust the switchboard for nothing.

<a id="d11"></a>**D11. Keep v1 small.** One switchboard per deployment and one line per node. Federation and
multi-line nodes are on the roadmap.

---

<a id="d12"></a>**D12. GPL-3.0-or-later.** You chose GPL 3. It covers everything in this repository: the engine, the
module, the scripts and the docs. The full text is in [`COPYING`](../COPYING). Every source file carries
`SPDX-License-Identifier: GPL-3.0-or-later`. This uses the "or later" form, matching hibr's own; if you meant
GPL-3.0-*only*, it's a one-word change now and much harder later. The pieces are compatible:
- **hibr** is LGPL-3.0-or-later, so a GPL module loaded by hibr is fine; the combination is GPL.
- **Monocypher** is CC0 or BSD-2, and **PQClean ML-KEM `clean`** is public domain/CC0. Both can be included in a
  GPL work.
- **The future Android app** inherits GPL-3.0 too, which matters if it's ever distributed through an app store.

<a id="d13"></a>**D13. Packages for the module, catalogue for the scripts.** The scripts change often and the C
rarely, and each goes where hibr already puts things of its kind:

| What | How it ships | Where it lands |
|---|---|---|
| module `longwire.so` | **deb, rpm, pacman, Homebrew** packages named `longwire`, from signed repositories like hibr's own (`packaging/` mirrors hibr's `packaging/deb`, `rpm`, `arch`, `homebrew`); each depends on `hibr` | hibr's install module directory (`$PREFIX/lib/hibr/longwire.so`) |
| node `longwire` (with its Control Panel pane as a `# part:`) | **hibr's catalogue**, as an app: `catalogue install longwire` | `~/.config/hibr/apps/`, or the machine's scope for a site box (below) |
| switchboard `longwire-switchboard` | **hibr's catalogue**, as an app | the same |

- **Dependencies are declared in the script header,** so the catalogue checks them before fetching anything. With
  the package installed, `# modules: longwire` is satisfied by "already loadable", and `# abi:` pins the module ABI.
  Without the package, the catalogue refuses the app by name and places nothing.
- **Site boxes use the machine's scope,** with `sudo catalogue --system install longwire`, which places it in
  `/var/lib/hibr`, owned by root. The splice service gets `CAP_NET_RAW` and runs a node script the service's own
  user can't modify, so the privileged moment never runs user-writable code.
- **The package also ships two launchers and a unit template:** `/usr/bin/longwire` and
  `/usr/bin/longwire-switchboard`, each a 3-line `exec hibr …` that runs the catalogue-installed script, or says
  `catalogue install longwire` when it isn't there yet; and `longwire@.service` for splice. A catalogue app isn't a
  command on `PATH` by itself, and the commands in [CLI](CLI.md) need to be. **Accepted** by you.

<a id="d14"></a>**D14. A default public switchboard, run by you.** Nodes work out of the box: a first run registers
with your switchboard without anyone typing an address. You'll supply the host and its full address (`lw1…`); the
docs use the documentation address `203.0.113.7` until then.
- **The default lives in the node script, not the module.** That way, moving or re-keying the public switchboard is a
  catalogue update (`catalogue update`) rather than a new package in four repositories.
- **Self-hosting stays one command:** `longwire --switchboard host:port/lw1…` overrides it, and
  `longwire-switchboard` runs your own.
- **Whoever runs the switchboard sees presence and the call graph.** For yours, that's you. [THREAT-MODEL](THREAT-MODEL.md)
  says so, and points privacy-minded deployments at self-hosting.
- **Operational limits** (registrations per IP, rings per minute, number release after 365 days) are the script's
  defaults, and you can tune them on your instance.

<a id="d15"></a>**D15. Scripts decide, C enforces.** The architecture says C handles bytes and keys while hibr makes
the decisions. M1 settled where the line falls:
- **Scripts decide policy.** The switchboard script decides who may register (tokens, reserved numbers, per-address
  limits) and whose ring goes through. The node script decides what a ring means (the answer policy). The worker
  emits each decision as a JSON event and takes the verdict back (`lw sb reply`, `lw pickup`, `lw reject`). A
  switchboard worker whose script doesn't answer in 2 s refuses; a node worker whose user doesn't answer in 60 s
  refuses.
- **C enforces what must not be skippable.** The pin store (number → key) is checked in C at the moment the other
  end's key arrives, so a script can't forget to check it; a changed key drops the call and raises `key-changed`.
  The handshake budget and the one-way key handling are likewise in C. A script can make the engine *more*
  restrictive but never less.
- **Why it matters:** `longwire-switchboard` is the part you'll tune for the public switchboard (invite-only,
  stricter limits, vanity numbers), and that is now a text file, not a C release.
