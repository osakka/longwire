# 0002 — A software Ethernet switch whose ports are encrypted sessions

Status: accepted

## Context

The goal is a network that spans sites: the same subnet in several countries, so that ARP, DHCP, mDNS and everything
else on a LAN works unchanged. That is layer 2. A routed overlay would need an address plan, route tables and a second
subnet per site.

## Decision

Every node is a port of one distributed learning switch. A frame is learned by its source address, forwarded to the one
place its destination lives, and flooded when the destination is unknown or a group address. A frame that arrived from a
peer is never sent on to another peer (split horizon), because sites form a full mesh and every peer has already
received it directly. That rule is what removes the need for spanning tree. There is no routing protocol, no address
plan and no controller.

## Consequences

"A client is a server" becomes literally true: there are no clients and servers, only ports. The cost is that a LAN's
chatter crosses the WAN, which the switch trims with a flood budget and ARP suppression
([0017](0017-a-splice-box-makes-a-nic-fit-the-line.md)). Loops cannot form between sites; the one way to make one is two
boxes on the same segment, which [0018](0018-boxes-on-one-segment-elect-one-to-forward.md) handles. Lines of more than two
nodes use the same switch, with introductions and admission ([0022](0022-lines-form-by-vouching.md)); but because split horizon needs a
full mesh, a pair that cannot connect splits the line until relaying exists *(planned: M3)*.

---

[← decisions](README.md)
