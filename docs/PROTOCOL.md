# Protocol

All traffic is UDP, on a single socket per address family. There are no TCP fallbacks and no ports to forward. This
document covers framing, sessions, NAT traversal, relaying and party lines. Cryptographic derivations are in
[CRYPTO](CRYPTO.md), and the switchboard's messages are in [SWITCHBOARD](SWITCHBOARD.md).

Integers are little-endian. `‖` is concatenation. Sizes are in bytes.

## 1. Datagram types

The first byte of every datagram is its type. Anything unrecognised is dropped silently and counted.

| Type | Name | Authenticated by | Purpose |
|---|---|---|---|
| `0x01` | `HS_INIT` | handshake | handshake message 1 (fragmented) |
| `0x02` | `HS_RESP` | handshake | handshake message 2 (fragmented) |
| `0x03` | `COOKIE` | XChaCha20-Poly1305 under a responder-derived key | DoS cookie reply |
| `0x04` | `DATA` | session AEAD | everything after the handshake |
| `0x05` | `RELAY` | relay route binding | a sealed peer-to-peer packet carried by a relay |
| `0x06` | `PUNCH` | call token MAC | opens NAT mappings during call setup |
| `0x07` | `ECHO` | call token MAC | asks the switchboard's second port for our reflexive address |
| `0x08` | `HELLO` | the requester checks `H(bundle) == key_id` | fetch a node's bundle when only its key ID is known (bootstrap) |

**HELLO (bootstrapping from a key ID).** The handshake needs the responder's full 1249-byte bundle before `HS_INIT`.
Sometimes only a key ID is known: the first contact with a switchboard given as `host:port/lw1…`, or a node dialed
directly by full address on a known endpoint. Then the initiator sends `0x08 ‖ 0x00 ‖ key_id[0..7]`, and the node
answers `0x08 ‖ 0x01 ‖ bundle`. The answer isn't trusted until `BLAKE2b-256(bundle)` equals the full key ID the
requester already holds, so a forged bundle is simply discarded.

The handshake also needs the responder to know the *initiator's* bundle (for `skem(I)`). Nodes get that from
`RING`, but a switchboard has never seen a node that is registering for the first time. So a node may first *offer*
its own bundle: `0x08 ‖ 0x02 ‖ bundle`. The responder holds an offered bundle in a small bounded cache for 10 s, and
only for a handshake that then completes; nothing is stored until it has been authenticated. The cache holds 16
offers and recycles the oldest. A request is 10 bytes and the answer is about 1.25 KB, so a node answers at most 20
HELLO requests a second *in total* (the first cut of the amplification limit; a per-source limit comes with the
cookies). A switchboard also admits a node only if its offer arrived in the last 10 s, so the handshake can't be
used to make it remember arbitrary keys.

## 2. Data packets

```
 0      1           4                  12                               n-16      n
 ┌──────┬───────────┬──────────────────┬─────────────────────────────────┬─────────┐
 │ 0x04 │ rx_index  │ counter          │ ciphertext( kind ‖ body )       │ tag     │
 │  1   │    3      │    8             │                                 │  16     │
 └──────┴───────────┴──────────────────┴─────────────────────────────────┴─────────┘
   AEAD = ChaCha20-Poly1305 (RFC 8439)   nonce = 0⁴ ‖ counter   AD = bytes 0..11
```

- `rx_index` is the *receiver's* session index (24 bits, random). It selects the key with no lookup by address, so
  a peer can roam between networks mid-session.
- `counter` starts at 0 for each session and never repeats. The receiver keeps a **4096-packet sliding replay
  window** and checks it *after* the tag verifies.
- Fixed overhead is **29 bytes** (12-byte header + 1-byte kind + 16-byte tag). Encryption is done in place, with
  headroom reserved in the receive buffer, so the data path never copies or allocates.

Plaintext `kind` values:

