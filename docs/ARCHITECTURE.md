# Architecture

## 1. The model: one switch, many ports

longwire is a **software Ethernet switch whose ports are encrypted UDP sessions**. All the nodes on one *line*
together make a single distributed learning switch:

- **Learn.** When a frame arrives from a peer, remember that its source MAC lives behind that peer.
- **Forward.** A frame for a known MAC goes to exactly one place: the local port or one peer.
- **Flood.** Broadcast, multicast and unknown-unicast frames go to every peer on the line and to the local port.
- **Split horizon.** A frame that arrived *from a peer* is never sent on to another peer. Sites form a full mesh,
  so every peer already received it directly. This rule is what removes the need for spanning tree.

There is no routing protocol, no address plan and no controller. "A client is a server" because there are no
clients or servers here, only ports.

```
         site A                                  site B                          site C
  ┌───────────────────┐                   ┌───────────────────┐          ┌───────────────────┐
  │ LAN 10.0.0.0/24   │                   │ LAN 10.0.0.0/24   │          │ LAN 10.0.0.0/24   │
  │  hosts ── eth0    │                   │  eth0 ── hosts    │          │  eth0 ── hosts    │
  │           │       │                   │    │              │          │    │              │
  │     [longwire]════╪═══ encrypted UDP ═╪═[longwire]════════╪══════════╪═[longwire]        │
  │      MAC table    │                   │   MAC table       │          │   MAC table       │
  └─────────╫─────────┘                   └───────────────────┘          └─────╫─────────────┘
            ╚═══════════════════════════ full mesh ════════════════════════════╝
                    ▲                         ▲                                 ▲
                    └──────── register / dial / punch only ─────────────────────┘
                                         [switchboard]
```

## 2. Attaching a node: ports

Every node has exactly one **local port**, the place where frames enter and leave the line on this machine. The
modes differ only in what that port is:

| Port | Mechanism | Privilege | Notes |
|---|---|---|---|
| **splice** | `AF_PACKET` raw socket on a physical NIC (Linux). BPF device on BSD/macOS. | `CAP_NET_RAW`, granted once to the node's service as an **ambient capability**, and dropped as soon as the socket is open | **No interface is created.** Wired Ethernet only: most Wi-Fi client links drop frames with foreign source MACs (4-address/WDS mode excepted). The splice host's *own* stack doesn't see injected frames, so if that box itself needs the far side, add a jack too. |
| **jack** | A tap device (`IFF_TAP \| IFF_NO_PI`) | Created once, owned by the user: `ip tuntap add mode tap user $USER`. On FreeBSD, `net.link.tap.user_open=1`. | Ordinary host membership. MTU 1400. |
| **shell** | A tap inside an unprivileged user + network namespace | **None** | The UDP socket is opened in the host namespace *first*, then `unshare(CLONE_NEWUSER\|CLONE_NEWNET)` and the tap is made inside. `longwire shell` hands you a shell whose only network is the line. Some distros (Ubuntu 24.04+) restrict unprivileged user namespaces with AppArmor; that is detected and reported. |
| **member** | An L3 TUN from the OS: Android `VpnService`, macOS utun / NetworkExtension, Windows wintun | The OS VPN consent prompt | See §5. The device appears on the far LAN as a real Ethernet host. |

**A node always runs as an ordinary user.** For splice, the service manager starts the node script with
`CAP_NET_RAW` as an ambient capability. The script's first act is `lw start --splice eth0`. That call opens the
packet socket, forks the engine worker, and then clears *every* capability set (effective, permitted, inheritable
and ambient) in both processes. No script code after that line holds any privilege, and hibr itself never gains
one, which is in keeping with hibr's ADR 0016 (see [DECISIONS D4](DECISIONS.md#d4)).

Splice details (Linux):
- `socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))` bound to the interface. Promiscuous mode comes from
  `PACKET_ADD_MEMBERSHIP` / `PACKET_MR_PROMISC`, which is reference-counted and cleared automatically when the
  socket closes, so nothing is left behind on a crash.
- `PACKET_IGNORE_OUTGOING` (Linux ≥ 4.20) keeps frames we inject from being read back as local traffic.
- Receive ring `TPACKET_V3` (mmap) for low per-packet cost; plain `recvfrom` as the fallback.
- Frames between two *local* MACs are dropped at once, since they never need to leave the site. A small classic BPF
  prefilter drops the noisiest local-only traffic (e.g. 802.3x pause, LLDP) in the kernel.

## 3. Keeping the WAN quiet

Bridging L2 over the internet carries the LAN's chatter. longwire trims it without breaking anything:

