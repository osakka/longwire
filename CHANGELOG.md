# Changelog

What changed, newest first. The reasons are in [docs/adr](docs/adr/README.md); what is still to come is in
[docs/ROADMAP.md](docs/ROADMAP.md). `git log` is the full history.

## M3, stage 2: relaying and path upgrade

- **Relay** ([ADR 0008](docs/adr/0008-hard-nats-are-relayed-by-line-members.md), [PROTOCOL §5](docs/PROTOCOL.md#5-relay-built-m3-stage-2)):
  when two direct tries to a line member fail, a node asks a connected member to carry the pair's packets. The pair
  runs its own handshake and session through the relay, which sees only sizes and timing; a relay is a member with a
  public address (`--relay auto`), forwards by 24-bit route id after checking the sender's address, and holds 64 routes
  that expire after 60 s idle. The switchboard still never relays.
- **Path upgrade:** a relayed peer calls its member again through the switchboard (`--direct-retry`, 60 s doubling to
  10 min); when the punch lands, session traffic goes to the punched address as well, and the first authenticated datagram
  straight from the other end drops the relay. Event `path`; `connected` has `path` `relay` and `via`.
- **Behind it:** every datagram to a peer now goes through one function that knows the peer's path; a datagram that came
  out of a relay never moves a peer's address; `NODE_STATUS` gains `relay` and each peer's `path`; the packet dispatcher
  became a function of its own. `FRAME_SINGLE` went from 1432 to 1428 so a wrapped full frame still fits a PPPoE link.
- **Per-source `HELLO` limit:** 2 a second (burst 4) from any one address, on top of the global 20 a second.
- **Crossed handshakes:** `tests/crossed.sh` starts both ends at once behind a delaying proxy; the link comes up and keeps
  rekeying. A lower-key-id tie-break was tried and dropped: it made that case worse, and no stall could be reproduced.
- **`longwire who NUMBER`:** what the node knows of a number (full address, pin, line membership, path, safety phrase). It is
  local and asks the switchboard nothing; `LOOKUP` and dialing a full address stay planned.
- **Tests:** `tests/m3b.sh`: two members behind symmetric NATs and a public third; frames (and full-size, fragmented ones)
  cross the relay, the NATs are loosened, the pair goes direct, then the relay is shut down and the pair carries on. The
  NAT simulator moved to `tests/lwnat.py`.

## M3, stage 1: lines of more than two nodes

- **Lines** ([ADR 0022](docs/adr/0022-lines-form-by-vouching.md)): up to 16 nodes on one L2 segment, joined by number.
  A node joins by calling any member, which vouches for it with a signed (Ed25519) admit and hands it the line; the
  newcomer then calls every other member, presenting the admit, and a member that holds a valid admit lets it through
  without a prompt under every answer policy but `never`. The founder admits itself. An admit is valid if its signer is in
  the *persisted* member list and it is newer than any eviction of that key.
- **Eviction:** `longwire evict NUMBER` signs and sends an eviction, drops the member everywhere, rotates the line key and
  bars the key from ringing back in. Lines persist (`~/.longwire/lines`, the key sealed under the seed), so a restarted member
  rejoins on the admit it kept, with nobody asked. A node that finds itself on no complete mesh says so (`line-partial`).
- **Behind it:** the single call became a call table (a line is many calls at once); a call that creates a mesh edge is
  quiet when it fails and retried with a back-off; two members dialing each other at once are resolved by key id;
  `DIAL` and `RING` carry the admit as an opaque blob, and `REJECTED` now names the number so a caller with several dials
  open knows which one failed; the switchboard knows nothing of lines.
- **The site secret** for two boxes on one segment is derived from the line key (a rotated key is accepted alongside the
  old one for 30 seconds); `--site-key` still overrides it.
- **New commands and verbs:** `peers`, `evict`, `pickup ID`, `reject ID`, `hangup NUMBER`; new events `line-joined`,
  `member-joined`, `member-left`, `member-evicted`, `evicted`, `line-partial`, `line-complete`; `ring` gains `admit`.
- **Also:** handshake ids are unique among a node's concurrent handshakes (a line starts many at once); a node holds 32
  peers by default.
- **Tests:** `tests/m3.sh` builds four LANs as one subnet, joined by number: the mesh builds with no prompt on any member,
  all twelve host pairs ping, a broadcast reaches every site, a fourth site joins through a different member, a member is
  evicted and cannot dial back in while the rest are untouched, a killed member restarts and the mesh heals from its
  persisted state, and a member leaves.

## M2: splice, and the documentation made to match the code

- **TCC only, no exceptions** ([ADR 0021](docs/adr/0021-tcc-is-the-compiler-without-exception.md)): the gcc option and the
  gcc cross-check runs are gone, the Makefile says `override CC = tcc` (a compiler named on the command line changes
  nothing), `make lint` is TCC's own `-Wall -Werror`, the Android plan no longer names the NDK (whether TCC can build
  that library is an open item, ROADMAP M6), and `tests/docs.py` fails if another compiler is named.
- **Fixed after an independent review of the docs against the code:**
  - a switchboard started with the *node's* handshake budget (50, not 2000) because the role was read before it was set;
  - a token's reserved number, if taken, was silently replaced by a random one; now the registration is refused;
  - expired and superseded session keys, and a finished handshake's secrets, are erased, not just marked unused;
  - a registration finishing in milliseconds could complete before the node script subscribed to events, so the user saw
    nothing; a new subscriber is now told the registration state (`registered` or `register-refused`) on connecting;
  - the node narrated on stdout, which hibr holds back when redirected, so a service's log lines appeared late; it now
    narrates on stderr;
  - `longwire up` ignored SIGTERM while waiting for an event (hibr runs traps between commands); it now wakes every
    second, so a service manager's stop is prompt and stops the worker;
  - a restarted `longwire up` failed because a worker was already running; it now attaches to it;
  - `longwire setup splice` now prints `Environment=LONGWIRE_MOD=` so the privileged moment names the module by path;
  - the switchboard script's control socket now follows `--state`; the Makefile's `HIBR` is `HIBR_SRC` so it no longer
    clashes with the `HIBR` the tests use; the `site` event is narrated.
  - Documentation: the example number `312-884-071` fails its own check digit (now `312-884-076`); the symmetric-NAT test
    covered symmetric-against-cone only (a symmetric-against-symmetric run was added); the order of the capability drop and
    the fork, the seed's brief life in the module process, the MTU wording, the probe-replay window, the endianness
    list, what the tests assert (`at least two rekeys`), and a dozen smaller points were corrected.

- **Splice:** an `AF_PACKET` port on a NIC in promiscuous mode, with no interface created. Frames the box injects are not
  read back (`PACKET_IGNORE_OUTGOING` and a direction filter); unfinished TCP/UDP checksums from virtual NICs are
  completed; coalesced TCP frames of up to 64 KB are cut into line-sized segments instead of being dropped.
- **The switch:** frames over 1432 bytes travel as two datagrams and are rejoined; TCP MSS is clamped on SYN and SYN-ACK;
  flooded frames have a budget per peer; ARP requests for hosts across the line are answered locally; a dropped peer's
  learned MACs are flushed and traffic for them is flooded again.
- **Site groups:** boxes sharing one LAN segment and a secret announce themselves with authenticated probes, the lowest
  key id forwards, the others stand by and keep a list of the remote hosts, and on takeover re-announce them so the LAN's
  switches move at once. Tested: kill the active box, traffic resumes in about 4 seconds (the test allows up to 14).
- **`call-lost`:** a call that was up notices when its other end dies, reports it, and goes idle; the script decides
  whether to dial again.
- **Handshake cookies:** past half the handshake budget a node answers an `HS_INIT` fragment without a valid cookie with a
  59-byte `COOKIE` and does no work. A legitimate handshake completes during a sustained flood (tested).
- **`longwire setup splice NIC` / `setup jack`** print the one-time privileged step; `up --splice NIC --site-key FILE`.
- **Documentation:** the single `DECISIONS.md` became `docs/adr/`, one record per decision (20 of them); the architecture,
  protocol, switchboard, CLI, crypto, threat-model and build documents were rewritten to say what the code does, with
  one marker, `*(planned: M3)*`, for what it does not yet do; `tests/docs.py` compares them with the code on every
  `make check`; the roadmap is status only.
- **Tests:** `tests/m2.sh` builds two LANs as one subnet from unprivileged namespaces and bridges, and runs real ping, ARP,
  a 3 MB TCP transfer, broadcasts, a broadcast storm, the capability drop, and the failover. The helpers shared by the
  suites moved to `tests/lwtest.py`.
- **Fixed on the way:** a coalesced-frame drop that cost 10% TCP retransmissions (4 Mbit/s became about 50); a 16-byte
  cookie hashed as if it were 32; a frame-size limit that split every clamped segment; a failover that left the LAN
  pointing at the dead box.

## M1: dial tone

- **The switchboard** (`longwire-switchboard`, a hibr script over the module's `lw sb` worker): 9-digit numbers with a
  Damm check digit, bound to keys; token registration, a token that reserves a number, limits per source address and per
  callee; the engine mechanism with the script's policy in `register` and `dial` events.
- **Calls:** `dial`, `pickup`, `reject`, `hangup` between two nodes, with the answer policies `ask`, `known`, `auto` and
  `never`, a six-word safety phrase on both screens, and key pinning (written once the call is up; `unpin` forgets one).
- **NAT hole punching:** host and reflexive candidates, simultaneous authenticated `PUNCH`, verified through a userspace
  cone NAT; two strict NATs fail cleanly with a plain message.
- **Bootstrapping:** `HELLO` fetches a bundle from a key id and lets a stranger introduce itself to a switchboard.
- **Fixed on the way:** `registered` was announced again at every rekey; a refused node asked again every 2 seconds;
  pins were written before the safety phrase could be compared; the caller gave up on a ring at 30 s while the callee rang
  for 60.

## M0: first frame

- **The engine** (`engine/`): identity from one 32-byte seed; a hybrid X25519 + ML-KEM-768 Noise-IK handshake in two
  fragments; ChaCha20-Poly1305 data packets with a 4096-packet replay window; rekeying, keepalive and dead-peer
  detection; the learning switch with split horizon.
- **The hibr module** (`module/longwire.c`): the `lw` builtin and the `/dev/longwire/events` scheme; a forked worker
  that closes inherited descriptors, locks its key memory and clears every capability.
- **The node script** (`scripts/longwire.hibr`), the Makefile (`make`, `make check`), and the vendored Monocypher and
  PQClean ML-KEM-768, built with TCC unchanged apart from a five-line shim.
- **Tests:** `tests/m0.sh`: three nodes across a proxy that duplicates and bit-flips datagrams.
