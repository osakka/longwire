# 0044 — Routes beyond the line go through a gateway on it

Status: accepted; built in 0.5.25. Builds on [0036](0036-the-jack-gets-its-address-from-the-profile.md) and [0037](0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md). A route for everything (a "full tunnel") is not offered yet *(planned: M4)*.

## Context

longwire carries Ethernet frames: what it makes is one segment. A machine with a jack and an address on that segment reaches every host on it with no route at all. Networks *behind* the segment (a site's other subnets, behind the router on the line) are an ordinary IP matter: a route through a
gateway that is on the segment. Until now that route was typed by hand as root after every restart, the gap the desktop is meant not to have.

Routing a network into the tunnel has a trap, the one [0037](0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md) solved for the jack's own network: if the switchboard or a peer's real address falls inside a network routed into the jack, the packets that keep the tunnel up go into it.

## Decision

1. **A profile may name a gateway and networks**: `gateway = 192.168.152.254`, `routes = 192.168.150.0/24,10.20.0.0/16` (the pane's **Gateway** and **Routes** rows). The jack helper ([0034](0034-a-connection-is-a-profile.md)), in the same password step that makes the jack and puts its address on, adds `ip route replace NET via GATEWAY dev TAP` for each. `on` and the pane ask without privilege whether the routes are there; only when one is missing do they run the helper.
2. **The helper checks before it changes anything.** The gateway must be on the jack's own network (a machine the line reaches directly), not the machine's own address, not the network's first or last address. Each route is a real IPv4 network: a prefix of 8 to 32, no host bits set, not loopback, not link-local, not multicast or above, not overlapping the jack's own network (reached already). At most 16. Routes need a static address: with DHCP the gateway's network is not known in advance.
3. **The default route, and anything wider than a /8, is not offered.** See below.
4. **The engine keeps its own traffic off every route through the jack.** The networks the jack is "in", for [0037](0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md)'s underlay socket, now include every route the kernel sends through the jack (read from `/proc/net/route`, which anyone may read; refreshed once a second). A switchboard or a peer whose address falls inside a routed network is reached through the socket tied to the real device. The `ECHO` probe and the port-spray probes go the same way.
5. **Routes vanish with the jack**, so `keep_jack = no` (or `longwire jack --remove`) takes them away too.

## Why not the default route, yet

A route for everything through the line ("full tunnel": all of the machine's traffic leaves by the far site) is a different promise. With item 4 the engine already keeps the switchboard and the peers on the real road whatever is routed into the jack, which is the hard part. What is missing, and why it is not offered:

- **It must not replace the machine's own default route.** The underlay socket needs a real default route to tie itself to. The way to do it is the two halves, `0.0.0.0/1` and `128.0.0.0/1`, through the gateway, which win over the default by being more specific and leave it in place.
- **It must fail safe.** A jack outlives the node. If the node stops or crashes with half-of-everything routed into a jack that nobody holds, the machine has no network until someone removes the jack. The kernel can skip routes through a device that has no carrier (`ignore_routes_with_linkdown` on the jack); a tap with no process attached has none. That has to be set and checked before the two routes go on, and it has to be tried on a real tap, which has not been done.
- **The far side must agree.** The gateway has to forward and translate the machine's traffic to the internet; name servers are a separate choice ([0038](0038-the-jack-may-ask-a-dhcp-server.md) refuses the line's on purpose).

## Consequences

The common case, "reach the site's other networks through its router", is a desktop setting. Tested: the helper against a stand-in `ip` (the routes it adds, the order, and its refusals: `tests/jack.sh`), the engine's reading of routes through the jack (`tests/jacknet.sh`), the script and the pane against stand-ins. Not tried: routes on a real tap, and traffic across them.
