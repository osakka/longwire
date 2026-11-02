# longwire

> **One long wire between your sites.** Same LAN, any distance.

longwire stretches one Ethernet segment across the internet. You run it at each site, dial a number, and the LANs
become one subnet. A printer in Lisbon answers ARP in Osaka. The DHCP server in your basement hands an address to a
machine at the other end. Anything designed but not built carries a marker naming its milestone, like `*(planned: M5)*`; the milestones are in the
[roadmap](docs/ROADMAP.md).

```
$ longwire up --switchboard 203.0.113.7:6741/lw1q7m…k2a --splice eth0
☎  your number 447-120-933 — dial tone
✓  connected to 312-884-076 (direct, 198.51.100.4:41641)
   safety phrase: gopher daisy castle cactus island flint harbor maple   Compare it with theirs now; hang up if it differs

$ longwire dial 312-884-076          # in another terminal
{"call":"dialing","number":"312-884-076"}
```

(A site box runs this as the service `longwire@eth0`, which holds the one capability splice needs: [OPERATING](docs/OPERATING.md).)

The examples write `longwire`. Install the package from the hibr repositories (`please apt install longwire`, or `please dnf install longwire`,
`please pacman -S longwire`), then `catalogue install longwire` for the command, the Control Panel pane and the menu bar telephone
(`catalogue --system install cli/longwire` on a server with no desktop); or run `hibr scripts/longwire.hibr` with `LONGWIRE_MOD` pointing at
the built module ([BUILD](docs/BUILD.md)).

## What it is

- **Layer 2, not layer 3.** longwire carries Ethernet frames, so ARP, DHCP, mDNS and everything else on your LAN
  works across sites. The line itself needs no route tables, no second subnet and no IP plan; a single machine can still reach
  networks behind a router at the far end, or send all its traffic out there ([QUICKSTART](docs/QUICKSTART.md#beyond-the-bridged-subnet)).
- **Every node is equal.** The same node script is a site bridge, a laptop and a switchboard's client. A client *is* a
  server: each node is a port on one software switch.
- **You dial a number.** Every node gets a short number from the **switchboard**, a tiny rendezvous server. It
  introduces two nodes, coordinates a NAT hole-punch, then **hangs up**. Traffic never passes through it.
- **Two sites behind ordinary NATs connect directly,** and any number of sites form one **line**: call one member and the
  rest of the mesh builds itself. Two *strict* (symmetric) NATs go through another node of your own line with a public address, still end-to-end
  encrypted, and move to a direct path as soon as one exists.
- **It never runs as root.** Bridging a whole site needs one capability, granted once by the service manager and
  dropped as soon as the network port is open. Joining a single Linux host needs one tap device you own, made by a small root helper after one password (again after a
  restart), with its address and routes.
- **Its key exchange is quantum-resistant.** The handshake is hybrid X25519 + ML-KEM-768, so a quantum computer would have to break ML-KEM-768 as
  well as X25519 to read traffic recorded today; the data is ChaCha20-Poly1305 and keys rotate every two minutes. The signatures that vouch for line membership
  (Ed25519) are not post-quantum, the handshake has had no formal analysis or outside review *(planned: M8)*, and none of this is a claim to be
  "ultra safe": [SECURITY](docs/SECURITY.md) says exactly what is and is not protected and checked.
- **It's tiny, and native to hibr.** The engine is plain C99, built with **TCC**, with vendored, widely used crypto (Monocypher and
  PQClean's ML-KEM-768) and nothing but libc. Everything you see and touch is a hibr script.

## Three components

| | Component | What it is | Ships through |
|---|---|---|---|
| 1 | **switchboard** (`longwire-switchboard`) | a hibr script: who may register, whose ring goes through | hibr's catalogue (`cli/longwire-switchboard`) |
| 2 | **node** (`longwire`) | a hibr script: client and server in one: the commands, the answer policy, the events as sentences | hibr's catalogue (`cli/longwire`) |
| 3 | **module** (`longwire.so`) | the hibr module: the whole engine in C. Crypto, sockets, NAT punching, the switch, the ports. Runs in its own worker process | deb, rpm and pacman packages from the hibr repositories; Homebrew *(planned: M5)* |

**C handles bytes and keys; hibr makes the decisions.** The engine's C never includes `hibr.h`, so the phone app can
reuse it later through JNI. That app will be a fourth piece *(planned: M6)*. The module ships as deb, rpm and pacman packages in the hibr
repositories, and the scripts, the pane and the menu bar telephone through hibr's catalogue; `make` builds the same from source ([BUILD](docs/BUILD.md)).

## Ways to attach

| Mode | For | One-time privilege | What happens |
|---|---|---|---|
| **splice** | a whole site (*the primary case*) | `AmbientCapabilities=CAP_NET_RAW` in the node's service unit | Frames go straight onto the wired NIC. **No interface is created.** |
| **jack** | one Linux host | `longwire jack` (a root helper makes the tap after one password; or by hand, `ip tuntap add mode tap user $USER`) | The host gets a tap on the far subnet, with an address from the profile or a DHCP server on the line, routes beyond it, or everything through it. |
| **shell** | one Linux user, no privilege at all | none (user namespaces) | A shell whose network *is* the line *(planned: M5)* |
| **member** | phones, laptops on macOS and Windows | the OS's VPN prompt | longwire acts as an Ethernet host in userspace, so the device gets a real DHCP lease on the far LAN *(planned: M6)* |

## Using it

| | |
|---|---|
| [QUICKSTART](docs/QUICKSTART.md) | two machines on one line in ten minutes, on the desktop or the command line |
| [DESKTOP](docs/DESKTOP.md) | the graphical side: the Control Panel pane, the menu bar telephone, the Control Strip, the Switchboard window |
| [OPERATING](docs/OPERATING.md) | run a switchboard, run a site as a service, back up your identity, upgrade |
| [TROUBLESHOOTING](docs/TROUBLESHOOTING.md) | a message or a symptom, its cause, and the fix |
| [SECURITY](docs/SECURITY.md) | what longwire protects, what "quantum-safe" does and does not mean here, what has been checked, what has not |
| manual pages | `man longwire`, `man longwire-switchboard`, `man longwire-profiles`, `man longwire-security` |

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

**M0 to M3 are built, and most of M4 (the desktop and distribution):** profiles, the Control Panel pane, the menu bar telephone and the Control Strip, the
packages and the catalogue entries are published. A jack takes its address from the profile or a DHCP server on the line, reaches networks
behind a gateway, and can send everything through the line, letting go when the machine changes network; the safety phrase is committed to before it is
shown, and the desktop shows it; a switchboard strikes and bans keys that keep failing, says so when it is silent, and its window shows traffic and bans. **M0, M1, M2 and M3** are built with TCC and nothing else and tested end to end: the
handshake and its cookies, the switchboard, calls through simulated NATs, splice carrying real ping, ARP, TCP and
broadcasts between LANs that are one subnet, a second box on a segment taking over when the first is killed, lines of
up to 16 sites that join by number (with an operator, if the line has one), build their own mesh with no prompt, evict a member, and heal after a restart, and nodes behind strict NATs, relays or IPv6. The jack has carried real calls between the author's desktop and a site
node; routes, everything through the line and a laptop moving between networks have not run on a real tap, and real routers and wide-area links beyond
that are untested. See the [roadmap](docs/ROADMAP.md).

```
make && make check
```

## License

GPL-3.0-or-later. See [COPYING](COPYING).
