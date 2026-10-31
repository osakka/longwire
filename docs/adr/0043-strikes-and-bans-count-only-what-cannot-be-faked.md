# 0043 — Strikes and bans count only what cannot be faked

Status: accepted; built in 0.5.24.

## Context

A switchboard on the internet is open to anyone who can send a UDP packet. Beyond the cheap rate limits (per source, per session, per number), the operator wanted this: a node that fails too often is cut off, and one that is plainly attacking is dropped completely, without that becoming a way for somebody else to take the switchboard down.
The trap is the usual one: over UDP a source address is not evidence. If "ten bad packets from 1.2.3.4" earned a ban, an attacker could forge packets from your own users' addresses and have them banned.

## Decision

1. **Count only what passed the handshake.** A packet that is not part of a session (wrong key, no valid cookie, garbage, a handshake from a spoofed source) is dropped as before, statelessly, and never counted: the `mac1` check proves the sender knows the switchboard, the cookie proves it receives at the address it claims,
   and the key exchange proves its key. A **strike** is a failure on a session that got that far, and what is banned is that session's **key**, which nobody can forge.
2. **What a strike is.** A refused registration is two (this is what guessing tokens looks like), a dial of a number that does not exist one, a dial over the limit one (a registration repeated within five seconds is ignored, not struck, since 0.5.28: [0048](0048-a-silent-switchboard-is-said-so.md)), a message over the session's budget a quarter, a malformed registration one. Strikes leak away at
   `--strikes` per `--strike-window`, so only a burst or a steady rate adds up. At `--strikes` the key is banned for `--ban-for` seconds, doubling at each repeat (forgotten after `--ban-max`) up to `--ban-max`; its live session is ended with a plain reason, and a new handshake from that key is refused
   after the same work as for any unknown key, so the time says nothing. Defaults 10, 600 s, 900 s, 86400 s; all settable; 0 strikes turns the whole thing off.
3. **A network is refused only for new identities.** When `--net-ban-keys` different keys from one /24 (IPv6: /64) earn bans within the window, a key the switchboard has never registered is refused from that network for `--net-ban-for` seconds. Nodes already registered behind the same address
   (a shared NAT, a café) are not affected: one person on a shared address cannot lock the others out.
4. **It cannot be turned into a weapon.** Fixed hashed tables (2048 keys, 256 networks) that never grow; a new entry takes a free slot or the least valuable one (idle before struck before banned), so a flood of identities cannot push a live ban out of the table; every ban and every strike expires;
   a ceiling on bans an hour (`--ban-ceiling`) stops a flood from making the switchboard refuse everybody; `--never-ban KEY|ADDR/PREFIX` exempts the operator's own nodes (empty by default, so nobody is exempt unless the operator says so). The attacker's cost of a strike is a full hybrid handshake, which is the most expensive thing the
   switchboard asks of a stranger, and the switchboard's own work for it is bounded by the existing handshake budget.
5. **The operator sees and undoes it.** The `ban` and `net-ban` events (owner only, with the key, the address, the seconds, the level and the reason), `longwire-switchboard bans list`, `bans clear [KEY|all]` (the owner's alone, like stopping the service), and counters in the status.

## Consequences

Nothing here makes the switchboard immune to a flood of valid handshakes: that is the handshake budget and the cookie mechanism, which are unchanged. What it adds is that a patient guesser of tokens, a prober of numbers or a session that floods messages is removed after a few tries, at the price of nothing the attacker can reach: the only identities that can be banned are the ones the attacker proves to hold.
Bans are in memory: a restart forgets them. A token that was spent stays spent; there is no "suspended token" because a token is single-use, and the key that guessed is the thing banned. Tested at the level of the tables under TCC's bounds checker (`tests/ban.sh`: thresholds, leak, doubling and the cap, never-ban, networks, the ceiling, a flood of 200,000 identities) and on a real switchboard (`tests/ban2.sh`: a wrong token earns a ban, a good node is untouched, 800 garbage and wrong-key packets earn nothing,
the ban ends, the operator lifts it). Not tried: a long real attack.