| kind | body |
|---|---|
| `0x00 FRAME` | one Ethernet frame, header included, without the FCS |
| `0x01 FRAG` | `frame_id:2 ‖ part:1 (0 or 1) ‖ total_len:2 ‖ bytes`. An oversized frame is split across two datagrams ([ARCHITECTURE §6](ARCHITECTURE.md#6-mtu)) |
| `0x02 KEEPALIVE` | empty, or zero padding when it's a PMTU probe |
| `0x10 CTL` | one control message: `msg:1 ‖ len:2 ‖ TLV body` (§6) |

**Roaming.** When a packet authenticates from a new source address, that address becomes the peer's endpoint.
Only an authenticated packet can move an endpoint.

## 3. Sessions and timers

| Timer | Value | Meaning |
|---|---|---|
| `KEEPALIVE` | 25 s | if nothing has been *sent* for 25 s, send a keepalive (holds NAT mappings open) |
| `REKEY_AFTER` | 120 s or 2⁴⁸ packets | the initiator starts a new handshake; the old keys keep working until it completes |
| `REJECT_AFTER` | 180 s or 2⁶⁰ packets | keys are erased, and nothing is sent or accepted under them |
| `HS_RETRY` | 1, 2, 4, 8, 8 s | handshake retransmission. After 5 failures the path is marked failed and the NAT ladder runs again (§4) |
| `DEAD` | 3 × `KEEPALIVE` with nothing received | the session is torn down and the peer is re-dialed through the switchboard |

Handshake messages are larger than one datagram (because of ML-KEM). Each one is sent as fragments:

```
 type:1 ‖ hs_id:2 ‖ frag:1 (index<<4 | count) ‖ chunk (≤ 1212) ‖ tag:16
 tag = BLAKE2b-128(key = H("mac1" ‖ receiver.key_id), every byte before it)
```

`HS_INIT` is 2416 bytes and `HS_RESP` is 2260, so each is exactly 2 fragments (a full handshake is 4 datagrams, each
under 1232 bytes, once every 2 minutes). Every fragment carries its own tag, so a responder discards anything that
doesn't prove knowledge of its key ID *before* buffering a byte. (The whole-message `mac1` at the end of the
handshake, checked after reassembly, can't serve for this: it sits in the last fragment.) A responder buffers at
most **one** partial message per source address, for at most 1 s. The `hs_id` is chosen by the initiator and echoed
in the response, which is how a reply is matched to its attempt. Each retry builds a completely fresh handshake
(new ephemerals, a larger `tai64n`), so a retransmission is never mistaken for a replay.

**Who initiates?** The caller makes the first handshake. After that, the rule is WireGuard's: *either* side starts
a new handshake when it has something to send and `REKEY_AFTER` has passed on the current keys. If both start at
once (a crossed rekey), both handshakes complete, and the session whose initiator has the lower key ID is kept.
Either way, a responder never sends `DATA` on a new session until it has received the first `DATA` from that
session's initiator. That first packet is the key confirmation.

Sessions with a switchboard carry only small control messages, so they use `REKEY_AFTER` = 600 s (and
`REJECT_AFTER` = 660 s). That keeps a large switchboard's handshake rate low; see [SWITCHBOARD](SWITCHBOARD.md#how-tiny).

## 4. NAT traversal: the ladder

Every call climbs this ladder, stopping at the first rung that works. The switchboard is involved only until a
path is up.

```
 IDLE ─dial─► SIGNAL ──GO(t₀)──► PUNCH ──ok──► HANDSHAKE ──► DIRECT ◄──────────┐
                                   │ 5 s                                        │ upgrade
                                   ▼                                            │ succeeds
                        one side EIM, other EDM? ──yes──► SPRAY ──ok──► HANDSHAKE
                                   │ no                      │ 10 s
                                   ▼                         ▼
                                 RELAY ◄─────────────────────┘ ─── retry DIRECT every 60 s → 10 min
```

**Candidates.** Each side gathers these and sends them through the switchboard in `DIAL` / `ANSWER`:

| Candidate | Source | Priority |
|---|---|---|
| global IPv6 | local interfaces | 1 (often no NAT at all) |
| host | each local IPv4/IPv6 address + our port (finds peers on the same LAN) | 2 |
| mapped | NAT-PMP / PCP mapping, if the gateway answers within 250 ms | 3 |
| reflexive | the address the switchboard observed (port 1), plus a second observation via `ECHO` to port 2 | 4 |
| relay | public-address members of the line that offer relaying | 5 |

**Mapping type.** If the switchboard sees the same reflexive port on both of its ports, our NAT is
endpoint-independent (EIM, "easy"). If it sees different ports, the NAT is endpoint-dependent (EDM, "hard").

**PUNCH.** The switchboard's `GO` message carries a start delay (150 ms in M1), not a clock time, so no clock
agreement is needed: each side starts that long after `GO` arrives (the round trips to the switchboard differ by
milliseconds, and the punching repeats). Each side sends a `PUNCH` to every candidate at 10 pps, for up to 8 s:

```
 0x06 ‖ call_id:8 ‖ sender_key_id[0..7] ‖ mac:16 = BLAKE2b-128(call_token, bytes 0..16)
```

`call_token` is a 32-byte secret that the switchboard gives both parties in `GO`. When a `PUNCH` that verifies
arrives, the receiver has a working address. Both sides answer it at once (which opens their own NAT toward it).
The caller then sends `HS_INIT` to that address. The callee keeps punching until the session is up, because its
mapping must stay open for the caller's handshake to arrive. A call that has no session after 8 s fails with
`no direct path (both sides behind strict NAT?)`, and nothing is left behind.

**Built in M1:** the direct rung (host and reflexive candidates, simultaneous `PUNCH`). **Not yet:** the `ECHO`
exchange that tells easy NATs from hard ones, port spray and relaying (M3). Until then, two strict NATs fail
cleanly, as the symmetric-NAT test shows.

**SPRAY** (one EIM side, one EDM side). The hard side opens 256 extra sockets, each sending to the easy side's
reflexive address, which creates 256 mappings on its NAT. The easy side probes random ports on the hard side's
public IP at 100 pps. The chance of a hit after *n* probes is `1 − (1 − 256/64512)ⁿ`, which is about **98% at 1,024
probes**, in roughly 10 seconds. Spraying isn't attempted when *both* sides are EDM, because the odds are hopeless,
so that case goes straight to relay.

**Path upgrade.** A relayed or IPv4 session keeps probing better candidates in the background, and moves to a
better path seamlessly when one is found. Session keys don't change when the path does.

## 5. Relay

A relay is an ordinary line member with a public address that offers relaying (on by default for public
nodes). **The switchboard never relays.**

1. A asks relay R over their own session: `CTL RELAY_REQ{to: key_id_B}`.
2. R asks B over *their* session, and B agrees if B is on the line.
3. R allocates two 24-bit route IDs and binds each one to an authenticated endpoint. It sends `RELAY_OK{route}` to
   A and to B.
4. A sends `0x05 ‖ route:3 ‖ <a complete A↔B packet>`. R checks that the source address matches the route's bound
   endpoint, swaps the route ID and forwards the packet. That's it.

R sees only sizes and timing. The inner packet is A↔B's own end-to-end session, and R doesn't have its keys. Relay
overhead is 4 bytes. Routes expire after 60 s idle, R limits each route's rate, and the source binding stops R from
being used as an open reflector.

## 6. Lines and party lines

A **line** is one L2 segment with a 64-bit random `line_id`, shown as its first 6 hex digits (`7f3a9c`), and a
32-byte **line key** that every member holds. The line key authenticates line probes ([ARCHITECTURE
§4](ARCHITECTURE.md#4-loops-and-high-availability)). It never encrypts data: sessions are always pairwise.

**Joining.** When node N dials number M:

1. If neither node is on a line, M creates one and becomes its first member. If M is on a line, N joins M's line.
   If *both* are already on different lines, the call is refused with `BUSY: already on line 7f3a9c` in v1.
2. M picks up, by asking its user or following its answer policy ([CLI](CLI.md#answer-policy)).
3. M signs an **admit** record, `ADMIT{line_id, key_id_N, role, admitted_by: key_id_M, time}` with Ed25519, and
   sends it to N and to every member.
4. M sends N the line key and `PEERS`: every member's number, key ID, role (`trunk` | `leaf`) and relay offer.
5. If N is a **trunk** (a site), it dials every member through the switchboard, presenting the admit record. Members
   pick up automatically for a valid admit, so the full mesh builds itself with no more prompts. A **leaf** stays
   with M only.

**Leaving.** `longwire hangup` sends `BYE` to all peers. The line keeps existing while at least one member remains.

**Eviction.** `longwire evict <number>` signs `EVICT{line_id, key_id}`. Every member tears down sessions with that
key and refuses it from then on. The evicting node also generates a new line key and sends it to the remaining
members, so the evicted node can no longer forge probes.

**Control messages (`CTL`):**

| msg | name | body |
|---|---|---|
| `0x01` | `PEERS` | list of `{number, key_id, role, relay, candidates}` |
| `0x02` | `ADMIT` | signed admit record |
| `0x03` | `EVICT` | signed evict record |
| `0x04` | `LINEKEY` | new line key (inside the pairwise session) |
| `0x05` | `RELAY_REQ` / `0x06 RELAY_OK` | §5 |
| `0x07` | `PMTU` | probe result |
| `0x08` | `BYE` | hanging up |
| `0x20–0x2F` | switchboard messages | only on a session with a switchboard; see [SWITCHBOARD](SWITCHBOARD.md) |
