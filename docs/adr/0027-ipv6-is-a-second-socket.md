# 0027 — IPv6 is a second socket, not a dual-stack one

Status: accepted

## Context

A node or switchboard needs to speak IPv6 as well as IPv4: for a switchboard reached over IPv6, for a direct path between
two nodes that have global IPv6 addresses (no NAT, no punching), and so that the line works on networks that give
IPv4 only through carrier NAT. One socket can do both on Linux by accepting IPv4 as IPv4-mapped IPv6 addresses.

## Decision

A node opens an IPv4 UDP socket and, beside it, an IPv6 one (`IPV6_V6ONLY`), on the same port when it can. Each datagram
is sent from the socket of the destination's family and a reply leaves by the socket the request came in on. `--listen` is a
list: `ADDR:PORT` (IPv4), `[ADDR]:PORT` (IPv6), or a bare `PORT` for both on any address.

- A candidate carries its family ([0025](0025-every-candidate-carries-its-own-length.md): 7 bytes for IPv4, 19 for IPv6).
  Global IPv6 addresses of the node's interfaces are host candidates; there is no reflexive or mapped IPv6 candidate, because
  there is no NAT66 to find through. The switchboard adds the address it sees a node at, of either family.
- The two old protocol facts that are IPv4 only stay so: `ECHO`, NAT-PMP and port spray work on IPv4 sockets, and a
  switchboard reached over IPv6 reports no IPv4 address to the node (the address field of `NUMBER` and `HBACK` is zero).
- A node with a global IPv6 address is public as it stands and may offer to relay.
- The switch answers IPv6 neighbour solicitations for hosts across the line, as it does ARP.

## Consequences

- No code treats an address as a 4-byte value without checking its family first; the same code that reads a source
  address works for both.
- Mapped addresses are never seen, so a family check is all that tells the two apart.
- A node with a global IPv6 address is reachable on its UDP port from anywhere: its protection is the handshake, as for a
  public IPv4 address, and a firewall that allows the port is the operator's choice ([THREAT-MODEL](../THREAT-MODEL.md)).
- IPv4 and IPv6 can be chosen by `--listen`: a node that wants one family only names that one.
