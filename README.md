# longwire

> **One long wire between your sites.** Same LAN, any distance.

longwire stretches one Ethernet segment across the internet. You run it at each site, dial a number, and the LANs
become one subnet. A printer in Lisbon answers ARP in Osaka. The DHCP server in your basement hands an address to a
machine at the other end. Anything designed but not built carries `*(planned: M3)*`; the milestones are in the
[roadmap](docs/ROADMAP.md).

```
$ longwire up --switchboard 203.0.113.7:1877/lw1q7m…k2a --splice eth0
☎  your number 447-120-933 — dial tone

$ longwire dial 312-884-076
{"call":"dialing","number":"312-884-076"}
✓  connected to 312-884-076 (direct, 198.51.100.4:41641)
   safety phrase: gopher daisy castle cactus island flint
```

The examples write `longwire`. Until the launcher is packaged *(planned: M4)* that is `hibr scripts/longwire.hibr` with
`LONGWIRE_MOD` pointing at the built module ([BUILD](docs/BUILD.md)).

## What it is

- **Layer 2, not layer 3.** longwire carries Ethernet frames, so ARP, DHCP, mDNS and everything else on your LAN
  works across sites. There are no route tables, no second subnet and no IP plan.
- **Every node is equal.** The same node script is a site bridge, a laptop and a switchboard's client. A client *is* a
  server: each node is a port on one software switch.
- **You dial a number.** Every node gets a short number from the **switchboard**, a tiny rendezvous server. It
  introduces two nodes, coordinates a NAT hole-punch, then **hangs up**. Traffic never passes through it.
- **Two sites behind ordinary NATs connect directly,** and any number of sites form one **line**: call one member and the
  rest of the mesh builds itself. Two *strict* (symmetric) NATs go through another node of your own line with a public address, still end-to-end
  encrypted, and move to a direct path as soon as one exists.
- **It never runs as root.** Bridging a whole site needs one capability, granted once by the service manager and
  dropped as soon as the network port is open. Joining a single Linux host needs one tap device you own, created once.
- **It's quantum-safe.** The handshake is hybrid X25519 + ML-KEM-768, the data is ChaCha20-Poly1305, and keys rotate
  every two minutes. The composition has not yet been formally analysed *(planned: M8)*.
- **It's tiny, and native to hibr.** The engine is plain C99, built with **TCC**, with vendored audited
  crypto and nothing but libc. Everything you see and touch is a hibr script.

## Three components

| | Component | What it is | Ships through |
|---|---|---|---|
| 1 | **switchboard** (`longwire-switchboard`) | a hibr script: who may register, whose ring goes through | hibr's catalogue *(planned: M4)* |
| 2 | **node** (`longwire`) | a hibr script: client and server in one: the commands, the answer policy, the events as sentences | hibr's catalogue *(planned: M4)* |
| 3 | **module** (`longwire.so`) | the hibr module: the whole engine in C. Crypto, sockets, NAT punching, the switch, the ports. Runs in its own worker process | deb, rpm, pacman and Homebrew packages *(planned: M4)* |

**C handles bytes and keys; hibr makes the decisions.** The engine's C never includes `hibr.h`, so the phone app can
reuse it later through JNI. That app will be a fourth piece *(planned: M6)*. Today the module is built from source
(`make`) and the scripts run from this repository ([BUILD](docs/BUILD.md)).

## Ways to attach

| Mode | For | One-time privilege | What happens |
|---|---|---|---|
| **splice** | a whole site (*the primary case*) | `AmbientCapabilities=CAP_NET_RAW` in the node's service unit | Frames go straight onto the wired NIC. **No interface is created.** |
| **jack** | one Linux host | `ip tuntap add mode tap user $USER` | The host gets a tap on the far subnet. Written, but untested here. |
| **shell** | one Linux user, no privilege at all | none (user namespaces) | A shell whose network *is* the line *(planned: M5)* |
| **member** | phones, laptops on macOS and Windows | the OS's VPN prompt | longwire acts as an Ethernet host in userspace, so the device gets a real DHCP lease on the far LAN *(planned: M6)* |

## Documents

| | |
|---|---|
| [ARCHITECTURE](docs/ARCHITECTURE.md) | the software-switch model, attach modes, site groups, packet sizes, the three components, the source layout |
| [PROTOCOL](docs/PROTOCOL.md) | wire format, handshake and cookies, NAT traversal, relay and lines |
| [CRYPTO](docs/CRYPTO.md) | primitives, handshake derivation, key schedule, measured cost |
| [THREAT-MODEL](docs/THREAT-MODEL.md) | adversaries, what's protected, what isn't |
| [SWITCHBOARD](docs/SWITCHBOARD.md) | numbers, registration, call setup, what it sees and never sees |
| [CLI](docs/CLI.md) | every command, the answer policies, status and events, the `lw` builtin for your own scripts |
| [BUILD](docs/BUILD.md) | building and running, the tests, TCC constraints, vendoring, platforms |
| [ROADMAP](docs/ROADMAP.md) | what is built and what each coming milestone adds |
| [decisions](docs/adr/README.md) | one record per decision, in the order taken |
| [CHANGELOG](CHANGELOG.md) | what changed, per milestone |
| [BRAND](docs/BRAND.md) | name, voice, vocabulary, logo, palette |

## Status

**M0, M1 and M2, and the first stage of M3, are built** and tested end to end, built with TCC and nothing else: the
handshake and its cookies, the switchboard, calls through simulated NATs, splice carrying real ping, ARP, TCP and
broadcasts between LANs that are one subnet, a second box on a segment taking over when the first is killed, and lines of
up to 16 sites that join by number, build their own mesh with no prompt, evict a member, and heal after a restart. The jack port, real
routers and real wide-area links are untested. See the [roadmap](docs/ROADMAP.md).

```
make && make check
```

## License

GPL-3.0-or-later. See [COPYING](COPYING).
