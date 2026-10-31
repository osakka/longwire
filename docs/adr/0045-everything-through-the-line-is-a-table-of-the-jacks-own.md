# 0045 — Everything through the line is a table of the jack's own

Status: accepted; built in 0.5.26. Builds on [0037](0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md) and [0044](0044-routes-beyond-the-line-go-through-a-gateway-on-it.md), which left this open.

## Context

"Send everything through the line" means a machine's traffic for the internet leaves by a router at the far end of the line, the way it would if the machine were plugged in there. A tunnel that carries
everything has three hard parts: the tunnel's own packets must not go into the tunnel; the machine's own routes must not be destroyed or bent out of shape; and when the tunnel stops, for any reason,
the machine must not be left with no road at all.

The common trick, two routes `0.0.0.0/1` and `128.0.0.0/1` that beat the default route by being more specific, was rejected: it is a hack in the main table, it fights every other program
that manages routes, and it says nothing about what happens when the node dies.

## Decision

1. **The default route through the line lives in a table of the jack's own** (7700 + N for `lwN`), and two rules put that table in front of the machine's own:
   `7700: lookup main suppress_prefixlength 0` (the machine's own routes to its own networks still win; only its default route is passed over), and `7701: lookup 7700+N`. The main table and the
   machine's default route are never touched. This is how WireGuard's `wg-quick` does it.
2. **Every socket of the engine is tied to the real device** (`SO_BINDTODEVICE`, before it is bound; no privilege needed). For a socket tied to a device, the kernel passes over a route through any
   other device, so the jack's table cannot capture the line's own packets: the switchboard, the peers, the punches, the port spray. One socket per family, as always: a second socket would be a second
   source port, a second NAT mapping, and the punch would miss. `wg-quick` marks its packets with a firewall mark instead, which needs privilege; tying needs none.
3. **The safety net is the kernel's, and it is checked.** A tap that no process holds has no carrier. The helper tells the kernel to skip routes through the jack while it has none
   (`ignore_routes_with_linkdown`), so the moment the node stops, is turned off, or crashes, the jack's table is passed over and the machine is back on its own road. No cleanup has to run for that.
   The helper looks at the carrier before it adds anything, and refuses if it is not down.
4. **IPv6 goes into the line too.** The jack's table has an IPv6 default route through the jack, with the same safety net. Unless the far side routes IPv6, nothing answers it there, and
   programs fall back to IPv4: IPv6 does not leak past the line, and does not work through it either.
5. **One switch, in the profile and the pane**: `everything = yes`, "Send everything through the line", which needs a Gateway (the router on the line that takes the traffic onward) and a static
   Address. The helper adds it in the same password step as the jack; turned off, the next Turn on takes the rules and the table away. `--remove` takes them away with the jack. One jack at a time:
   if something else holds the places in the rules, the helper refuses and says what holds them.
6. **`longwire show` says it plainly**: `everything through the line (lw0); the line itself goes by eth0`, or, if the kernel is not sending the machine's traffic through the line though it was asked, says that.

## What it does not do

- **Fail open, not a kill switch.** When the node stops, traffic goes the machine's own way again, which is what "off" means. A switch that blocks everything while the line is down is a different
  promise, and not made here.
- **Names.** The machine keeps asking whichever name server it was set up with; if that is a router on its own network, those questions stay local. Since 0.5.27 a name server can be asked through the line: [0047](0047-names-through-the-line-are-set-on-the-jack.md).
- **Roaming.** The engine's sockets are tied to the device the machine reached the world by when it started. Since 0.5.27 the node lets go of the jack when that changes, and starts again on the new road: [0046](0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md).
- **The far side.** The gateway must pass the traffic on: forward it and, if it is the edge of its network, translate it to its own address. A machine bridged into a home or office network
  that names that network's own router as the gateway needs nothing more: to the router it is one more machine on its network.

## Consequences

Tested on the real kernel, in a network namespace of its own, with the real helper and a stand-in jack whose carrier comes and goes (`tests/tunnel.sh`): a program's packet goes through
the line, a tied socket goes the real way, the machine's own networks and the line's own network stay direct, the default route is untouched, IPv6 goes into the line, the machine is back on its
own road the moment the carrier goes, the refusals, running twice, and taking it away. The helper's commands and refusals against a stand-in `ip` (`tests/jack.sh`); the engine's tying (`tests/jacknet.sh`);
the script and the pane (`tests/pane.py`). Not tried: a real tap with a real node and real traffic to the internet across a real line.
