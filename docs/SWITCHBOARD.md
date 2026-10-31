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

- **Random, not sequential**, so numbers don't leak how many users there are or when someone joined. A registered node
  can still ask whether a number exists (the answers differ: unknown, or not online), so enumeration is slow and noisy
  rather than impossible: a node gets ten such answers a minute (a dial to a number that is unknown or not online), and after
  that is told only "refused by the switchboard", whichever it was.
- **The check digit** (Damm algorithm) catches every single-digit typo and every swap of adjacent digits; a node
  refuses such a number before anything goes on the wire.
- **Bound to a key.** A number is the registry's record `number → bundle`. Registering again with the same key
  returns the same number, and a different key can never take it.
- **Portable.** Your number follows your 32-byte seed: restore the seed on a new machine and you have your number
  back, as long as the switchboard still has its record.
- **Tokens (optional).** A private switchboard can require an invite token to register (`--tokens FILE`). A token
  can carry a reserved number, so only the key that presents that token can have that number; the token is spent
  when it is used.
- **Released after a year unheard, then held.** The registry keeps the time each number was last heard (a `seen` line in
  `numbers.log`, at most once an hour per node). A number unheard for `--release-after` (a year) is released: it stops
  ringing and the switchboard emits `number-released`. It is then **held back** for as long again, and no stranger can
  be given it in that time, so a pin somewhere cannot meet a different key by surprise. If its own key comes back
  while it is held, it gets the same number. After the hold the number can be minted again; a node that pinned the old
  key then sees `key-changed` and the call is refused. Names stay bound to their keys through all of this.
- **The full address** (`lw1…`, 55 characters) is the key id in base32. A switchboard is given by it
  (`--switchboard host:port/lw1…`; 64 hex digits also work). Dialing a full address instead of a number
  works when the switchboard has a directory and the node is listed in it ([ADR 0023](adr/0023-names-are-opt-in-claims.md)).

## Talking to the switchboard

