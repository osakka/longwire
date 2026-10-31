# 0047 — Names through the line are set on the jack

Status: accepted; built in 0.5.27. Follows [0045](0045-everything-through-the-line-is-a-table-of-the-jacks-own.md); [0038](0038-the-jack-may-ask-a-dhcp-server.md) refuses the line's name servers from DHCP, and still does.

## Context

With everything through the line, the machine still asks its usual name server. That is usually a router on its own network, which the rules of [0045](0045-everything-through-the-line-is-a-table-of-the-jacks-own.md)
leave on its own network on purpose (the printer stays reachable): so every name the machine looks up is seen by the network it is sitting on. For "as if plugged in at the far end", names must go there too.

Rewriting `/etc/resolv.conf` is the old way, and it fails badly: it fights NetworkManager, and if the node crashes the file points at a server that can no longer be reached, so the machine has no names.

## Decision

1. **A profile with everything through the line may name a name server** (`dns = 192.168.152.254`, the pane's **Name server** row, shown when the switch is on). The jack helper sets it, in the same password step,
   **on the jack, in systemd-resolved**: `resolvectl dns lw0 ADDR`, `resolvectl domain lw0 '~.'` (every name), `resolvectl default-route lw0 yes`. Nothing in `/etc` is written.
2. **It fails the way the routes fail.** systemd-resolved passes over a device that has no carrier; the jack has none when no node holds it. So when the node stops or crashes, names come back to the machine's own
   name server together with its traffic. Taking the jack away (`--remove`) takes the setting with it; without `--dns` the helper takes it off the jack (`resolvectl revert`).
3. **Only with everything through the line.** Names through the line while the traffic for the answers does not go there is no promise worth making.
4. **Without systemd-resolved**, the helper says plainly that names stay with the machine's own name server, and everything else goes ahead; `longwire show` says the same in its `names` line.
5. Names the machine's own network claims as its own (a search domain the local router hands out) may still be asked there: systemd-resolved sends a name to the device whose domain matches it best. That keeps local names working.

## Consequences

Tested against a stand-in `resolvectl` (what is set, in what order, that it is taken away, the refusals: `tests/jack.sh`) and in the pane (`tests/pane.py`). Not tried: a real systemd-resolved (the build machine has none), or a name looked up through a real line.
