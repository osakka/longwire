# 0016 — Handshake cookies are checked on every fragment

Status: accepted

## Context

Every handshake costs a KEM decapsulation, so a flood of handshakes is a cheap way to burn a node's CPU, and a public
switchboard is exposed to exactly that. The first defence was a budget that shed excess handshakes, but that also let a
flooder starve real ones. WireGuard's answer is a cookie bound to the source address; but its handshake messages fit one
datagram, and ours are two fragments, with the message-level `mac1` in the *last* one.

## Decision

Past half of the handshake budget (50 a second on a node, 2000 on a switchboard) a node is *under load* and answers an
`HS_INIT` fragment that lacks a valid cookie MAC with a 59-byte `COOKIE` reply, doing no work and buffering nothing. Every
handshake fragment carries two 16-byte tags: one keyed by the receiver's key id (proof you know whom you are talking to)
and one keyed by the cookie, zero when none is held. The initiator retries at once with the cookie, accepts at most one
a second, and a holder of a cookie skips the global budget but is limited per source address (burst 8, 2 a second). See
[PROTOCOL §3](../PROTOCOL.md#3-sessions-and-timers).

## Consequences

Handshake fragments grow by 16 bytes, to at most 1248, still under the 1280-byte IPv6 minimum. A flood from addresses that
cannot receive replies costs a small reply per packet and no public-key work, and a legitimate handshake still completes
during it (the test sustains a flood while a peer rekeys, and requires at least two rekeys and no lapse). An attacker with a real address can fetch a cookie
and is then held to 2 handshakes a second; someone who can read the path can replay a cookie from the same address.

---

[← decisions](README.md)
