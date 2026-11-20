# 0098 — The phone carries IPv6

Status: accepted; built in the release after 0.5.74. (Ticket LW-9; [ADR 0076](0076-the-phone-is-a-member-with-an-adapter-and-an-app.md) left IPv6 out.) A phone on the far LAN has no IPv6 there: the app dropped what the OS sent, and with "send everything through the line" it routed IPv6 to nowhere so that it would not leak.

## Decision

1. **The phone makes an IPv6 address the way a host on a LAN does** (SLAAC): it asks the routers on the line (a router solicitation, three times and then once a minute until it has one) and takes the first prefix of an advertisement that is 64 bits long, says it may be made into an address (the A flag), has a lifetime, and is a network (not multicast, link-local or the unspecified). The address is that prefix and an interface id made from the phone's MAC (EUI-64). Its link-local address is made the same way. No duplicate-address check is made: the MAC is made from the phone's own key.
2. **An advertisement is believed only if** it comes from a link-local address, with hop limit 255, a good ICMPv6 checksum and well-formed options. The node's own guard ([ADR 0080](0080-only-a-member-we-asked-for-may-speak-for-the-network.md)) has already refused one from a member this node did not ask for. The router is the sender, for the lifetime it says; its MAC is taken from the advertisement.
3. **Neighbour discovery:** a solicitation for the phone's address is answered with its MAC; an advertisement is kept (for the time ARP is kept); a packet for a neighbour waits one second at a time, four times, for the answer, as an IPv4 packet waits for ARP. On the link, or in the prefix, a packet goes to its neighbour; anything else goes to the router.
4. **Packets:** from the OS, only those from the phone's own address and not to itself; link-local and wider multicast go to the multicast Ethernet address. To the OS, only unicast to its address from another host, with a consistent length; nothing multicast is delivered, as with IPv4.
5. **The app** reports the address and the router (`ip6`, `prefix6`, `gateway6`) in the member status; `gen` moves when the address does, so the app builds its tunnel again with it. The tunnel has the address and a route for the prefix, or for all of IPv6 with "send everything through the line". On a far LAN with no IPv6 the sink of ADR 0076 is kept: IPv6 is sent nowhere, not past the line.
6. **The prefix's lifetime is kept:** an address whose prefix ran out unrenewed is dropped and asked for again.

## Consequences

- A phone on a LAN with IPv6 has it through the line, and "send everything through the line" no longer loses it.
- Not done: DHCPv6 and stateful addressing (the M flag); router advertisements' DNS option (the phone's DNS is IPv4's); privacy addresses; a duplicate-address check; reachability probing of a neighbour that has gone (an entry just ages).
- Unit and network tested with a stand-in LAN that is an IPv6 router (`tests/leafnet.sh`): the address, the answers, and what must not change it (a bad checksum, a hop limit under 255, a source that is not link-local, no A flag, a prefix that is not 64). Not tried on a phone or against a real router.
