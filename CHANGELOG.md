# Changelog

What changed, newest first. The reasons are in [docs/adr](docs/adr/README.md); what is still to come is in
[docs/ROADMAP.md](docs/ROADMAP.md). `git log` is the full history.

## M2: splice, and the documentation made to match the code

- **Splice:** an `AF_PACKET` port on a NIC in promiscuous mode, with no interface created. Frames the box injects are not
  read back (`PACKET_IGNORE_OUTGOING` and a direction filter); unfinished TCP/UDP checksums from virtual NICs are
  completed; coalesced TCP frames of up to 64 KB are cut into line-sized segments instead of being dropped.
- **The switch:** frames over 1432 bytes travel as two datagrams and are rejoined; TCP MSS is clamped on SYN and SYN-ACK;
  flooded frames have a budget per peer; ARP requests for hosts across the line are answered locally; a dropped peer's
  learned MACs are flushed and traffic for them is flooded again.
- **Site groups:** boxes sharing one LAN segment and a secret announce themselves with authenticated probes, the lowest
  key id forwards, the others stand by and keep a list of the remote hosts, and on takeover re-announce them so the LAN's
  switches move at once. Tested: kill the active box, traffic resumes in about 4 seconds.
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
