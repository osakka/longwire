# 0038 — The jack may ask a DHCP server

Status: accepted. Builds on [0036](0036-the-jack-gets-its-address-from-the-profile.md).

## Context

A static address per machine ([0036](0036-the-jack-gets-its-address-from-the-profile.md)) is right for a few machines. A site that bridges a LAN with a DHCP server already has one,
and a person joining it with a jack would otherwise run `dhclient lw0` in a terminal after every restart. A lease needs root (the address is set on the device), and the node
is not root. The tunnel need not be up when the jack is made, so a client that asked once would fail.

## Decision

1. **`address = dhcp`** in a profile (the pane's **Address** row, `longwire jack --address dhcp`). The root helper that makes the jack ([0034](0034-a-connection-is-a-profile.md)) also starts a
   DHCP client on it, in the same password step: `dhclient`, else `dhcpcd`, else `udhcpc`, whichever is installed; with none it says which to install and exits.
2. **The client keeps asking** until the line answers, so the tunnel can come up afterwards. A client of ours from before is stopped first, so there is one (its pid is kept in `/run/longwire/dhcp-TAP.pid`).
3. **It takes the address and the network route only.** The system's client script runs behind a hook that removes the default route, the name servers, the search domains, the time servers and static routes
   (for dhcpcd: `-G -C resolv.conf -C ntp.conf`). A line is somebody else's network: it must not take over where this machine's traffic goes.
4. **No second password.** `longwire on` and the pane ask without privilege whether a client of ours is running on the device or it already has an address; only when not do they run the helper.

## Consequences

Tested without privilege against stand-in clients (`tests/jack.sh`): the arguments each is started with, that the hook strips what it must and keeps the address, and that an old client is stopped.
Not tried: a real lease from a real server. A static route a line's server pushes is dropped on purpose; a site that needs one adds it by hand.