A node opens an ordinary longwire session to the switchboard, using the [same handshake](CRYPTO.md#4-the-handshake-hybrid-noise-ik-with-kems).
Today `--switchboard HOST:PORT/KEY` is required; the default public switchboard you operate
([ADR 0014](adr/0014-a-default-public-switchboard.md)) will be a default in the node script once its host and key are known, so a
catalogue update can move or re-key it. The node holds only a key id at first, so it fetches the switchboard's
bundle with [`HELLO`](PROTOCOL.md#1-datagram-types), checks it against that id, and introduces itself the same way
before the handshake. Messages are `CTL` messages inside that session:

| msg | id | Direction | Body | Meaning |
|---|---|---|---|---|
| `REGISTER` | `0x20` | node → sb | token, then optional parts: name, listed; or, from a switchboard asking a registry for its code, "switchboard" | → `NUMBER` or `REFUSED`. The node's bundle is already known from its session |
| `NUMBER` | `0x21` | sb → node | number(9), observed address, flags (caller ID offered, directory offered), optional parts: the switchboard's assigned code. To a switchboard registering with a registry, the number is all zeros and the code is its own | the node's number, and the address the switchboard saw it at (its reflexive candidate) |
| `HEARTBEAT` | `0x22` | node → sb | candidates | every `--keepalive` s (default 25), which also holds the NAT mapping open → `HBACK` |
| `HBACK` | `0x23` | sb → node | observed address | refreshes the reflexive candidate |
| `DIAL` | `0x24` | caller → sb | number, caller candidates, optional parts (admit) | the switchboard rings the callee, or answers `REJECTED`. The admit is an optional part (tag 1, length 145) that the switchboard passes on untouched |
| `RING` | `0x25` | sb → callee | call id, caller number, caller bundle, candidates, optional parts (admit, caller name) | the phone rings. The switchboard never reads the admit and does not know lines |
| `ANSWER` | `0x26` | callee → sb | call id, accept \| reject \| busy, candidates | |
| `GO` | `0x27` | sb → both | call id, role, delay, peer number, `call_token`, peer bundle, candidates | start punching ([PROTOCOL §4](PROTOCOL.md#4-nat-traversal)). The caller learns the callee's bundle here, so this is where pins are checked |
| `REJECTED` | `0x28` | sb → caller | call id, reason, callee number | unknown number, not online, declined, busy, refused by policy, no answer |
| `LOOKUP` | `0x29` | node → sb | kind (key id or name), query | the directory ([ADR 0023](adr/0023-names-are-opt-in-claims.md)): → `FOUND` |
| `FOUND` | `0x2a` | sb → node | status (ok, no directory, slow down), query, up to 8 × (number, key id, name) | only nodes that chose to be listed. A node may look up once a second, with a burst of 5 |
| `REFUSED` | `0x2f` | sb → node | reason text | registration refused |

A node counts as **online** if its session is up and it has been heard from in the last 80 seconds. The switchboard
adds its own observation (the address it sees each side at) to every candidate list, since that is the best candidate
there is. A ring that nobody answers is dropped after 65 seconds with `REJECTED` (no answer).

**Policy is a script's decision.** With `--policy script` the worker emits an event, and waits up to 2 seconds for the
script's verdict before refusing:

- `register`: a key it has not seen before wants a number (`id`, `key_id`, `ip`, `net`, `token`, and the `name` it announced and whether it is `listed`). The script answers
  `lw sb reply ID accept [NUMBER]` or `lw sb reply ID refuse REASON...`.
- `register` with `switchboard` true: another switchboard asks this registry for its code (`id`, `key_id`, `ip`, `net`). The shipped script
  allows `--codes-per-ip-day` of them (default 2) per address.
- `dial`: a registered node rings another (`id`, `from`, `to`, `ip`, `net`). The script answers `lw sb reply ID accept` or
  `lw sb reply ID refuse`.

A key that is already registered gets its number back without asking.

**Strikes and bans** ([ADR 0043](adr/0043-strikes-and-bans-count-only-what-cannot-be-faked.md)). A switchboard on the internet is hit by people guessing tokens, probing numbers and sending nonsense. Only what an attacker cannot fake is punished: nothing before the handshake counts (a source
address is spoofable, and a ban on it would be a weapon against your own users); a failure counts only on a session that got through the handshake, and what is banned is that session's **key**. A refused registration is two strikes, a dial of a number that does not exist one, a message over the session's
budget a fraction; strikes leak away, so honest mistakes never add up. A banned key's session ends and its handshake is refused (it looks like any unknown key) for `--ban-for` seconds, doubling on repeats. Several different banned keys from one network refuse *new* identities from it, never the ones
already registered behind it. Fixed tables, a ceiling on bans an hour and everything expiring keep this from being a way to take the switchboard down. `longwire-switchboard bans list` shows them and `bans clear [KEY|all]` lifts them (only the user it runs as, or root for the service); each ban is also written to its log, with the command that lifts it ([ADR 0048](adr/0048-a-silent-switchboard-is-said-so.md)). A node whose key is banned is not told, but it says itself that the switchboard's host answers and the switchboard does not. The `ban` and `net-ban` events tell the script. The shipped `longwire-switchboard` script
answers with this policy:

| Option | Default | Does |
|---|---|---|
| `--listen PORT\|ADDR:PORT\|[ADDR]:PORT` | `1877` | where to listen (UDP): a bare port is IPv4 and IPv6 on any address, `ADDR:PORT` IPv4 only, `[ADDR]:PORT` IPv6 only, and a comma separates several ([ADR 0027](adr/0027-ipv6-is-a-second-socket.md)). It also listens on the next port up (1878) to answer `ECHO`, so a node can tell what kind of NAT it is behind; let both through a firewall, and leave the next port free when running several on one host |
| `--tokens FILE` | open registration | a token per line, `TOKEN` or `TOKEN NUMBER` (that token reserves that number). A token is spent when used. A spent or unknown token: `a valid token is needed to register here`. A token is visible characters with no spaces, quotes, backslashes or leading `#` (1 to 63); anything else is refused before the script sees it. Lines that are blank or begin with `#` are kept and are never tokens. The file is rewritten whole, beside itself, and moved into place, so **the file must be in a directory the service can write** (for the unit, its state directory, `/var/lib/longwire-switchboard`; the unit cannot write `/etc`): if it cannot be written the token is not spent, and the registration is refused |
| `--per-ip-hour N` | 20 | registrations per source per hour (`net` in the event: an IPv4 address, or an IPv6 /64) |
| `--rings-per-minute N` | 6 | rings per callee per minute |
| `--state DIR` | `~/.longwire/switchboard` | the seed, the control socket, `numbers.log`, `names.log` |
| `--max-nodes N` | 1024 | how many node sessions the worker holds at once (at most 65,536). When they are all taken, a session that never registered gives way to a new one; a registered one never does |
| `--max-numbers N` | 100000 | the most numbers it will give out. Each is a bundle (1.3 KB) in memory and a line in `numbers.log`; past it a registration is refused with a plain message |
| `--strikes N` | 10 | failures on an authenticated session within `--strike-window` that earn a ban; 0 turns bans off ([ADR 0043](adr/0043-strikes-and-bans-count-only-what-cannot-be-faked.md)) |
| `--strike-window S` | 600 | seconds over which strikes leak away: slow trickles never add up |
| `--ban-for S` | 900 | the first ban of a key, doubling at each repeat |
| `--ban-max S` | 86400 | the longest a ban gets |
| `--net-ban-keys N` | 5 | different banned keys from one /24 (IPv6: /64) within the window that refuse *new identities* from that network |
| `--net-ban-for S` | 900 | how long that network is refused (doubling, capped by `--ban-max`) |
| `--ban-ceiling N` | 1000 | most bans an hour, so that a flood cannot make the switchboard refuse everybody |
| `--never-ban KEY\|ADDR/PREFIX` | none | never struck (repeatable, 16 at most): a key id (64 hex digits) or an address with a prefix. Nobody is exempt unless you say so |
| `--registry HOST:PORT/KEY\|none` | required | the registry of switchboard codes, which gives this switchboard its short code. `none` for a private switchboard, which keeps its derived code. Required until the project's registry is running |
| `--issue-codes` | off | this switchboard is a registry: switchboards register with it for a code (`codes.log` in `--state` holds who has which) |
| `--public-switchboard KEY` | none | on a registry: the switchboard (`lw1…` or hex) that is given the code `+1` |
| `--release-after TIME` | 365d | how long a number may go unheard before it is released, and held back after (`365d`, `12h`, `90s`; a bare number is days) |
| `--codes-per-ip-day N` | 2 | on a registry: codes asked for per source address per day. The count is kept in memory, so a restart of the registry resets it |
| `--caller-id on\|off` | on | carry the name a caller announced on its rings; off drops names |
| `--directory on\|off` | on | answer lookups for nodes that listed themselves; off answers none |

Without `--policy script` (the engine's own default), the worker accepts everything.

**The registry of switchboard codes.** A registry is an ordinary switchboard started with `--issue-codes`. A switchboard started with
`--registry` registers with it on start, over the same handshake a node uses, saying it is a switchboard; the registry answers with
the next code in the numbering plan ([ADR 0024](adr/0024-a-full-number-is-a-switchboard-code-and-a-local-number.md)) and remembers it
against the switchboard's key, so the same key always gets the same code. The switchboard puts the code in the `NUMBER` it sends to its
nodes, and they show it (`+20 312-884-076`); the derived code (`+0…`) keeps working beside it. A switchboard that is not a registry
refuses to issue codes.

**Watching it.** The worker's event stream (`/dev/longwire/events`) also tells a watcher how calls go: `call-ring` when a ring is sent and `call-go` when the call is put through (each with the two numbers, `from` and `to`), and `call-end` when it ends without one, with a `reason` of declined, busy or no answer. They hold numbers only, are not written anywhere, and are for the operator's own screen: the Switchboard application on hibr's desktop draws them ([ADR 0030](adr/0030-three-surfaces-one-engine.md)). `lw sb nodes` pages through the registry: number, name, listed, online, and seconds since heard. When the switchboard runs as the service, its control socket is `/run/longwire-switchboard/ctl`, given to the group `longwire`: a person in it can watch the switchboard from the desktop and nothing more than the owner of the socket could do ([ADR 0032](adr/0032-the-services-share-their-control-socket-with-a-group.md)).

**After `GO`, the switchboard forgets the call.** The call record is erased when `GO` is sent. Nothing logs who called
whom: the registry on disk (`numbers.log`) holds numbers and keys, never calls.

Everything the switchboard does not do yet is marked where it is described and listed under its milestone in the
[ROADMAP](ROADMAP.md).

## What it sees

| It sees | It never sees |
|---|---|
| that a number is online, and the public IP:port it connects from | any frame, any traffic volume after `GO`, or how long a call lasts |
| who rings whom, and when (until `GO`, then it forgets) | MAC or IP addresses inside the line |
| bundles (public keys) and the numbers they hold | seeds, session keys, call contents |
| a name for each node that gave one, whether it asked to be listed, and (in `names.log`) which key holds each listed name (`lw sb nodes` shows them to its operator) | a name for a node that gave none: nothing is ever invented |

## How tiny

- **State:** the registry is one record per number (the 1,249-byte bundle and the number), kept in memory and in an
  append-only file, `numbers.log`, that is read when the worker starts. A live session costs about the size of one
  peer slot (roughly 10 KB); the worker holds **1,024** by default (`--max-nodes`), and when the table is full a session
  that never registered gives way to a new one (the one silent longest first); a registered session is never put out.
- **Work:** one handshake per node every 10 minutes (`--sb-rekey`), one heartbeat every `--keepalive` seconds,
  plus calls. The handshake budget is **2,000/s** against 50/s for an ordinary node, and cookies are asked for only
  once more than half of it is spent ([PROTOCOL §3](PROTOCOL.md#3-sessions-and-timers)), so in normal operation no node
  pays the extra round trip. A handshake costs a few milliseconds of CPU under TCC (about 5 ms for both ends together, [CRYPTO §6](CRYPTO.md#6-measured)), most of it on the responder.
- **Abuse limits:** registrations per source (an IPv4 address, or an IPv6 /64) per hour and rings per callee per minute (the
  script); the handshake budget, 10 control messages a second from a session, one registration in five seconds, a dial every
  two seconds (the worker); one registration pending per asker and at most a line's worth (16) of dials or calls, 100,000 numbers (`--max-numbers`).
  The script empties its counters every hour and day.
- **One switchboard per deployment.** Numbers are local to it. Federation (`number@sb`), redundancy and a larger number space *(planned: M9)*.

## Running one

```
$ longwire-switchboard --listen 1877 --registry none
switchboard  lw1q7m…k2a   (give this full address to your nodes)
listening    udp 1877    tokens: open registration
```

It needs hibr and the module, runs as an ordinary user, needs no capabilities (1877 is above 1024), and keeps
everything in `~/.longwire/switchboard/`. Since the policy is a script, changing it (vanity numbers, a different token
scheme, an allow-list) means editing hibr, not C.

### In public

A switchboard on the internet is open to anyone who can send a UDP packet; [ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md)
says what was done about that and what is still unknown. What the operator decides:

- **Registration.** Open registration (no `--tokens`) lets anyone take a number, up to `--max-numbers`, at the per-source limit
  (`--per-ip-hour`). A botnet is not stopped by a per-address limit. For a switchboard that matters, hand out tokens and run it
  with `--tokens`.
- **Capacity.** `--max-nodes` sessions and `--max-numbers` numbers are the limits; set both to what the machine's memory affords
  (a session about 10 KB, a number about 1.3 KB). Past them the switchboard refuses; it does not fall over.
- **The service.** Run it as the systemd unit: it is sandboxed (a transient user, no capabilities, no home, a system-call
  filter), restarts itself (`Restart=always`), and its control socket belongs to the group `longwire`, so put in that group only
  people who may stop it. Open UDP 1877 and 1878 (`ECHO`); nothing else.
- **What it cannot do for you.** A flood of plain traffic is for the network in front of it.

