# Roadmap

Each milestone ends with something you can *use*, and with tests that prove it.

| | Milestone | Done when |
|---|---|---|
| **M0** | **First frame.** *(built; see below)* `engine/` skeleton, wire format, handshake, sessions, jack mode; the module's `lw start`/`lw status` and the worker fork; a minimal `longwire` node script; peers given by address (no switchboard yet) | two Linux hosts on one LAN ping each other through `lw0`, driven from hibr; KATs pass under tcc and gcc |
| **M1** | **Dial tone.** *(built; see below)* The `longwire-switchboard` script and `lw sb` worker: numbers, register, heartbeat, dial/ring/answer/GO; UDP punching; pinning; safety phrase; answer policy in the node script | two hosts behind two different home NATs connect by number, and the call survives killing the switchboard |
| **M2** | **Splice.** AF_PACKET splice, ambient-capability service unit + capability drop, MSS clamp, frame split/reassembly, ARP/ND suppression, broadcast limits, line probes, site groups and failover | two physical sites share one /24: DHCP from site A leases a host at site B; pulling one of two site boxes fails over in ≤ 3 s |
| **M3** | **Party line.** Lines with more than 2 members, admits, PEERS, auto-mesh, evict + line-key rotation, relay, port spray, path upgrades | 4 sites, one behind CGNAT on both ends, all at L2; the relayed pair upgrades to direct once a path exists |
| **M4** | **Desktop and distribution.** `control-panel/lines.hibr` (the Network pane Lines section, ring dialog with safety phrase, menu-bar ☎); the module as signed deb/rpm/pacman/brew packages; both scripts in hibr's catalogue; the default public switchboard live | on a clean machine: install the package, `catalogue install longwire`, then dial and pick up entirely from the hibr desktop |
| **M5** | **No privilege, more Unix.** `longwire shell` (user namespaces); FreeBSD/OpenBSD tap + BPF splice; macOS BPF splice | a user with no sudo anywhere joins a line with `longwire shell` |
| **M6** | **Pocket.** Member-mode adapter (DHCP client, ARP/ND responder, virtual MAC); leaf role; Android app (`VpnService` around `engine/` via JNI; the one piece outside hibr) | a phone on mobile data gets a lease from the home router and prints to the home printer |
| **M7** | **Speed.** `recvmmsg`/`sendmmsg`, `TPACKET_V3`, one worker per core, x86-64 ChaCha20 assembly assembled by TCC (arm64 once TCC's assembler is verified), optional AEGIS-256 suite | ≥ 1 Gbit/s on the gcc build; ≥ 600 Mbit/s on the tcc build (x86_64) |
| **M8** | **1.0.** Tamarin/ProVerif model of the handshake, continuous fuzzing of `wire.c`, dudect timing tests, external review, reproducible builds | an independent report with no open high-severity findings |

### M1: what exists

Built and tested end to end (`make check` runs `tests/m1.sh`): a `longwire-switchboard` script and node scripts,
across a userspace NAT router in unprivileged network namespaces.
- **Cone NAT, both sides:** registration with token policy (including a token that reserves a number, and a spent
  token refused), bad and unknown numbers, a call that rings and is picked up by a person, the same six-word safety
  phrase on the ring and on both `connected` events, the path confirmed as NAT-to-NAT, 150 frames each way over the
  punched path, hangup, decline, a second call that recognises the pinned number, a third call that connects again,
  and a pinned number answered by a different key (the call is dropped and the user told).
- **Symmetric NAT on one side:** both ends get `no direct path (both sides behind strict NAT?)` and nothing is left
  half-open; the router confirms it blocked the punches.
- **Handshake flood:** 400 well-formed handshakes without cookies are answered with `COOKIE` replies and no handshake
  work; the live session is unharmed. (Cookies landed after M1; see the next section.)

Not done, and said plainly:
- **No default switchboard yet:** `--switchboard HOST:PORT/KEY` is required until you supply the host.
- **NAT type, spray and relay** (`ECHO`, hard-NAT rungs): M3. Two strict NATs fail cleanly rather than connect.
- **Numbers are never released**, `LOOKUP`/`who`, per-source HELLO limit, and the 10-minute rekey for switchboard
  sessions are not built.
- The callee of a call whose caller drops it on a key mismatch waits out the 8 s punch timeout instead of being told.
- **A call has no exit from "up" except `hangup`.** If the peer dies, the dead-peer timer invalidates the session but no
  event fires, status keeps saying `call: up`, and the caller keeps retrying the handshake to the last address. The
  design says "torn down and re-dialed"; that isn't built.
- **A dropped peer leaves its MAC-table entries behind,** so for up to 300 s frames for those addresses are dropped
  instead of flooded. One call at a time hides it; it matters with party lines (M3).
- One call at a time, IPv4 only, and the real tap port is still untested here (no `/dev/net/tun`).
- `make check` needs unprivileged user namespaces (`unshare -Urn`), which some distributions restrict (Ubuntu 24.04+).
- Script nits: the switchboard script spends a token before the engine confirms the reply, and its rate-limit counters
  are never pruned.

Found and fixed while building M1 (all now covered by tests): `registered` was announced again at every rekey; a
refused node asked again every 2 s (which would have burned a whole NAT address's registration budget); pins were
written before the user could compare the safety phrase, and there was no way to remove one (a pin is now written
when the call is up, and `longwire unpin NUMBER` forgets it); the caller gave up at 30 s while the callee rings for 60.

### M0: what exists

Built and tested: the hybrid handshake, sessions with replay protection and rekeying, the learning switch,
`lw start/stop/status/key`, the forked worker that clears every capability, and a minimal `longwire` node script.
`make check` starts three nodes through hibr and drives them across a hostile network (a proxy that duplicates and
bit-flips datagrams): handshakes, delivery in both directions, the switch's learn/forward/flood rules, replay and
tamper rejection, repeated rekeys under load without loss, an unknown peer refused, a second node refused, and
empty capability sets in every worker.

Not done, and said plainly:
- **The jack port (a real tap) is untested.** The machine this was built on is an LXC container with no
  `/dev/net/tun`. The code is written; the tests use the equivalent `sock` port. First real-tap test, on a host
  that has one: `ip tuntap add dev lw0 mode tap user $USER; ip link set lw0 mtu 1400 up`, then ping across.
- **End-to-end throughput is unmeasured.** 290 Mbit/s remains the single-core crypto microbenchmark.
- **No known-answer tests yet.** The ML-KEM round trip and the handshake agree with themselves; neither has been
  checked against FIPS 203, RFC 8439 or RFC 7748 vectors. Until `tools/vendor.sh` and the KATs exist, the vendored
  crypto's correctness rests on upstream's own tests.
- **A crossed rekey has no tie-break.** If both sides start a handshake in the same instant, each can end up on the
  session the other holds as its previous one, and the link goes dark until the dead-peer timer redials
  (about 76 s). The responder's rekey is deliberately 10 s behind the initiator's to make this rare, and the lower-
  key-ID rule in [PROTOCOL §3](PROTOCOL.md#3-sessions-and-timers) is not implemented yet.
- IPv4 only; MAC-table limits are fixed.
- A frame bigger than the line's packet limit is dropped rather than split (M2).

**Later, not promised:** federated switchboards (`number@sb`), ML-DSA admit signatures, more than one line per node,
IGMP/MLD snooping, Windows and iOS members, and an OpenWrt package.
