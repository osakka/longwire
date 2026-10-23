# longwire

> **One long wire between your sites.** Same LAN, any distance.

longwire stretches one Ethernet segment across the internet. You run it at each site, dial a number, and the LANs
become one subnet. A printer in Lisbon answers ARP in Osaka. The DHCP server in your basement hands an address to
your phone on hotel Wi-Fi.

```
$ longwire
  ☎  your number   447-120-933
     switchboard   203.0.113.7:1877   dial tone

$ longwire dial 312-884-071
  ringing 312-884-071 …  picked up
  connected  direct (hole-punched)  31 ms  pq-hybrid
  spliced    eth0 ⇄ line 7f3a9c  — 10.0.0.0/24 now spans 2 sites
```

## What it is

- **Layer 2, not layer 3.** longwire carries Ethernet frames, so ARP, DHCP, mDNS and everything else on your LAN
  works across sites. There are no route tables, no second subnet and no IP plan.
- **Every node is equal.** The same node script is a site bridge, a laptop, and a relay. A client *is* a server:
  each node is a port on one software switch.
- **You dial a number.** Every node gets a short number from the **switchboard**, a tiny rendezvous server. It
  introduces two nodes, coordinates a NAT hole-punch, then **hangs up**. Traffic never passes through it.
- **NAT on both sides is fine.** Nodes punch through NAT directly. When that's impossible, traffic is relayed by
  another node on *your own* line, still end-to-end encrypted.
- **It never runs as root.** Bridging a whole site needs one capability, granted once by the service manager and
  dropped as soon as the network port is open. Joining a single Linux host needs one tap device you own, created
  once. `longwire shell` needs nothing at all.
- **It's quantum-safe.** The handshake is hybrid X25519 + ML-KEM-768 with post-quantum forward secrecy, the data is
  ChaCha20-Poly1305, and keys rotate every two minutes.
- **It's tiny, and native to [hibr](../hibr).** The engine is plain C99, built with **TCC**, with vendored audited
  crypto and nothing but libc. Everything you see and touch is a hibr script.

## Three components

| | Component | What it is | Ships through |
|---|---|---|---|
| 1 | **switchboard** (`longwire-switchboard`) | a hibr script: number allocation, tokens, call routing, limits | hibr's catalogue |
| 2 | **node** (`longwire`) | a hibr script: client and server in one. It is the CLI, the running node's brain (answer policy, state, events) and the desktop **Lines** pane | hibr's catalogue |
| 3 | **module** (`longwire.so`) | the hibr module: the whole engine in C. Crypto, sockets, NAT traversal, the switch, the ports. Runs in its own worker process | deb, rpm, pacman and Homebrew packages |

```
sudo apt install longwire        # the module: also dnf, pacman, brew
catalogue install longwire       # the node, from hibr's catalogue
longwire                         # registers with the public switchboard and prints your number
```

**C handles bytes and keys; hibr makes the decisions.** The engine's C never includes `hibr.h`, so the phone app
can reuse it later through JNI. That app will be a fourth piece, and it doesn't exist yet.

## Ways to attach

| Mode | For | One-time privilege | What happens |
|---|---|---|---|
| **splice** | a whole site (*the primary case*) | `AmbientCapabilities=CAP_NET_RAW` in the node's service unit | Frames go straight onto the wired NIC. **No interface is created.** |
| **jack** | one Linux/BSD host | `ip tuntap add mode tap user $USER` | The host gets a tap on the far subnet. |
| **shell** | one Linux user, no privilege at all | none (user namespaces) | Opens a shell whose network *is* the line. |
| **member** | phones, laptops on macOS/Windows | the OS's VPN prompt | longwire acts as an Ethernet host in userspace, so the device gets a real DHCP lease on the far LAN. |

## Documents

| | |
|---|---|
| [ARCHITECTURE](docs/ARCHITECTURE.md) | software-switch model, attach modes, forwarding, loops, MTU, the three components |
| [PROTOCOL](docs/PROTOCOL.md) | wire format, handshake, NAT traversal, relay, party lines |
| [CRYPTO](docs/CRYPTO.md) | primitives, handshake derivation, key schedule, measured cost |
| [THREAT-MODEL](docs/THREAT-MODEL.md) | adversaries, what's protected, what isn't |
| [SWITCHBOARD](docs/SWITCHBOARD.md) | numbers, registration, call setup, what it sees and never sees |
| [CLI](docs/CLI.md) | the `longwire` commands, and the module's `lw` builtin for your own scripts |
| [BUILD](docs/BUILD.md) | TCC constraints, vendoring, install, platforms |
| [ROADMAP](docs/ROADMAP.md) | milestones, from first frame to Android |
| [DECISIONS](docs/DECISIONS.md) | decision log, open questions included |
| [BRAND](docs/BRAND.md) | name, voice, vocabulary, logo, palette |

## License

GPL-3.0-or-later. See [COPYING](COPYING).

## Status

**M0 and M1 are built.** Nodes handshake with the hybrid X25519 + ML-KEM-768 exchange, bridge frames through the
learning switch and rekey under load (M0). A switchboard script hands out numbers under a token policy, nodes dial
each other through it, and two nodes behind separate NATs reach each other directly by hole punching, with pins and
a safety phrase (M1). Everything is driven from hibr scripts and verified by `make check` under both TCC and gcc.
Not yet: splice, hard-NAT traversal and relay, the desktop, the real tap port (untested here: no
`/dev/net/tun`). See [ROADMAP](docs/ROADMAP.md#m1-what-exists).

```
make && make check
```
