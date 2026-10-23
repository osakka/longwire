# The switchboard

> *It puts the call through, then it hangs up.*

The switchboard is component 1: a hibr script, `longwire-switchboard`, on the same module as every node. The
module's C worker (`lw sb start`) is the mechanism: sessions, handshakes, `HELLO`, cookies, heartbeats, presence, the
number registry and the call introductions. The script is the policy: whether a key may register, and whether a ring
goes through. The switchboard does three jobs and nothing else:

1. **Numbers.** It hands out a short number and binds it to a node's key, first come, for good.
2. **Presence.** It remembers where each registered node can currently be reached.
3. **Introductions.** It rings the callee, passes on the answer, and tells both sides when to punch.

**It never carries traffic, never relays, and never holds a key that can read anything.** Once two nodes are
connected, the switchboard can go away and the call stays up.

## Numbers

```
447-120-933        9 digits: 8 random + 1 Damm check digit
```

- **Random, not sequential**, so numbers don't leak how many users there are or when someone joined, and can't be
  walked.
- **The check digit** (Damm algorithm) catches every single-digit typo and every swap of adjacent digits; a node
  refuses such a number before anything goes on the wire.
- **Bound to a key.** A number is the registry's record `number → bundle`. Registering again with the same key
  returns the same number, and a different key can never take it.
- **Portable.** Your number follows your 32-byte seed: restore the seed on a new machine and you have your number
  back, as long as the switchboard still has its record.
- **Tokens (optional).** A private switchboard can require an invite token to register (`--tokens FILE`). A token
  can carry a reserved number, so only the key that presents that token can have that number; the token is spent
  when it is used.
- **Never released.** A number, once issued, is kept for good. Releasing one that has been unused for a year *(planned:
  M3)* needs the time it was last seen to be stored, which the registry doesn't do yet. A re-issued number would show
  up on every node that pinned the old key as `key-changed`, and the call would be refused.
- **The full number** (`lw1…`, 55 characters) is the key id in base32. A switchboard is given by it
  (`--switchboard host:port/lw1…`; 64 hex digits also work). Dialing a full address instead of a number
  *(planned: M3)*.

## Talking to the switchboard

