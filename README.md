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
  restart, or never, if the administrator allows the device with none), with its address and routes; sending the whole machine's traffic through the line is a permission of its own.
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

**C handles bytes and keys; hibr makes the decisions.** The engine's C never includes `hibr.h`, so the phone app reuses it through JNI: that app is a fourth piece, built and run on a phone ([ANDROID](docs/ANDROID.md)). The module ships as deb, rpm and pacman packages in the hibr
repositories, and the scripts, the pane and the menu bar telephone through hibr's catalogue; `make` builds the same from source ([BUILD](docs/BUILD.md)).

## Ways to attach

| Mode | For | One-time privilege | What happens |
|---|---|---|---|
| **splice** | a whole site (*the primary case*) | `AmbientCapabilities=CAP_NET_RAW` in the node's service unit | Frames go straight onto the wired NIC. **No interface is created.** |
| **jack** | one Linux host | `longwire jack` (a root helper makes the tap after one password; or by hand, `ip tuntap add mode tap user $USER`) | The host gets a tap on the far subnet, with an address from the profile or a DHCP server on the line, routes beyond it, or everything through it, out by a member that shares its internet. |
| **shell** | one Linux user, no privilege at all | none (user namespaces) | A shell whose network *is* the line *(planned: M5)* |
| **member** | a phone (Android: built); macOS and Windows *(planned: M6)* | the OS's VPN prompt | longwire acts as an Ethernet host in userspace, so the device gets a real DHCP lease on the far LAN ([ANDROID](docs/ANDROID.md)) |

## Using it

| | |
|---|---|
| [QUICKSTART](docs/QUICKSTART.md) | two machines on one line in ten minutes, on the desktop or the command line |
| [ANDROID](docs/ANDROID.md) | the phone as a member of a line: build, sign, install, use; what is not done |
| [DESKTOP](docs/DESKTOP.md) | the graphical side: the Control Panel pane, the menu bar telephone, the Control Strip, the Switchboard window |
| [BACKLOG](docs/BACKLOG.md) | what is open, in the order it is done, one ticket at a time |
| [IANA](docs/IANA.md) | the application for a service name and a port number, ready to submit, with what the owner must add |
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

The releases are 0.5.x; the [CHANGELOG](CHANGELOG.md) says what each carried and the [BACKLOG](docs/BACKLOG.md) what is open. In short:

- **Built with TCC and tested end to end** (the full suite, `make check`, on real kernels in network namespaces): the handshake and its cookies; the switchboard (numbers, tokens, directory, bans); calls through simulated NATs, strict NATs and relays; splice carrying real ping, ARP, TCP and broadcasts between LANs that are one subnet;
  lines of up to 16 sites with an operator, evictions, rolls and expiring admits; **two boxes on one segment as one site** (one active, one standby, a takeover in seconds, a second box joining through a real switchboard while a host broadcasts, in both key orders); what a bridge never carries
  (spanning tree, LLDP and CDP stay on their own wire) and a loop between two forwarding boxes stopped even if the election fails; **a DHCP guard** (only a member you asked for may be the network's server or router); the jack's address and the whole-line options; exits; names; dialling by name from the address book
  (hibr 0.99.553 or later); and the Switchboard window, which shows traffic, bans and the pairs.
- **The phone** (Android, [ANDROID](docs/ANDROID.md)): the engine as a library built by TCC, an adapter that gives the phone a real DHCP lease from the far LAN, and a Java app around it with invitation links, backup numbers and give-up limits. The adapter and the embedded engine are tested on Linux, on a real line; the app has been **run on the
  owner's phone** (connected through the line, the screens and the notification as built up to 0.5.68), and its backup numbers and give-up limits, its battery saving, its behaviour across a change from wifi to mobile data, and its look on other phones have **not been tried or measured**.
- **Not built or not verified**: IPv6 on the phone; the leaf role (one session); the seed in the Android Keystore; macOS and Windows members; the formal analysis of the handshake and an outside review *(planned: M8)*; a real exit on the internet and routes on a real tap end to end; wide-area links beyond those tried; the IANA registration of the port
  ([IANA](docs/IANA.md), drafted). See the [roadmap](docs/ROADMAP.md) and the [backlog](docs/BACKLOG.md).

```
make && make check
```

## License

GPL-3.0-or-later. See [COPYING](COPYING).
