# The switchboard

> *It puts the call through, then it hangs up.*

The switchboard is component 1: a hibr script, `longwire-switchboard`, on the same module as every node. The
module's C worker (`lw sb start`) handles sessions, handshakes, `HELLO`, cookies, heartbeats, presence and
the bundle store. The script makes the decisions: who gets which number, which tokens are valid, who is rung and
when to punch, and what is rate-limited. The switchboard does three jobs and nothing else:

1. **Numbers.** It hands out a short number and binds it to a node's key, first come, for good.
2. **Presence.** It remembers where each registered node can currently be reached.
3. **Introductions.** It rings the callee, relays the answer, and tells both sides when to punch.

**It never carries traffic, never relays, and never holds a key that can read anything.** Once two nodes are
connected, the switchboard can go away and the call stays up.

## Numbers

```
447-120-933        9 digits: 8 random + 1 Damm check digit
```

- **Random, not sequential**, so numbers don't leak how many users there are or when someone joined, and can't be
  walked.
- **The check digit** (Damm algorithm) catches every single-digit typo and every swap of adjacent digits before
  anything goes on the wire.
- **Bound to a key.** A number is the switchboard's record `number → bundle`. Re-registering with the same key
  returns the same number. A different key can never take it.
- **Portable.** Your number follows your 32-byte seed. Restore the seed on a new machine and you have your number.
- **Tokens (optional).** A private switchboard can require an invite token to register (`--tokens FILE`). A token
  can also carry a reserved number, so "only that token gets that number": the first key to present the token
  claims the number, and the token is spent.
- **Release.** A number unused for 365 days (configurable) is released. If it's later re-assigned, everyone who
  pinned the old key sees `KEY CHANGED for 447-120-933` and the call is refused until they accept the change.
- **The full number** (`lw1…`, 55 chars) is the key ID itself. Dialing it skips the number lookup and trusts the
  switchboard for nothing except presence.

## Talking to the switchboard

A node opens an ordinary longwire session to the switchboard, using the [same handshake](CRYPTO.md#4-the-handshake-hybrid-noise-ik-with-kems).
By default that's the **public switchboard you operate** ([DECISIONS D14](DECISIONS.md#d14)). Its address and full
key are a default in the node script, so a catalogue update can move or re-key it. Any node can point elsewhere
with `--switchboard host:port/lw1…`. Either way the node
holds only a key ID at first, so it fetches the switchboard's bundle with
[`HELLO`](PROTOCOL.md#1-datagram-types) and checks it against that ID before the handshake. Messages are `CTL`
messages `0x20–0x2F` inside that session:

| msg | id | Direction | Body | Meaning |
|---|---|---|---|---|
| `REGISTER` | `0x20` | node → sb | token | → `NUMBER` or `REFUSED`. The node's bundle is already known from its session |
| `NUMBER` | `0x21` | sb → node | number(9), observed address | the node's number, and the address the switchboard saw it at (its reflexive candidate) |
| `HEARTBEAT` | `0x22` | node → sb | candidates | every `--keepalive` s (default 25), which also holds the NAT mapping open → `HBACK` |
| `HBACK` | `0x23` | sb → node | observed address | refreshes the reflexive candidate |
| `DIAL` | `0x24` | caller → sb | number, caller candidates | the switchboard rings the callee, or answers `REJECTED` |
| `RING` | `0x25` | sb → callee | call id, caller number, caller bundle, candidates | the phone rings |
| `ANSWER` | `0x26` | callee → sb | call id, accept \| reject \| busy, candidates | |
| `GO` | `0x27` | sb → both | call id, role, delay, peer number, `call_token`, peer bundle, candidates | start punching ([PROTOCOL §4](PROTOCOL.md#4-nat-traversal-the-ladder)). The caller learns the callee's bundle here, so this is where pins are checked |
| `REJECTED` | `0x28` | sb → caller | call id, reason | unknown number, not online, declined, busy, refused by policy, no answer |
| `REFUSED` | `0x2f` | sb → node | reason text | registration refused |

The switchboard adds its own observation to every candidate list (what it sees of each side), since that is the best
candidate there is. A call lasts only as long as it takes to ring: `GO` is sent and the record is erased.

**Policy is a script's decision.** With `--policy script` the worker emits an event for each `REGISTER` (from a key
it hasn't seen) and each `DIAL`, and waits up to 2 s for `lw sb reply ID accept|refuse` before refusing. The shipped
`longwire-switchboard` script answers with its policy:

| Option | Default | Does |
|---|---|---|
| `--tokens FILE` | open registration | a token per line, `TOKEN` or `TOKEN NUMBER` (that token reserves that number). A token is spent when used. A spent or unknown token: `a valid token is needed to register here` |
| `--per-ip-hour N` | 20 | registrations per source address per hour |
| `--rings-per-minute N` | 6 | rings per callee per minute |

Without `--policy script` (the engine's own default), the worker accepts everything.

**Not built yet** (listed in the [ROADMAP](ROADMAP.md)): the `ECHO` port and NAT-type detection, `LOOKUP` and
`longwire who`, number release after a year unused, and the longer rekey interval for switchboard sessions. A
number, once issued, is yours for good.

**After `GO`, the switchboard forgets the call.** It keeps no call log in memory or on disk. Logging is off by
default, and `--log calls` exists for operators who need it, with a warning.

## What it sees

| It sees | It never sees |
|---|---|
| that a number is online, and its public IP:port | any frame, any traffic volume after `GO`, or how long a call lasts |
| who dials whom, and when (until it forgets) | line membership, line keys, MAC or IP addresses inside the line |
| bundles (public keys) | seeds, session keys, call contents |
| NAT mapping type | anything about relayed traffic, because relays are line members, not the switchboard |

## How tiny

- **State:** one 1.3 KB record per number (the bundle, plus timestamps) and a few dozen bytes of presence per online
  node. 100,000 numbers take about 130 MB of RAM. The number table is persisted as an append-only file
  (`numbers.log`) and compacted on start.
- **Work:** one handshake per node every **10 minutes** (switchboard sessions rekey slowly, because they only carry
  control messages), one heartbeat every 25 s, plus calls. 100,000 online nodes is about 170 handshakes/s. At
  roughly 3 ms of responder CPU per handshake under TCC, that's about half a core, so a $3 VPS handles tens of
  thousands of nodes. The switchboard's handshake budget is **2,000/s**, against 50/s for an ordinary node. Cookies are
  asked for only once more than half of it is spent, so in normal operation no node pays the extra round trip.
- **Abuse limits:** registrations per IP per hour, dials per number per minute and rings per callee per minute. A
  dial to an unknown number and a dial that's refused get the same response time.
- **Many switchboards.** v1 has one switchboard per deployment, so numbers are local to it. Federation (`number@sb`)
  is a roadmap item, not a v1 promise.

## Running one

```
$ longwire-switchboard --listen 0.0.0.0:1877
  switchboard  lw1q7m…k2a   (give this full address to your nodes)
  listening    udp 1877, echo 1878
  numbers      0 registered      tokens: open registration
```

It needs hibr and the module, runs as an ordinary user, needs no capabilities (1877 and 1878 are above 1024), and
keeps everything in `~/.longwire/switchboard/`. Since the policy is a script, changing it (vanity numbers, a
different token scheme, an allow-list) means editing hibr, not C.
