# 0090 — A member may not claim an address our LAN holds

Status: accepted; built in 0.5.71. [ADR 0083](0083-a-pair-of-boxes-is-one-site-and-a-bridge-carries-no-control-frames.md) refuses a member's frame whose *source MAC* is a host speaking on our LAN. A member could still say, in ARP or in neighbour discovery, that an address a local host holds is its own, from a MAC of its own, and the hosts of the LAN would then send that address's traffic to it. (Ticket LW-6.)

## Decision

1. **The LAN's claims are noted.** An ARP message's sender address (not 0.0.0.0, the probe), an IPv6 neighbour solicitation's source address and an advertisement's target (not the unspecified or a multicast address), seen coming from our own LAN, are kept for two minutes, the time an ARP cache lasts. A frame that is a copy of what a member has just put on the LAN is not from the LAN ([ADR 0083](0083-a-pair-of-boxes-is-one-site-and-a-bridge-carries-no-control-frames.md)) and is not noted.
2. **A member's claim of the same address is refused,** whatever the frame's source MAC, with a count (`drop_claim`) and an event, `address-claim` (the member's number, ARP or neighbour discovery, the address), said at most once a minute per member. It is refused before anything is learned from it. A priority tag, two, and the IPv6 extension headers are looked through, as the DHCP guard does.
3. **It is the switch's own, always on:** no option; a member has no business with an address the LAN's own host holds.

## Consequences

- A member cannot take a local host's address in the hosts' ARP and neighbour caches by saying so, as long as that host has spoken on the LAN in the last two minutes.
- **Not covered:** a host that has been silent for two minutes (its address is then nobody's claim: the first to speak holds it, as on any LAN, and the host's own next message will contest it); data frames that merely carry a source address (the guard is about what the network says of itself, not about every packet); a fragment chain that runs off the frame (it says nothing readable).
- Two hosts that really do share an address across sites are a configuration error that this now makes visible (`address-claim`) instead of silent.
- Unit tested under the bounds checker (`tests/claim.sh`); not tried on a real LAN with a real attacker.
