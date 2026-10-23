# Protocol

All traffic is UDP, on a single socket, over IPv4 for now; IPv6 *(planned: M3)*. There are no TCP fallbacks and no ports
to forward. This document covers framing, sessions, NAT traversal, and the designs for relaying and party lines.
Cryptographic derivations are in [CRYPTO](CRYPTO.md), and the switchboard's messages are in
[SWITCHBOARD](SWITCHBOARD.md). Designed-but-unbuilt parts carry `*(planned: M3)*`.

Integers on the wire are little-endian except where a field says otherwise (candidate ports, observed addresses, `GO`'s start
delay, CTL lengths and the site probe's time are big-endian). `‖` is concatenation. Sizes are in bytes.

## 1. Datagram types

The first byte of every datagram is its type. Anything unrecognised is dropped silently and counted.

| Type | Name | Authenticated by | Purpose |
|---|---|---|---|
| `0x01` | `HS_INIT` | handshake | handshake message 1 (fragmented) |
| `0x02` | `HS_RESP` | handshake | handshake message 2 (fragmented) |
| `0x03` | `COOKIE` | XChaCha20-Poly1305 under a key from the responder's key id | DoS cookie reply |
| `0x04` | `DATA` | session AEAD | everything after the handshake |
| `0x05` | `RELAY` | relay route binding | a sealed peer-to-peer packet carried by a relay *(planned: M3)* |
| `0x06` | `PUNCH` | call token MAC | opens NAT mappings during call setup |
| `0x07` | `ECHO` | call token MAC | asks the switchboard's second port for our reflexive address *(planned: M3)* |
| `0x08` | `HELLO` | the requester checks `H(bundle) == key_id` | fetch a node's bundle when only its key id is known, or offer one (bootstrap) |

**HELLO (bootstrapping from a key id).** The handshake needs the responder's full 1249-byte bundle before `HS_INIT`.
Sometimes only a key id is known: a switchboard given as `host:port/lw1…`. Then the initiator sends
`0x08 ‖ 0x00 ‖ key_id[0..7]`, and the node answers `0x08 ‖ 0x01 ‖ bundle`. The answer isn't trusted until
`BLAKE2b-256(bundle)` equals the full key id the requester already holds, so a forged bundle is simply discarded.

The handshake also needs the responder to know the *initiator's* bundle (for `skem(I)`). Nodes get that from `RING`,
but a switchboard has never seen a node that is registering for the first time. So a node first *offers* its own
bundle: `0x08 ‖ 0x02 ‖ bundle`. A switchboard holds an offered bundle in a cache of 16 (the oldest is recycled) for
10 seconds, and admits a node only if the handshake that follows names a key it was offered, so the handshake cannot be
used to make it remember arbitrary keys. A request is 10 bytes and the answer about 1.25 KB, so a node answers at
most 20 `HELLO` requests a second *in total* (a per-source limit *(planned: M3)*).

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
- Fixed overhead is **29 bytes** (12-byte header + 1-byte kind + 16-byte tag). The data path allocates nothing, apart
  from one reassembly table per peer the first time a frame is split.

Plaintext `kind` values:

| kind | body |
|---|---|
| `0x00 FRAME` | one Ethernet frame, header included, without the FCS |
| `0x01 FRAG` | `frame_id:2 ‖ part:1 (0 or 1) ‖ total_len:2 ‖ bytes` (big-endian). A frame over 1432 bytes is split across two datagrams ([ARCHITECTURE §6](ARCHITECTURE.md#6-packet-sizes)) |
| `0x02 KEEPALIVE` | empty |
| `0x10 CTL` | one control message: `msg:1 ‖ len:2 (big-endian) ‖ payload` (§6) |

**Roaming.** When a packet authenticates from a new source address, that address becomes the peer's endpoint.
Only an authenticated packet can move an endpoint.

## 3. Sessions and timers

| Timer | Value | Meaning |
|---|---|---|
| `KEEPALIVE` | `--keepalive`, default 25 s | if nothing has been *sent* for that long, send a keepalive (holds NAT mappings open) |
| `REKEY_AFTER` | `--rekey`, default 120 s | the session's initiator starts a new handshake; the other end starts one 10 s later if none has come. The old keys keep working until the new ones are confirmed |
| `REJECT_AFTER` | 1.5 × `REKEY_AFTER` (180 s), or 2⁶⁰ packets | keys are erased, and nothing is sent or accepted under them |
| `HS_RETRY` | 1, 2, 4, 8, then every 8 s | handshake retransmission; each retry is a fresh handshake |
| `DEAD` | 3 × `KEEPALIVE` + 1 s with nothing received | the session is dropped. The dialer starts a new handshake at once, and a node on a call reports `call-lost` if nothing replaces the session within 2 × `KEEPALIVE` |

Handshake messages are larger than one datagram (because of ML-KEM). Each one is sent as fragments:

```
 type:1 ‖ hs_id:2 ‖ frag:1 (index<<4 | count) ‖ chunk (≤ 1212) ‖ tag:16 ‖ cmac:16
 tag  = BLAKE2b-128(key = H("mac1" ‖ receiver.key_id), every byte before it)
 cmac = BLAKE2b-128(key = cookie, every byte before it)      or sixteen zero bytes when no cookie is held
```

`HS_INIT` is 2416 bytes and `HS_RESP` is 2260, so each is exactly 2 fragments (a full handshake is 4 datagrams, each
at most 1248 bytes, once every 2 minutes). Every fragment carries its own tag, so a responder discards anything that
doesn't prove knowledge of its key id *before* buffering a byte. (The whole-message `mac1` at the end of the
handshake, checked after reassembly, can't serve for this: it sits in the last fragment.) A responder buffers at
most **one** partial message per source address, for at most 1 s.

**Cookies (DoS defence).** Each handshake costs a KEM decapsulation, so a responder counts them in a budget (50/s on
a node, 2000/s on a switchboard). Once the budget is more than half spent, the responder is *under load*: an
`HS_INIT` fragment whose `cmac` isn't valid for its source address gets no slot and no work, only a `COOKIE` reply:

```
 0x03 ‖ hs_id:2 ‖ nonce:24 ‖ XChaCha20-Poly1305(key = H("cookie--" ‖ responder.key_id), ad = the fragment's tag,
                                                  plaintext = cookie:16) ‖ tag:16          = 59 bytes
 cookie = BLAKE2b-128(key = secret, source address and port)       the secret rotates every 120 s
```

The initiator decrypts it (the associated data is the tag of the fragment it sent, which an off-path attacker never
sees), keeps the cookie for 120 s, and retries at once with `cmac` set; it accepts at most one cookie a second, so a
bad reply cannot start a loop. Fragments that carry a valid cookie skip the global budget, so a flood from addresses
that can't receive replies can't starve a real handshake. They are limited per source address instead (burst 8,
2 per second). The `mac2` field at the end of the reassembled message is the same cookie MAC over the whole message
and is checked as well, but only while the node is under load. The `hs_id` is chosen by the initiator and echoed in the response,
which is how a reply is matched to its attempt. Each retry builds a completely fresh handshake (new ephemerals, a
larger `tai64n`), so a retransmission is never mistaken for a replay.

**Who initiates?** The dialer makes the first handshake. After that the session's initiator rekeys at `REKEY_AFTER`
and the other end 10 seconds later if nothing has come, which makes a crossed rekey rare. A responder never sends
`DATA` on a new session until it has received the first `DATA` from that session's initiator; that first packet is the
key confirmation. If both ends do start at once, each can end up on a different session and the link stalls until the
dead-peer timer fires; the tie-break (the session whose initiator has the lower key id wins) *(planned: M3)*.

Sessions with a switchboard use the same intervals as any other; a longer one, to keep a large switchboard's
handshake rate low, *(planned: M3)*.

## 4. NAT traversal

Every call tries the **direct** path. The switchboard is involved only until a path is up.

**Candidates.** Each side gathers these and sends them through the switchboard in `DIAL` / `ANSWER`:

| Candidate | Source |
|---|---|
| host | each local IPv4 address that is up (not loopback, not link-local; loopback only with `--cand-loopback`), with our UDP port. Finds a peer on the same LAN |
| reflexive | the address the switchboard observed the node at, learned from `NUMBER` and `HBACK` |

The switchboard adds the address it sees each side at to the list it passes on.

**PUNCH.** The switchboard's `GO` message carries a start delay (150 ms), not a clock time, so no clock agreement is
needed: each side starts that long after `GO` arrives (the round trips to the switchboard differ by milliseconds, and
the punching repeats). Each side sends a `PUNCH` to every candidate at 10 packets a second, for up to 8 seconds:

```
 0x06 ‖ call_id:8 ‖ sender_key_id[0..7] ‖ mac:16 = BLAKE2b-128(key = call_token, the 17 bytes before it)
```

`call_token` is a 32-byte secret that the switchboard gives both parties in `GO`. When a `PUNCH` that verifies
arrives, the receiver has a working address. Both sides answer it at once (which opens their own NAT toward it).
The caller then sends `HS_INIT` to that address. The callee keeps punching until the session is up, because its
mapping must stay open for the caller's handshake to arrive. A call that has no session after 8 seconds fails with
`no direct path (both sides behind strict NAT?)`, and nothing is left behind. Two strict (symmetric) NATs therefore
fail cleanly, as the symmetric-NAT tests show (a symmetric NAT facing a cone NAT, and two symmetric NATs).

### The harder rungs *(planned: M3)*

The full design is a ladder, each rung tried when the one before fails:

```
 IDLE ─dial─► SIGNAL ──GO──► PUNCH ──ok──► HANDSHAKE ──► DIRECT ◄──────────┐
                              │ 8 s                                        │ upgrade
                              ▼                                            │ succeeds
                   one side EIM, other EDM? ──yes──► SPRAY ──ok──► HANDSHAKE
                              │ no                      │ 10 s
                              ▼                         ▼
                            RELAY ◄─────────────────────┘ ─── retry DIRECT every 60 s → 10 min
```

- **More candidates:** global IPv6, a NAT-PMP / PCP mapping, and relay candidates (public members of the line that
  offer relaying).
- **Mapping type.** If the switchboard sees the same reflexive port on both of its ports (the `ECHO` exchange), the
  NAT is endpoint-independent (EIM, "easy"); if it sees different ports, endpoint-dependent (EDM, "hard").
- **SPRAY** (one EIM side, one EDM side). The hard side opens 256 extra sockets, each sending to the easy side's
  reflexive address, which creates 256 mappings on its NAT. The easy side probes random ports on the hard side's
  public IP at 100 pps. The chance of a hit after *n* probes is `1 − (1 − 256/64512)ⁿ`, about **98% at 1,024
  probes**, in roughly 10 seconds. Spraying isn't attempted when *both* sides are EDM, because the odds are
  hopeless, so that case goes straight to relay.
- **Path upgrade.** A relayed session keeps probing better candidates in the background, and moves to a better path
  seamlessly when one is found. Session keys don't change when the path does.

## 5. Relay *(planned: M3)*

A relay is an ordinary line member with a public address that offers relaying (on by default for public
nodes). **The switchboard never relays.**

1. A asks relay R over their own session: `CTL RELAY_REQ{to: key_id_B}`.
2. R asks B over *their* session, and B agrees if B is on the line.
3. R allocates two 24-bit route ids and binds each one to an authenticated endpoint. It sends `RELAY_OK{route}` to
   A and to B.
4. A sends `0x05 ‖ route:3 ‖ <a complete A↔B packet>`. R checks that the source address matches the route's bound
   endpoint, swaps the route id and forwards the packet. That's it.

R sees only sizes and timing. The inner packet is A↔B's own end-to-end session, and R doesn't have its keys. Relay
overhead is 4 bytes. Routes expire after 60 s idle, R limits each route's rate, and the source binding stops R from
being used as an open reflector.

## 6. Control messages and lines

A `CTL` message is `msg:1 ‖ len:2 ‖ payload` inside a session. Between two nodes on a call, the only message built is
`BYE`: `longwire hangup` sends it and the other end tells its user. The messages with a switchboard (`0x20`–`0x2f`)
are in [SWITCHBOARD](SWITCHBOARD.md).

| msg | name | body |
|---|---|---|
| `0x01` | `PEERS` | list of `{number, key_id, role, relay, candidates}` *(planned: M3)* |
| `0x02` | `ADMIT` | signed admit record *(planned: M3)* |
| `0x03` | `EVICT` | signed evict record *(planned: M3)* |
| `0x04` | `LINEKEY` | new line key, inside the pairwise session *(planned: M3)* |
| `0x05` | `RELAY_REQ` | §5 *(planned: M3)* |
| `0x06` | `RELAY_OK` | §5 *(planned: M3)* |
| `0x07` | `PMTU` | probe result *(planned: M7)* |
| `0x08` | `BYE` | hanging up |

### Lines and party lines *(planned: M3)*

A **line** is one L2 segment with a 64-bit random `line_id`, shown as its first 6 hex digits (`7f3a9c`), and a
32-byte **line key** that every member holds. The line key authenticates the probes between boxes of one site (which
use a shared site secret until then, [ARCHITECTURE §4](ARCHITECTURE.md#4-two-boxes-on-one-segment-site-groups)). It
never encrypts data: sessions are always pairwise.

**Joining.** When node N dials number M:

1. If neither node is on a line, M creates one and becomes its first member. If M is on a line, N joins M's line.
   If *both* are already on different lines, the call is refused with `BUSY: already on line 7f3a9c` in v1.
2. M picks up, by asking its user or following its answer policy ([CLI](CLI.md)).
3. M signs an **admit** record, `ADMIT{line_id, key_id_N, role, admitted_by: key_id_M, time}` with Ed25519, and
   sends it to N and to every member.
4. M sends N the line key and `PEERS`: every member's number, key id, role (`trunk` | `leaf`) and relay offer.
5. If N is a **trunk** (a site), it dials every member through the switchboard, presenting the admit record. Members
   pick up automatically for a valid admit, so the full mesh builds itself with no more prompts. A **leaf** stays
   with M only.

**Leaving.** `longwire hangup` sends `BYE` to all peers. The line keeps existing while at least one member remains.

**Eviction.** `longwire evict <number>` signs `EVICT{line_id, key_id}`. Every member tears down sessions with that
key and refuses it from then on. The evicting node also generates a new line key and sends it to the remaining
members, so the evicted node can no longer forge probes.