- **ARP / IPv6 ND suppression.** Each node keeps an IP→MAC cache learned from ARP replies and NAs crossing the line.
  When a local host ARPs for a remote IP that the cache knows, the local node answers on the remote host's behalf,
  and the request never crosses the WAN. Unknown entries flood as usual.
- **Broadcast/multicast rate limit** per peer: default 1,000 pps with a burst of 200, which kills a broadcast storm
  before it costs anything.
- **DHCP stays honest.** DHCP isn't filtered. A line has one subnet, and normally one DHCP server. If two sites each
  run one, `longwire status` warns: "2 DHCP servers seen on line 7f3a9c". Silencing either one is a config choice we
  don't make for you.
- **MAC table aging** at 300 s. Entries move at once when a MAC appears at a new port (a laptop walking between
  sites).

## 4. Loops and high availability

Split horizon prevents loops *between* sites. The remaining danger is **two splice nodes on the same physical
segment** (two boxes at one site, or a site cabled to itself). longwire detects this and turns it into a feature:

- Every splice node sends a **line probe** onto its local segment once per second. It's a small frame with IEEE
  local-experimental EtherType `0x88B5`, carrying the line ID, the node's key ID and a MAC under the line key.
- A node that hears a valid probe from *another member of its own line* on its *local* port knows they share a
  segment. They form a **site group**: the lowest key ID is **active**, and the others are **standby** (connected
  and keyed, but not forwarding frames).
- After 3 missed probes, the next standby takes over and sends gratuitous ARP for the MACs it learned, so failover
  takes about 3 seconds. Two boxes at a site give you HA for free.

## 5. Member mode: a phone as an Ethernet host

Android and macOS give apps an IP tunnel, not Ethernet. longwire bridges the gap **in userspace**, on the device:

1. The device gets a stable, locally administered MAC derived from its key: `02:` + 5 bytes of `BLAKE2b(key-id)`.
2. A built-in **DHCPv4 client** (~300 lines) sends its DISCOVER over the line. *The far LAN's own DHCP server hands
   out the lease*, so the phone gets a real address, gateway and DNS from home.
3. An **ARP responder** answers for that address, and **IPv6** runs SLAAC + an NDP responder.
4. IP packets from the OS tunnel are wrapped in Ethernet frames toward the learned gateway/peer MACs, and frames for
   our MAC are unwrapped back into the tunnel.
5. The tunnel gets the lease's address and, by default, only the line's subnet (*split*). `--all-traffic` also
   routes the default route through the far site's gateway.

Members, along with laptops in jack mode that ask for it, are **leaves** by default. A leaf keeps *one* session, to
the node it dialed, and that node treats the leaf like a host on its local port: it forwards the leaf's frames to
the rest of the line and floods line traffic to the leaf. One session means one keepalive, which a phone battery
appreciates. Sites are always full-mesh **trunks**.

## 6. MTU

Overhead per data packet: IP (20/40) + UDP (8) + longwire header (12) + payload kind (1) + Poly1305 tag (16).

| Situation | Handling |
|---|---|
| jack, shell, member | The interface MTU is set to **1400**, which is safe over IPv4, IPv6 and PPPoE. |
| splice | LAN hosts send 1500-byte frames and we can't change them. So: **(1)** clamp the TCP MSS in SYN/SYN-ACK frames as they pass, to **1340**. That covers the worst common case, IPv6 inside IPv6 over PPPoE: 1492 − 40 − 8 − 29 = 1415-byte frame, 1401-byte IP packet, 60 bytes of IPv6+TCP headers, so an MSS of 1341. It fixes nearly all real traffic at almost no cost. **(2)** Any frame still too large is split across two datagrams by longwire itself and reassembled (with a 64-entry reassembly table and a 2 s timeout). Jumbo frames are refused and counted. |
| path MTU | Probed per peer at session start (1280 → 1500 binary search with padded keepalives) and re-probed when packet loss suggests a black hole. |

## 7. The three components

| | Component | Language | Owns |
|---|---|---|---|
| 1 | `longwire-switchboard` | hibr script | registration policy (number allocation, tokens, release), call routing (`DIAL` → `RING` → `ANSWER` → `GO`), abuse limits, logging policy |
| 2 | `longwire` (the node) | hibr script | the CLI; the running node's brain (answer policy, pins, lines, events); setup helpers; the desktop **Lines** pane |
| 3 | `longwire.so` | C (hibr module) | everything that touches bytes or keys, in a forked worker process |

**C handles bytes and keys; hibr makes the decisions.** A script never sees a packet or a key. The engine never
decides who is allowed in.

