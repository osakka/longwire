# 0025 — Every candidate carries its own length

Status: accepted

## Context

A candidate was seven fixed bytes: an IPv4 address, a port and a type. Every list of them (`DIAL`, `ANSWER`, `RING`,
`GO`, `HEARTBEAT`, `NUMBER`) was a count followed by that many seven-byte entries. A reader that met an eight-byte
entry, such as an IPv6 address, would misread the rest of the list. Once a public switchboard is running, changing
this means a version split. Today nothing public exists, so it is free.

## Decision

An entry is `len:1 ‖ type:1 ‖ port:2 (big-endian) ‖ address:len-3`. IPv4 entries have length 7 (eight bytes in
all); the types stay 1 host, 2 reflexive, 3 mapped. A reader skips any entry whose length or type it does not know
and keeps the ones after it. Buffers are sized for the largest entry (a 16-byte address), `CANDWIRE`.

IPv6 itself is not part of this decision, and it needed nothing more on the wire: the family is implied by the entry's
length ([0027](0027-ipv6-is-a-second-socket.md)).

## Consequences

- A list costs one byte more per candidate (at most eight bytes more in a message).
- A node with IPv6 can talk to one without it: the entries that mean nothing are dropped, not mistaken.
