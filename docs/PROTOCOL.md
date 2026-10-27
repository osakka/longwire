# Protocol

All traffic is UDP, over IPv4 and IPv6 (a node holds a socket of each, [ADR 0027](adr/0027-ipv6-is-a-second-socket.md)). There are no TCP fallbacks and no ports
to forward. This document covers framing, sessions, NAT traversal, and the designs for relaying and party lines.
Cryptographic derivations are in [CRYPTO](CRYPTO.md), and the switchboard's messages are in
[SWITCHBOARD](SWITCHBOARD.md). Designed-but-unbuilt parts carry `*(planned: M3)*`.

Integers on the wire are little-endian except where a field says otherwise (candidate ports, observed addresses, `GO`'s start
delay, CTL lengths, the site probe's time and the 8-byte times in admits and evictions are big-endian). `‖` is concatenation. Sizes are in bytes.

## 1. Datagram types

The first byte of every datagram is its type. Anything unrecognised is dropped silently and counted.

| Type | Name | Authenticated by | Purpose |
|---|---|---|---|
| `0x01` | `HS_INIT` | handshake | handshake message 1 (fragmented) |
| `0x02` | `HS_RESP` | handshake | handshake message 2 (fragmented) |
| `0x03` | `COOKIE` | XChaCha20-Poly1305 under a key from the responder's key id | DoS cookie reply |
| `0x04` | `DATA` | session AEAD | everything after the handshake |
| `0x05` | `RELAY` | the relay's route table, then the inner packet's own authentication | a peer-to-peer datagram carried by a relay (§5) |
| `0x06` | `PUNCH` | call token MAC | opens NAT mappings during call setup |
| `0x07` | `ECHO` | none: the nonce is echoed, the answer comes from the switchboard's address, and it is shorter than the request | asks the switchboard's second port for our reflexive address |
| `0x08` | `HELLO` | the requester checks `H(bundle) == key_id` | fetch a node's bundle when only its key id is known, or offer one (bootstrap) |

**HELLO (bootstrapping from a key id).** The handshake needs the responder's full 1249-byte bundle before `HS_INIT`.
Sometimes only a key id is known: a switchboard given as `host:port/lw1…`. Then the initiator sends
`0x08 ‖ 0x00 ‖ key_id[0..7]`, and the node answers `0x08 ‖ 0x01 ‖ bundle`. The answer isn't trusted until
`BLAKE2b-256(bundle)` equals the full key id the requester already holds, so a forged bundle is simply discarded.

The handshake also needs the responder to know the *initiator's* bundle (for `skem(I)`). Nodes get that from `RING`,
but a switchboard has never seen a node that is registering for the first time. So a node first *offers* its own
bundle: `0x08 ‖ 0x02 ‖ bundle`. A switchboard holds an offered bundle for 10 seconds in a cache of 1,024 places found by the key id
(a hash of the bundle, so nobody can aim an offer at another's place; two places are tried, and the older is recycled), and admits a node only if the handshake that follows names a key it was offered, so the handshake cannot be
used to make it remember arbitrary keys. A request is 10 bytes and the answer about 1.25 KB, so a node answers at
most 20 `HELLO` requests a second *in total*, and 2 a second (burst 4) from any one source address.

## 2. Data packets

```
 0      1           4                  12                               n-16      n
 ┌──────┬───────────┬──────────────────┬─────────────────────────────────┬─────────┐
 │ 0x04 │ rx_index  │ counter          │ ciphertext( kind ‖ body )       │ tag     │
 │  1   │    3      │    8             │                                 │  16     │
 └──────┴───────────┴──────────────────┴─────────────────────────────────┴─────────┘
   AEAD = ChaCha20-Poly1305 (RFC 8439)   nonce = 0⁴ ‖ counter   AD = bytes 0..11
```

- `rx_index` is the *receiver's* session index (24 bits, chosen by the receiver: random, with the low bits naming the peer slot it
  is for, so the owner of a datagram is found in one step). It selects the key with no lookup by address, so
  a peer can roam between networks mid-session.
- `counter` starts at 0 for each session and never repeats. The receiver keeps a **4096-packet sliding replay
  window** and checks it *after* the tag verifies.
- Fixed overhead is **29 bytes** (12-byte header + 1-byte kind + 16-byte tag). The data path allocates nothing, apart
  from one reassembly table per peer the first time a frame is split.

Plaintext `kind` values:

| kind | body |
|---|---|
| `0x00 FRAME` | one Ethernet frame, header included, without the FCS |
| `0x01 FRAG` | `frame_id:2 ‖ part:1 (0 or 1) ‖ total_len:2 ‖ bytes` (big-endian). A frame over 1428 bytes is split across two datagrams ([ARCHITECTURE §6](ARCHITECTURE.md#6-packet-sizes)) |
| `0x02 KEEPALIVE` | empty |
| `0x10 CTL` | one control message: `msg:1 ‖ len:2 (big-endian) ‖ payload` (§6) |

**Roaming.** When a packet authenticates from a new source address, that address becomes the peer's endpoint.
Only an authenticated packet can move an endpoint.

## 3. Sessions and timers

| Timer | Value | Meaning |
|---|---|---|
| `KEEPALIVE` | `--keepalive`, default 25 s | if nothing has been *sent* for that long, send a keepalive (holds NAT mappings open) |
| `REKEY_AFTER` | `--rekey`, default 120 s | the session's initiator starts a new handshake; the other end starts one 10 s later if none has come. The old keys keep working until the new ones are confirmed |
| `SB_REKEY` | `--sb-rekey`, default 600 s | `REKEY_AFTER` for a session with a switchboard, on both ends (`REJECT_AFTER` scales with it) |
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
most **one** partial message per source address, for at most 1 s, in one of 512 slots found by a per-boot keyed hash of the
source, so an attacker's packets push out other slots only at random. An `HS_RESP` fragment gets a slot only if a handshake of ours
is waiting for that `hs_id`, and a reassembled one draws on the same budget as an `HS_INIT`.

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
2 per second; an IPv6 /64 is one source, and sources are told apart by a per-boot keyed hash so two addresses cannot be chosen to share a
bucket). The cookie goes back out of the socket the fragment came in on, IPv6 included. The `mac2` field at the end of the reassembled message is the same cookie MAC over the whole message
and is checked as well, but only while the node is under load. The `hs_id` is chosen by the initiator and echoed in the response,
which is how a reply is matched to its attempt. Each retry builds a completely fresh handshake (new ephemerals, a
larger `tai64n`), so a retransmission is never mistaken for a replay.

**Who initiates?** The dialer makes the first handshake. After that the session's initiator rekeys at `REKEY_AFTER`
and the other end a little later if nothing has come (10 seconds, or a quarter of the interval if that is shorter, so it
always falls well inside the hard limit of 1.5 intervals); it does not start one while it is answering the other end's, with
that session's first `DATA` still to come. A responder never sends `DATA` on a new session until it has received the first
`DATA` from that session's initiator; that first packet is the key confirmation.

**Crossed handshakes.** If both ends start a handshake at once, as when each is told the other's address, the one whose
initiator has the lower key id stands. A node that has a handshake of its own in flight and receives the other's `INIT`
compares the two key ids: if its own is lower it ignores the `INIT` (the other end will drop its handshake when ours reaches it);
if the other's is lower it forgets its own and answers. Both ends reach the same decision without a word, so exactly one
session is made. Without this, each end finished its own handshake and discarded the session it had made for the other's, and
the two were left on different sessions until the dead-peer timer or the next rekey: about one start in three when both ends
were given each other's address. `tests/start.sh` starts twelve pairs together; `tests/crossed.sh` delays every datagram 0.8 s
and holds the link through two rekeys.

Sessions with a switchboard rekey on a longer interval, `--sb-rekey` (default 600 s, the same on the node and the
switchboard), to keep a large switchboard's handshake rate low; every other interval is as for any session.

## 4. NAT traversal

Every call tries the **direct** path. The switchboard is involved only until a path is up.

**Candidates.** Each side gathers these and sends them through the switchboard in `DIAL` / `ANSWER`:

| Candidate | Source |
|---|---|
| host | each local address that is up (not loopback, not link-local; loopback only with `--cand-loopback`), IPv4 or IPv6, with our UDP port of that family. Finds a peer on the same LAN, and, for a global IPv6 address, a peer anywhere with no punching at all |
| reflexive | the address the switchboard observed the node at, learned from `NUMBER` and `HBACK` |
| mapped | the public address and port the home router agreed to keep for our UDP port, by NAT-PMP (RFC 6886): the node asks the default gateway (UDP 5351) for its public address and for a mapping of its own port for an hour, and renews halfway. Best effort: three tries, then again in half an hour. Type 3 on the wire |

The switchboard adds the address it sees each side at to the list it passes on.

On the wire a list is a count, then entries of `len:1 ‖ type:1 ‖ port:2 ‖ address:len-3` (an IPv4 entry is 8 bytes
with `len` 7). A reader skips an entry whose length or type it does not know, so an address family added later
does not break the entries around it ([ADR 0025](adr/0025-every-candidate-carries-its-own-length.md)). The top bit of the
count byte says the sender is behind a **strict** NAT (one that makes a new mapping for every destination); the switchboard
carries it from `DIAL` to `RING` and from `ANSWER` to `GO`, and each end uses it to decide who sprays and who probes
(below). It costs no bytes, which matters: a `RING` with a bundle and an admit is already near one datagram.

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
`no direct path (…)`, and nothing is left behind: the reason says that this network's NAT is strict when the node has measured
that, and otherwise asks whether both sides are behind strict NAT. (A strict NAT against an easy one gets longer, for spray.) Two strict (symmetric) NATs therefore
fail cleanly, as the symmetric-NAT tests show (a symmetric NAT facing a cone NAT, and two symmetric NATs).

### The harder rungs *(relay, path upgrade and spray are built)*

The full design is a ladder, each rung tried when the one before fails:

```
 IDLE ─dial─► SIGNAL ──GO──► PUNCH ──ok──► HANDSHAKE ──► DIRECT ◄──────────┐
                              │ 8 s                                        │ upgrade
                              ▼                                            │ succeeds
                   one side EIM, other EDM? ──yes──► SPRAY ──ok──► HANDSHAKE
                              │ no                      │ 14 s
                              ▼                         ▼
                            RELAY ◄─────────────────────┘ ─── retry DIRECT every 60 s → 10 min
```

- **IPv6** *(built)*: a node holds a second socket for it ([ADR 0027](adr/0027-ipv6-is-a-second-socket.md)); a global address is a host candidate and a path between two such nodes needs no punching. The switchboard may be reached over either family; one reached over IPv6 sends zero in the IPv4 address field of `NUMBER` and `HBACK`.
- **More candidates:** PCP, and relay candidates (public members of the line that
  offer relaying) *(planned: M7)*.
- **Mapping type** *(built)*. A switchboard also listens on its port plus one. A node sends that port an `ECHO`
  (`0x07 ‖ nonce:8`, padded to 40 bytes) and gets back `0x07 ‖ nonce:8 ‖ ip:4 ‖ port:2`, the address it was seen at.
  The reply is shorter than the request, so it cannot be used to amplify. If the port is the one the main port saw,
  the NAT is endpoint-independent (EIM, "easy"); if it differs, endpoint-dependent (EDM, "hard"). The node reports it
  as `nat` (`easy`, `strict` or `unknown`) in its status, measures again every ten minutes, and says so when a call
  fails because of it. A switchboard behind a firewall must let the second port through, or nodes show `unknown`.
- **SPRAY** *(built)* (one EIM side, one EDM side). The sender's NAT type travels in the candidate list (the count byte's top bit above), so each end knows the other's. Two seconds after `GO` starts the ordinary punch, if no `PUNCH` has
  come back: the strict end opens 256 extra sockets, each sending a `PUNCH` to the easy end's reflexive (and mapped)
  address, which creates 256 mappings on its NAT; the easy end sends `PUNCH` at 100 packets a second to random ports
  (1024–65535) of the strict end's public IP. A probe that lands on one of the mappings reaches that socket; the strict
  end verifies it as it would any `PUNCH` and answers from that socket, which makes it the strict end's own socket for
  this peer (the other 255 are closed): everything it sends to that peer afterwards, handshake and data, leaves from it,
  and it is polled alongside the node's own. The chance of a hit after *n* probes is `1 − (1 − 256/64512)ⁿ`, about **98%
  at 1,024 probes**, in roughly 10 seconds. The window is 14 s; the call fails at 17 s after `GO`, as it does without
  spray. Spraying isn't attempted when *both* sides are strict, because the odds are hopeless, so that case goes straight
  to relay.
- **Path upgrade** *(built)*. A relayed pair keeps trying for a direct path, by an ordinary call through the switchboard
  every 60 s, doubling to 10 min (`--direct-retry`). When the punch lands, each end sends copies of its session traffic to
  the punched address as well as through the relay; the first authenticated datagram that arrives from the other end
  itself ends the relay on that side, and the other side follows on seeing it. Session keys don't change when the path does.

**Optional parts.** `DIAL`, `RING`, `REGISTER` and `NUMBER` end with optional parts, each `tag:1 ‖ len:1 ‖ bytes`, read to the end of
the message and skipped if the tag is unknown: tag 1 is a line admit (145 bytes), 2 a name (at most 32 bytes of UTF-8, no
control characters), 3 "listed" (no bytes), 4 "this registrant is a switchboard" (`REGISTER`, no bytes) and 5 the registry's code for it (`NUMBER`, ASCII digits). The switchboard adds the caller's name to `RING` itself, from what the caller
registered, if it carries names.

## 5. Relay *(built, M3 stage 2)*

A relay is an ordinary member of the line that has a public address (`--relay auto`, the default: the address the
switchboard sees is one the node holds) or was told to relay (`--relay yes`; `no` never). **The switchboard never relays.**
A node relays only between two members of its own line that it has a direct session with, and it never relays a relayed
leg (no chains).

1. After two direct dial attempts to a member have failed (about 15 s), A asks each connected member in turn, every 6 s,
   over their own session: `RELAY_REQ{key_id_C}`.
2. R, if it will relay and has C on the line with a direct session, allocates two 24-bit route ids, one per direction,
   and sends `RELAY_OK{role ‖ peer_key_id ‖ route ‖ peer_bundle}` to A (role 1, A dials) and to C (role 0). Otherwise
   `RELAY_NO{key_id_C}`.
3. A and C check that the bundle hashes to the key id and that the peer is on their line, then add the other as an
   ordinary peer whose datagrams are all wrapped `0x05 ‖ route:3 ‖ <inner datagram>` to R. The handshake, sessions,
   rekeying and the line protocol then run unchanged between A and C, through R.
4. R looks the route up and checks that the source address is the one it authenticated for that route's owner, swaps the
   route id for the other direction's and forwards. That's it. An end of the pair accepts a wrapped datagram only from
   the relay it chose, on the route id it was given, and it never takes the peer's address from one.

R sees only sizes and timing: the inner datagram is A↔C's own end-to-end session, whose keys R does not have. The overhead
is 4 bytes (a full-size frame is cut to `FRAME_SINGLE`, 1428 bytes, so it still fits a PPPoE link once wrapped). A relay
holds 64 routes (two per pair), forgets one after 60 s idle, and limits each route to about 10,000 datagrams a second, so
it is not a free amplifier. When the relay's session dies or the relay leaves, the relayed peers go with it, a
`call-lost` is reported and the maintainer starts again.

*Known limit:* the handshake's cookie reply (`COOKIE`) to a relayed `HS_INIT` is not wrapped, so a node under handshake
load cannot be reached through a relay until the load passes.

## 6. Control messages and lines

A member is named by its local number *and the key id of the switchboard that number belongs to* ([ADR 0024](adr/0024-a-full-number-is-a-switchboard-code-and-a-local-number.md)).
Today every member shares one switchboard; a member naming another is ignored until federation.

A `CTL` message is `msg:1 ‖ len:2 ‖ payload` inside a session. The messages with a switchboard (`0x20`–`0x2f`) are in
[SWITCHBOARD](SWITCHBOARD.md). Between two nodes:

| msg | name | body |
|---|---|---|
| `0x01` | `LINEHELLO` | `has_line:1 ‖ line_id:8`, sent by each end when a call comes up |
| `0x02` | `LINE` | `line_id:8 ‖ founder:32 ‖ line_key:32 ‖ authority:32 ‖ admit:145 ‖ n:1 ‖ n × (number:9 ‖ key_id:32 ‖ ed_pub:32 ‖ role:1 ‖ switchboard_key_id:32)`: the line, handed to a new member. `authority` is the operator's public key, or all zero for a line run by its members |
| `0x03` | `MEMBER` | `number:9 ‖ key_id:32 ‖ ed_pub:32 ‖ role:1 ‖ switchboard_key_id:32 ‖ admit:145`: a new member, announced to the others |
| `0x04` | `EVICT` | `line_id:8 ‖ key_id:32 ‖ time:8 ‖ evictor:32 ‖ sig:64`; on a line with an operator `evictor` is the operator's public key |
| `0x05` | `LINEKEY` | `line_id:8 ‖ key:32`: the new line key, inside the pairwise session |
| `0x06` | `RELAY_REQ` | `key_id:32`: please carry my packets to this member (§5) |
| `0x07` | `RELAY_OK` | `role:1 ‖ key_id:32 ‖ route:3 ‖ bundle:1249` (§5) |
| `0x08` | `BYE` | `leave:1`: hanging up; `1` also leaves the line |
| `0x09` | `PMTU` | probe result *(planned: M7)* |
| `0x0a` | `RELAY_NO` | `key_id:32`: this node will not relay to that member (§5) |

### Lines and party lines

A **line** is one L2 segment: a 64-bit random `line_id` (shown as its first 6 hex digits, `7f3a9c`), a **founder**, a
32-byte **line key** that every member holds, and a list of **members** (number, key id, Ed25519 public key, role).
The line key never encrypts data, since sessions are always pairwise; it derives the secret that boxes of one site use
for their probes ([ARCHITECTURE §4](ARCHITECTURE.md#4-two-boxes-on-one-segment-site-groups)), unless `--site-key` is
given. Lines persist in `~/.longwire/lines`: the line id, founder, line key (sealed under the seed), the node's own
admit, the members, and the evictions. A node is on at most one line; at most 16 nodes are on one.

**Admits.** An admit is the proof that a member vouched for a node:
`line_id:8 ‖ key_id:32 ‖ role:1 ‖ admitted_by:32 ‖ time:8 ‖ sig:64` (145 bytes), the signature being Ed25519 by
`admitted_by` over the 81 bytes before it. It is valid for a node that holds the line if the `line_id` matches, the
`admitted_by` key is in the *persisted* member list (not merely connected right now), the signature verifies against
the key stored for that member, and the `time` is later than any eviction recorded for that `key_id`. The **founder**
admits itself (`admitted_by` = `key_id` = the founder), and members accept a self-admit only for the recorded founder, so
every member holds an admit, the first included. An admit rides in the `DIAL` and `RING` messages as an optional
part (tag 1, length 145, then the admit; absent when there is none); the switchboard passes it through without looking at it.

**Joining.** When a call comes up, each end sends `LINEHELLO`. Then:

1. If neither end is on a line, the callee founds one (new id and key, itself founder and first member), admits the
   caller and sends it `LINE`.
2. If exactly one end is on a line, that end is the hub: it admits the other and sends it `LINE`, and tells every
   member it is directly connected to about the newcomer with `MEMBER`.
3. If both ends are on the *same* line, nothing happens: this is a mesh edge. If they are on different lines, the call
   ends with `BYE` and the dialer is told `that node is on another line`.
4. A newcomer that receives `LINE` dials every member it is not connected to, through the switchboard, presenting its
   admit. A member that holds a valid admit for the caller reports `admit: true` in the `ring` event; the node script
   picks up such rings under every policy except `never`, so a mesh builds with no prompt on any member.

Every node on a line keeps dialing members it has no session with, with a back-off (8 s, doubling to 60 s), so a mesh
heals after a restart or a drop and a restarted member rejoins on the admit it persisted. If two members dial each other
at once, the call from the node with the lower key id stands and the other is answered busy.

**Partial meshes.** Split horizon assumes a full mesh: a frame from A that reaches B is never forwarded to C. If a pair
cannot connect directly, it is relayed (§5); if no member will relay, that pair is partitioned, silently. So a node reports it: the
status lists members against sessions up, and `line-partial` fires if the mesh is still incomplete 30 seconds after the
node joined (`line-complete` when it completes).

**Leaving.** `longwire hangup` sends `BYE` with `leave` set to every peer and forgets the line; the line goes on while
any member remains.

**Eviction.** `longwire evict NUMBER` signs `EVICT` and sends it to every member, which verifies the signature against
the evictor's stored key, drops the member and its sessions, and records `(key_id, time)` so no older admit can bring it
back. The evictor also draws a new line key and sends `LINEKEY` to the remaining members, so the evicted node can no
longer forge probes; a site accepts probes under the previous key for a short grace period so a rotation doesn't flap
a site's boxes. Any member can evict any other: a line is a cable into everyone's LAN, and its members trust each other
to that extent ([THREAT-MODEL](THREAT-MODEL.md)).

**A line with an operator** ([ADR 0026](adr/0026-a-line-may-have-an-operator.md)). The founder names an Ed25519 public key
(`up --authority`) and founds the line at once. From then on an admit is valid only if that key signed it (its public key
is in the admit's signer field) or it is the founder's own, and a member's admit is not. A newcomer brings its admit
(`up --admit`) in the `DIAL`; the member it calls checks the admit it was *given* and passes that one on in `LINE` and
`MEMBER`; it signs nothing. An `EVICT` is valid only with the operator's signature, whichever member carries it
(`longwire revoke`); a member's own `evict` is refused. Every member keeps the operator's signed evictions and sends them
to each member it links to, two seconds after the link comes up and every 30 seconds after, since datagrams are not
acknowledged: a member that was offline hears of a revocation from whoever it meets first. The operator's seed signs on
a machine that runs no node: `lw key authority|admit|evict` print the public key, an admit or an eviction.
