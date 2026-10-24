# Architecture

Anything in these documents that is designed but not built is marked `*(planned: M3)*` (the milestone that builds it,
see [ROADMAP](ROADMAP.md)). Everything else describes what the code does; `make check` compares the two.

## 1. The model: one switch, many ports

longwire is a **software Ethernet switch whose ports are encrypted UDP sessions**. All the nodes on one *line*
together make a single distributed learning switch:

- **Learn.** When a frame arrives, remember that its source MAC lives behind the port (the local port, or a peer) it
  came from. An entry moves at once when the MAC shows up somewhere else, and ages out after 300 s.
- **Forward.** A frame for a known MAC goes to exactly one place: the local port or one peer. A frame between two
  local MACs never leaves the site.
- **Flood.** Broadcast, multicast and unknown-unicast frames go to every peer on the line (and, from a peer, to the
  local port). If the peer a MAC was learned behind has gone, its frames are flooded again instead of dropped.
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

A call is between two nodes, and a node can be on many calls at once. When a call comes up the two ends find out
whether either holds a **line**, and a node that is called by a newcomer vouches for it, so calls accumulate into the full
mesh in the picture ([PROTOCOL §6](PROTOCOL.md#6-control-messages-and-lines), [ADR 0022](adr/0022-lines-form-by-vouching.md)).
A node is on one line of up to 16. A pair that cannot connect directly is relayed by another member
([PROTOCOL §5](PROTOCOL.md#5-relay-built-m3-stage-2)); if no member will relay, the pair splits the line, and the node says so.

## 2. Attaching a node: ports

Every node has exactly one **local port**, the place where frames enter and leave the line on this machine. The
modes differ only in what that port is:

| Port | Mechanism | Privilege | Notes |
|---|---|---|---|
| **splice** | `AF_PACKET` raw socket on a NIC (Linux) | `CAP_NET_RAW`, granted once to the node's service as an **ambient capability**, and dropped as soon as the socket is open | **No interface is created.** Wired Ethernet only: most Wi-Fi client links drop frames with foreign source MACs (4-address/WDS mode excepted). The splice box's *own* stack doesn't see injected frames, so if that box itself needs the far side, add a jack too. |
| **jack** | A tap device (`IFF_TAP \| IFF_NO_PI`) | Created once, owned by the user: `ip tuntap add mode tap user $USER` | Ordinary host membership. The tap's MTU should be 1400. *Written but untested: the machine this was built on has no `/dev/net/tun`.* |
| **sock** | frames as datagrams on a Unix socket | none | for tests and experiments only |
| **none** | no local port | none | what the switchboard uses: it never sees a frame |
| **shell** | A tap inside an unprivileged user + network namespace | **None** | *(planned: M5)* `longwire shell` hands you a shell whose only network is the line. |
| **member** | An L3 TUN from the OS: Android `VpnService`, macOS utun, Windows wintun | The OS VPN consent prompt | *(planned: M6)* See §5. |
| **BSD / macOS splice** | BPF device | access to `/dev/bpf*` | *(planned: M5)* |

**A node always runs as an ordinary user.** For splice, the service manager starts the node script with
`CAP_NET_RAW` as an ambient capability. `lw start --splice eth0` opens the packet socket, then clears *every* capability set (effective, permitted,
inheritable and ambient) and forks the engine worker, which inherits the empty sets; it also sets
`no_new_privs`. No script code after that holds any privilege, and hibr itself never gains one, in keeping with hibr's
ADR 0016 (see [ADR 0004](adr/0004-a-node-never-runs-as-root.md)). `longwire setup splice NIC` prints the service unit.

Splice details (Linux):
- `socket(AF_PACKET, SOCK_RAW, ETH_P_ALL)` bound to the interface. Promiscuous mode comes from
  `PACKET_ADD_MEMBERSHIP` / `PACKET_MR_PROMISC`, which the kernel releases when the socket closes, so nothing is left
  behind after a crash.
- **No loop.** `PACKET_IGNORE_OUTGOING` (Linux 4.20 and later) keeps the frames this box injects from being read back
  as local traffic, and the read path also discards anything the kernel marks as outgoing.
- **Unfinished checksums are completed.** A frame from a virtual NIC (veth, virtio) can arrive with its TCP/UDP
  checksum only partly done; the packet socket says so (`PACKET_AUXDATA`) and longwire finishes it before the frame
  goes anywhere else, or the far LAN would drop it.
- **Coalesced frames are cut.** See §6.
- Reading uses one `recvmsg` per frame; a `TPACKET_V3` ring and batching, for speed, *(planned: M7)*.

## 3. Keeping the WAN quiet

Bridging L2 over the internet carries the LAN's chatter. longwire trims it without breaking anything:

- **ARP suppression (IPv4).** Each node keeps an IP→MAC cache learned from ARP packets crossing the switch. When a
  local host asks who has an address the cache knows to be across the line (the MAC is learned behind a peer, and the
  entry is under 120 s old), the node answers the request itself and the question never crosses the WAN. Probes and
  announcements (sender address 0, or sender = target) always cross. IPv6 neighbour discovery *(planned: M3)*.
- **Flood budget.** Frames flooded to a peer are limited to 1000 a second after a burst of 200, per peer. A
  broadcast storm costs the WAN almost nothing; a unicast frame to a known address is never limited.
- **DHCP stays honest.** DHCP is not filtered: a line has one subnet and normally one DHCP server. The switch watches the DHCP offers
  that pass through it, and when two different servers have offered within ten minutes it says so once (the
  `dhcp-conflict` event, and the `dhcp_conflicts` count in status). It warns and changes nothing.

## 4. Two boxes on one segment: site groups

Split horizon prevents loops *between* sites. The remaining danger is **two splice boxes on the same physical LAN
segment**: each would forward the other's frames, and a loop follows. Boxes that share a secret avoid it by electing
one to forward:

- The secret is derived from the line key, so boxes of one line share it automatically (a rotated key is accepted
  alongside the previous one for 30 seconds, so an eviction doesn't flap a site). A file of 64 hex digits given with
  `--site-key FILE` overrides it, which is how two boxes work that are not on a line. A box with neither does not look
  for others, so two boxes on one segment **must** have one or the other.
- Every box sends a **probe** once a second: a broadcast frame with EtherType `0x88B5` (IEEE local experimental),
  carrying its key id, the time, and a MAC under the site secret. Probes are consumed locally and never forwarded
  over the line. A probe more than 30 s off the local clock is ignored.
- A box that has heard no other member for 3.5 seconds is alone. Among the boxes heard, the one with the **lowest key
  id is active**; the rest are **standby**. A standby box keeps its sessions up (so a takeover needs no handshake) but
  forwards nothing and delivers nothing to the LAN. A new box listens for 3 seconds before it forwards anything.
- The active box shares, every 2 seconds, the list of remote hosts it knows (up to 64 MACs). When a standby box takes
  over it sends one frame from each of those addresses onto the LAN, so every switch on the segment learns at once that
  they are now behind it, and then sends gratuitous ARP for the hosts it has seen ARP. Without this, the LAN would keep
  sending their traffic to the box that went quiet until its tables aged out.

In the test suite the active box is killed and traffic resumes, with nothing done to any host, in about 4 seconds
(the test allows up to 14). A takeover within two seconds of the group forming, before the first list has been shared, falls back to
ordinary learning and is slower.

## 5. Member mode: a phone as an Ethernet host *(planned: M6)*

Android and macOS give apps an IP tunnel, not Ethernet. longwire bridges the gap **in userspace**, on the device:

1. The device gets a stable, locally administered MAC derived from its key: `02:` + 5 bytes of `BLAKE2b(key-id)`.
2. A built-in **DHCPv4 client** sends its DISCOVER over the line. *The far LAN's own DHCP server hands out the
   lease*, so the phone gets a real address, gateway and DNS from home.
3. An **ARP responder** answers for that address, and **IPv6** runs SLAAC and an NDP responder.
4. IP packets from the OS tunnel are wrapped in Ethernet frames toward the learned gateway or peer MACs, and frames
   for our MAC are unwrapped back into the tunnel.
5. The tunnel gets the lease's address and, by default, only the line's subnet (*split*). `--all-traffic` also
   routes the default route through the far site's gateway.

Members, along with laptops in jack mode that ask for it, are **leaves**: a leaf keeps *one* session, to the node it
dialed, and that node treats the leaf like a host on its local port. One session means one keepalive, which a phone
battery appreciates. Sites are **trunks**, a full mesh.

## 6. Packet sizes

Overhead per data packet: IP (20) + UDP (8) + longwire header (12) + payload kind (1) + Poly1305 tag (16) = 57 bytes
over IPv4. The underlay is IPv4 only for now; IPv6 *(planned: M3)*.

| Situation | Handling |
|---|---|
| jack, sock | `longwire setup jack` prints the command that sets the tap's MTU to **1400**, so frames are at most 1414 bytes and fit one datagram. The engine does not enforce it: a larger frame is split like any other. A `sock` port has no MTU. |
| splice | LAN hosts send 1500-byte frames and we can't change them. So: **(1)** clamp the TCP MSS in SYN and SYN-ACK frames as they pass, to **1340** (IPv4) or **1320** (IPv6). A segment carrying 1340 bytes and the usual 12 bytes of TCP options is a 1406-byte frame, which travels in one datagram. **(2)** A frame up to **1428** bytes goes in one datagram (1428 + 29 + 28 = 1485, and 1489 once a relay wraps it, which fits PPPoE's 1492). A larger one is split across two datagrams (`FRAG`) and put back together on arrival; each peer holds four reassembly slots with a 2 s timeout. **(3)** A NIC can hand a splice box a *coalesced* TCP frame of up to 64 KB (GRO, or a virtual NIC's GSO), so oversize TCP frames are cut into line-sized segments with fresh lengths, sequence numbers and checksums; that is always valid TCP. Other oversize frames are dropped and counted. |
| path MTU | Probing the real path MTU per peer *(planned: M7)*. Until then the constants above assume 1492 or better. |

## 7. The three components

| | Component | Language | Owns |
|---|---|---|---|
| 1 | `longwire-switchboard` | hibr script | **policy**: who may register (tokens, a token reserving a number, a limit per source address) and whose ring goes through (a limit per callee per minute) |
| 2 | `longwire` (the node) | hibr script | the commands; the answer policy (ask, known, auto, never); turning events into sentences; setup helpers |
| 3 | `longwire.so` | C (hibr module) | **mechanism**: everything that touches bytes or keys, in a forked worker process: sockets, ports, handshake, sessions, the switch, NAT punching, the switchboard's registry and call routing, pins, cookies |

**C handles bytes and keys; hibr makes the decisions.** A script never sees a packet or a key, and it can make the
engine more restrictive but never less ([ADR 0015](adr/0015-scripts-decide-c-enforces.md)).

### Process model

```
 systemd / hold / a terminal
   └─ hibr longwire up                     node script: policy, UI
        │  lw start --splice eth0          ← opens the port, clears every capability, forks
        ├─ worker (C only)                 sockets, port, sessions, crypto, switch, NAT punching
        │    └─ serves ~/.longwire/ctl     control socket, mode 0600, peer uid checked
        └─ while read ev </dev/longwire/events     ring → answer policy → lw pickup / lw reject
                                                   key-changed, call-lost, hangup … → sentences

 another terminal:  longwire dial 312-884-076    → lw dial → control socket → worker
```

- **The worker is pure C, forked by the module** (the same shape as hibr's `hold` server). The packet path never
  enters the interpreter, so a busy script can't stall traffic. It closes every inherited descriptor and locks its key
  memory.
- **Every `lw` verb works from any hibr process,** through the control socket. That is how `longwire dial` in a
  second terminal reaches the running node.
- **Events** stream from the worker as JSON lines on `/dev/longwire/events` (see [CLI](CLI.md#events)).
  hibr parses them with `json parse`.
- **If the node script dies,** the worker keeps every established call up. A ring that nobody answers is refused
  after 60 s. A restarted script finds the worker still running and attaches to it (`up` says so), so a service manager can
  restart the script freely; `longwire down` stops the worker.
- **The switchboard has the same split.** `lw sb start` forks a C worker that handles sessions, handshakes, `HELLO`,
  cookies, heartbeats, presence, the number registry and call routing. Only two decisions reach the script, a
  registration from a key it hasn't seen and a ring, so a few events per call and none per heartbeat (see
  [SWITCHBOARD](SWITCHBOARD.md#how-tiny)).

### Source layout

```
engine/                      plain C99: never includes hibr.h, so the Android app can reuse it
  lw.h                       the engine's public API: configuration, open/serve, identity
  int.h                      internal structures and the function map between the files below
  proto.h                    message ids, candidates, the small codecs shared by node and switchboard
  words.h                    the 256 words of the safety phrase
  id.c                       identity from the seed, key derivation, handshake AEAD, the lw1... address
  hs.c                       the handshake, its fragments, the load budget and cookies
  sess.c                     sessions and the peer table: data packets, replay window, rekey, keepalive
  sw.c                       the switch: MAC table, flood, split horizon, frame split, MSS clamp, ARP, re-segmenting
  site.c                     site groups: probes, election, standby, takeover
  line.c                     lines: members, admits, eviction, persistence, the mesh maintainer
  relay.c                    relaying: the route table, wrapped datagrams, relay requests, path upgrade
  spray.c                    port spray: a strict NAT end opens many sockets, the easy end probes, the socket that is hit becomes that peer's
  pmp.c                      NAT-PMP: ask the home router to keep a port open, offered as a candidate
  port.c                     local ports: splice, jack, sock, none
  caps.c                     clears every capability set once the port is open
  ctl.c                      the control socket and the event stream
  call.c                     the node: numbers, HELLO, candidates, pins, the call table, punching, its commands
  board.c                    the switchboard worker
  node.c                     opening a node and the event loop
  all.c                      unity build of the files above
module/longwire.c            hibr glue: the `lw` builtin and the /dev/longwire/ scheme; forks the worker
scripts/longwire.hibr        component 2: the node
scripts/longwire-switchboard.hibr   component 1: the switchboard
tests/                       end-to-end tests (m0, m1, m2), shared helpers (lwtest.py), the documentation check (docs.py)
vendor/monocypher/           X25519, Ed25519, ChaCha20-Poly1305, BLAKE2b
vendor/mlkem768/             PQClean ML-KEM-768 "clean" + fips202, with a 5-line compat shim for TCC
brand/                       the mark and wordmark
docs/                        these documents
control-panel/lines.hibr     *(planned: M4)* the Network pane's Lines section, shipped as a `# part:` of the node app
packaging/                   *(planned: M4)* deb, rpm, arch and homebrew builds, the launchers, the systemd unit
```

When the Android app comes (M6), it builds `engine/` with TCC for arm64 into a library behind a small Kotlin `VpnService`. That is the
only place the engine is used without hibr.

**The control socket.** The worker owns the sockets, the port and all keys. The node script, `longwire` commands
in other terminals and the desktop pane all reach it through `lw` verbs, which travel over a Unix socket (mode 0600
in `~/.longwire/`). A request is one text line; the reply is one JSON object (put in `$RET`), or, after `watch`, the
connection stays open and carries events. Keys never cross the control socket.

**State on disk (`~/.longwire/`).** This isn't configuration, just state:

| File | Contents |
|---|---|
| `seed` | 32 bytes, mode 0600. *This is your identity and your number.* |
| `known` | pins: switchboard key id, number, key id, one line each; written when a call comes up, edited by `longwire unpin` |
| `lines` | the line this node is on: its id, founder, line key (sealed under the seed), the node's own admit, the members, and the evictions |
| `ctl` | the control socket |
| `switchboard/numbers.log` | on a switchboard: every number issued, with its key id and bundle |
| `switchboard/codes.log` | on a registry: which switchboard key holds which short code |
| `switchboard/names.log` | on a switchboard: which key holds each listed name (append-only, the last line for a key wins) |
| `switchboard/seed`, `switchboard/ctl` | the switchboard's own identity and control socket |
