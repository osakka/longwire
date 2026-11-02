# 0051 — longwire's datagrams may be split on the way

Status: accepted; built in 0.5.33. See [ARCHITECTURE §6](../ARCHITECTURE.md#6-packet-sizes).

## Context

A line carries a frame of up to 1428 bytes in one datagram: 1485 bytes on the wire, 1489 once a relay wraps it, sized for an IPv4 path of 1492 or better. Linux sends a UDP datagram with
"don't fragment" (DF) set by default. A path narrower than that, behind a router that drops a DF datagram without the ICMP "fragmentation needed" that would tell the sender (a path-MTU black
hole), loses every large datagram in silence: a call connects, small traffic flows, and bulk transfers over the line stall. Such a path was found on the owner's own network (a home router's
upstream of about 1400 bytes, with no ICMP), where HTTPS stalled the same way.

## Decision

1. **Every IPv4 socket of the engine sends without DF** (`IP_MTU_DISCOVER` = `IP_PMTUDISC_DONT`): the node's own, the spray sockets, the underlay socket and a switchboard's echo port. A
   router on a narrower path splits the datagram; the far end's kernel puts it together before longwire sees it. Nothing on the wire of the line changes.
2. **The line's own sizes stay as they are.** Fragments cost a little and can be lost or refused by some middleboxes; probing the real path MTU per peer and sizing to it *(planned: M7)* is
   the better answer, and this is the safety net until then.
3. **IPv6 is unchanged**: routers never split an IPv6 packet, so a narrower IPv6 path still needs the sender to know it.

## Consequences

A narrow path with a silent router carries the line, slower by the fragments, instead of stalling it. Tested: the option on the node's socket (`tests/jacknet.sh`). Not tried: a real path
with a black hole (the build machine has no packet filter to make one).