A node opens an ordinary longwire session to the switchboard, using the [same handshake](CRYPTO.md#4-the-handshake-hybrid-noise-ik-with-kems).
Today `--switchboard HOST:PORT/KEY` is required; the default public switchboard you operate
([ADR 0014](adr/0014-a-default-public-switchboard.md)) will be a default in the node script once its host and key are known, so a
catalogue update can move or re-key it. The node holds only a key id at first, so it fetches the switchboard's
bundle with [`HELLO`](PROTOCOL.md#1-datagram-types), checks it against that id, and introduces itself the same way
before the handshake. Messages are `CTL` messages inside that session:

| msg | id | Direction | Body | Meaning |
|---|---|---|---|---|
| `REGISTER` | `0x20` | node → sb | token | → `NUMBER` or `REFUSED`. The node's bundle is already known from its session |
| `NUMBER` | `0x21` | sb → node | number(9), observed address | the node's number, and the address the switchboard saw it at (its reflexive candidate) |
| `HEARTBEAT` | `0x22` | node → sb | candidates | every `--keepalive` s (default 25), which also holds the NAT mapping open → `HBACK` |
| `HBACK` | `0x23` | sb → node | observed address | refreshes the reflexive candidate |
| `DIAL` | `0x24` | caller → sb | number, caller candidates | the switchboard rings the callee, or answers `REJECTED` |
| `RING` | `0x25` | sb → callee | call id, caller number, caller bundle, candidates | the phone rings |
| `ANSWER` | `0x26` | callee → sb | call id, accept \| reject \| busy, candidates | |
| `GO` | `0x27` | sb → both | call id, role, delay, peer number, `call_token`, peer bundle, candidates | start punching ([PROTOCOL §4](PROTOCOL.md#4-nat-traversal)). The caller learns the callee's bundle here, so this is where pins are checked |
| `REJECTED` | `0x28` | sb → caller | call id, reason | unknown number, not online, declined, busy, refused by policy, no answer |
| `REFUSED` | `0x2f` | sb → node | reason text | registration refused |

A node counts as **online** if its session is up and it has been heard from in the last 80 seconds. The switchboard
adds its own observation (the address it sees each side at) to every candidate list, since that is the best candidate
there is. A ring that nobody answers is dropped after 65 seconds with `REJECTED` (no answer).

**Policy is a script's decision.** With `--policy script` the worker emits an event, and waits up to 2 seconds for the
script's verdict before refusing:

- `register`: a key it has not seen before wants a number (`id`, `key_id`, `ip`, `token`). The script answers
  `lw sb reply ID accept [NUMBER]` or `lw sb reply ID refuse REASON...`.
- `dial`: a registered node rings another (`id`, `from`, `to`, `ip`). The script answers `lw sb reply ID accept` or
  `lw sb reply ID refuse`.

A key that is already registered gets its number back without asking. The shipped `longwire-switchboard` script
answers with this policy:

| Option | Default | Does |
|---|---|---|
| `--listen ADDR:PORT` | `0.0.0.0:1877` | where to listen (UDP) |
| `--tokens FILE` | open registration | a token per line, `TOKEN` or `TOKEN NUMBER` (that token reserves that number). A token is spent when used. A spent or unknown token: `a valid token is needed to register here` |
| `--per-ip-hour N` | 20 | registrations per source address per hour |
| `--rings-per-minute N` | 6 | rings per callee per minute |
| `--state DIR` | `~/.longwire/switchboard` | the seed, the control socket, `numbers.log` |
| `--max-nodes N` | 1024 | how many node sessions the worker holds at once |

Without `--policy script` (the engine's own default), the worker accepts everything.

**After `GO`, the switchboard forgets the call.** The call record is erased when `GO` is sent. Nothing logs who called
whom: the registry on disk (`numbers.log`) holds numbers and keys, never calls.

Everything the switchboard does not do yet is marked where it is described and listed under its milestone in the
[ROADMAP](ROADMAP.md): NAT-type detection (`ECHO`) and relaying *(planned: M3)*, `LOOKUP` and `longwire who` *(planned: M3)*,
number release *(planned: M3)*, and a longer rekey interval for switchboard sessions *(planned: M3)*.

## What it sees

| It sees | It never sees |
|---|---|
| that a number is online, and the public IP:port it connects from | any frame, any traffic volume after `GO`, or how long a call lasts |
| who rings whom, and when (until `GO`, then it forgets) | MAC or IP addresses inside the line |
| bundles (public keys) and the numbers they hold | seeds, session keys, call contents |

## How tiny

- **State:** the registry is one record per number (the 1,249-byte bundle and the number), kept in memory and in an
  append-only file, `numbers.log`, that is read when the worker starts. A live session costs about the size of one
  peer slot (roughly 10 KB); the worker holds **1,024** by default (`--max-nodes`), and when the table is full it
  recycles the stalest slot that has no session.
- **Work:** one handshake per node every 2 minutes (the default rekey), one heartbeat every `--keepalive` seconds,
  plus calls. The handshake budget is **2,000/s** against 50/s for an ordinary node, and cookies are asked for only
  once more than half of it is spent ([PROTOCOL §3](PROTOCOL.md#3-sessions-and-timers)), so in normal operation no node
  pays the extra round trip. The handshake is about 1.5 ms of CPU under TCC.
- **Abuse limits:** registrations per source address per hour and rings per callee per minute (the script), the
  handshake budget (the worker), and a limit on pending decisions.
- **One switchboard per deployment.** Numbers are local to it. Federation (`number@sb`) *(planned)*.

## Running one

```
$ longwire-switchboard --listen 0.0.0.0:1877
switchboard  lw1q7m…k2a   (give this full address to your nodes)
listening    udp 0.0.0.0:1877    tokens: open registration
```

It needs hibr and the module, runs as an ordinary user, needs no capabilities (1877 is above 1024), and keeps
everything in `~/.longwire/switchboard/`. Since the policy is a script, changing it (vanity numbers, a different token
scheme, an allow-list) means editing hibr, not C.
