# 0080 — Only a member we asked for may speak for the network

Status: accepted; built in 0.5.65. The owner asked what stops a machine on a LAN posing as a DHCP server (Cisco's DHCP snooping, and the rest), and for longwire to do it properly.

## Context

A line is a LAN. A member of it can answer a machine's DHCP request and be its router and its name server, and the switch's own protections (a member cannot take a local host's address, [ADR 0063](0063-inputs-are-bounded-by-the-way-they-are-written.md))
do not stop a *server*. The engine warned of two servers (`dhcp-conflict`) and changed nothing. A managed switch's answer is DHCP snooping: a port is trusted or it is not, and a server's reply from an untrusted port is dropped.
Here every frame from the line arrives on an authenticated session, so the thing to trust is not a port but **a member's key**.

## Decision

1. **The guard** (`engine/sw.c`, `guard_ok`): a frame from a member that is a **DHCPv4 reply** (UDP 67 to 68), a **DHCPv6 reply** (547 to 546) or a **router advertisement or redirect** (ICMPv6 134, 137) is refused unless that
   member **may speak for the network**: the node asked for it (a number a person dialled, remembered, so a call the other end places later keeps the trust; or a configured peer that has an address), or the configuration names it
   (`--dhcp-server KEY`, up to eight). The refusal comes **before anything is learned** from the frame, is counted (`dhcp_guarded` in status) and said (the `dhcp-blocked` event, at most once a minute per member, with the
   command that allows it).
2. **It looks through what gets past a switch that reads one header**: priority tags (one and two), IPv4 and IPv6 extension headers (hop-by-hop, routing, destination options, authentication, fragment), and a chain that runs off the frame, which is
   **refused rather than guessed at**. A fragment that is not the first has no header to read and goes on (without the first it cannot be put together, and the first is refused).
3. **One check for every kind of port**: on for a **splice** (the site's LAN), a **jack** (a machine's own network card) and a **phone** (its adapter), off for a socket port, which stands in for a LAN in the tests (`--dhcp-guard on` makes it
   on there, so the tests can try it). `--dhcp-guard on|off|auto`. The default is by the port, so there is **no moment after a restart when it is open**; nothing is learned and kept.
4. **What it protects and what it does not.** A splice node's LAN is no longer open to a member that rang it. A jack or a phone takes a lease only from a member it dialled or one it names. A member that *is* trusted can still lie in what it
   says, and a member can still spoof ARP and scan: the line is a LAN of people one admitted ([SECURITY](../SECURITY.md)).

## Not done

- **No ARP or neighbour inspection** beyond what [ADR 0063](0063-inputs-are-bounded-by-the-way-they-are-written.md) does. The engine knows which MAC is on which port, so a member's claim to an address a local host holds could be dropped; that is a further step.
- **Not persisted**: the dialled numbers are remembered for the life of the node. A node that restarts and is rung by its server before it dials again refuses the server's replies and says so; dialling it again, or `--dhcp-server`, lets it.
- **Profiles and the pane** do not set `--dhcp-guard` or `--dhcp-server` yet; the command line does.

## Tests

`tests/guard.sh` (bounds-checked): a reply refused from an unasked member, nothing learned, counted; a request, other UDP and a non-first fragment pass; a priority tag, two tags; trust by asking, by a dialled number, by a named key, and not once
forgotten; DHCPv6, router advertisements, redirects, behind hop-by-hop, destination-option and routing headers; a chain that runs off the frame; ordinary IPv6 passes; the default by port (socket off, splice, jack and phone on) and `off`. The test fails
17 ways with the guard disabled. `tests/guardnet.sh`: a real line, a member the node dialled and a rogue that rang it: the first's reply reaches the LAN, the second's does not, its other frames still do, and the count is right.