### Process model

```
 systemd / hold / a terminal
   └─ hibr longwire up                     node script: policy, state, UI
        │  lw start --splice eth0          ← opens the port, forks, drops all capabilities
        ├─ worker (C only)                 sockets, port, sessions, crypto, switch, NAT, relay
        │    └─ serves ~/.longwire/ctl     control socket, mode 0600, peer uid checked
        └─ while read ev </dev/longwire/events     ring → answer policy → lw pickup / lw reject
                                                   key-changed, path, hangup → state, desktop

 another terminal:  longwire dial 312-884-071    → lw dial → control socket → worker
```

- **The worker is pure C, forked by the module** (the same shape as hibr's `hold` server). The packet path never
  enters the interpreter, so a busy script or a busy desktop can't stall traffic. Throughput is the engine's, about
  290 Mbit/s per core under TCC.
- **Every `lw` verb works from any hibr process.** In the process that started the worker, a verb talks to the
  worker directly; anywhere else it goes through the control socket. That's how `longwire dial` in a second
  terminal, or the desktop pane, reaches the running node.
- **Events** (`ring`, `connected`, `path`, `key-changed`, `hangup`) stream from the worker as JSON lines on
  `/dev/longwire/events`. hibr parses them into maps with `json parse`.
- **If the node script dies,** the worker keeps every established call up. New rings are rejected after 60 s until
  the script comes back, and its service manager restarts it.
- **The switchboard has the same split.** `lw sb start` forks a C worker that handles sessions, handshakes,
  `HELLO`, cookies, heartbeats, presence and the bundle store. Only registration and call-setup messages
  reach the script, which makes the decisions. That's a few per second per call, never per heartbeat (see
  [SWITCHBOARD](SWITCHBOARD.md#how-tiny)).

### Source layout

```
engine/                      plain C99 — never includes hibr.h, never allocates on the data path
  lw.h                       engine API (opaque lw_node*, event callbacks) — what the module and, later, JNI call
  ev.c                       poll/epoll loop, timers; single-threaded in v1
  wire.c                     packet encode/decode — the only code that parses network input
  hs.c                       handshake state machine (see CRYPTO)
  sess.c                     sessions, counters, replay window, rekey
  sw.c                       the switch: MAC table, flood, split horizon, ARP/ND cache, rate limits
  nat.c                      candidates, punch scheduler, port-spray, path upgrades
  relay.c                    relay routes
  line.c                     line membership, admits, line key, probes, site groups
  member.c                   L3↔L2 adapter: DHCP client, ARP/ND responder
  sbc.c                      switchboard client
  sb.c                       switchboard worker: sessions, presence, heartbeats, bundle store
  ctl.c                      control socket (~/.longwire/ctl), text requests, JSON replies, event stream
  caps.c                     drop every capability set after the port is open
  port_splice_linux.c  port_jack_linux.c  port_shell_linux.c  port_bpf.c  port_tun.c
module/longwire.c            hibr glue: the `lw` builtin, /dev/longwire/ scheme, results as maps, forks the worker
scripts/longwire.hibr        component 2: the node (hibr catalogue app)
scripts/longwire-switchboard.hibr   component 1: the switchboard (hibr catalogue app)
control-panel/lines.hibr     the Network pane's Lines section, shipped as a `# part:` of the node app
packaging/                   deb, rpm, arch, homebrew (as hibr's), launchers in bin/, systemd/longwire@.service
vendor/monocypher/           X25519, Ed25519, ChaCha20-Poly1305, BLAKE2b (pinned release)
vendor/mlkem768/             PQClean ML-KEM-768 "clean" + fips202, with a 5-line compat shim for TCC
```

When the Android app comes ([ROADMAP](ROADMAP.md) M6), it compiles `engine/` with the NDK behind a small Kotlin
`VpnService`. That's the only place the engine is used without hibr.

**The control socket.** The worker owns the sockets, the port and all keys. The node script, `longwire` commands
in other terminals and the desktop pane all reach it through `lw` verbs, which travel over a Unix socket (mode 0600
in `~/.longwire/`). Requests are one text line; replies are one JSON object, which the module turns into a map in
`$RET`. Keys never cross the control socket.

**State on disk (`~/.longwire/`).** This isn't configuration, just state:

| File | Contents |
|---|---|
| `seed` | 32 bytes, mode 0600. *This is your identity and your number.* |
| `known` | pinned peers: number → key ID, first seen, policy |
| `lines` | lines this node belongs to, its role, and the line key (encrypted under the seed) |
| `ctl` | the control socket |
