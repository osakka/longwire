# 0108 — Everything through the line goes out by the lease's router

Status: accepted; built in 0.5.80. The owner's desktop sends everything through the line, with an address from the site's DHCP server and no gateway named. Its way out was the member that shares its internet (ADR 0056); the site's boxes share none (they splice a LAN that has its own router), so the desktop had "Way out: none" and reached nothing beyond the LAN.

## Decision

- With `--everything` and `--dhcp` and no `--via`, the jack helper starts `lw-dhcp` with `--table N` (the jack's own table). When a lease names a **router on its own network**, `lw-dhcp` points that table's default route at it: the site's router is the way out, as for any machine on that LAN. Its state file says `bound ADDR via ROUTER`.
- When the lease is lost, or the client stops, the table goes back to the member that shares (169.254.77.1, ADR 0056), after the address is removed (the kernel takes every IPv4 route of a device off with its last address).
- A lease whose router is not on its own network is refused, as before (`lease_sane`). The machine's own table and default route are never touched.

## Consequences

- A desktop on a spliced site LAN reaches the internet the way the site does, with no flag and no member set up to share.
- Name servers from the lease are not applied (unchanged).
- Tested (`tests/dhcpnet.sh`, also as real root): the table follows the router, the state says so, the main table is untouched, the table goes back on stop, an off-network router is refused.
