# 0037 — Traffic to the other end never goes into our own jack

Status: accepted. Builds on [0036](0036-the-jack-gets-its-address-from-the-profile.md); found by the first real site node (a container that
splices its only network card) and a desktop whose jack was given an address in that same network.

## Context

A node connects to another by trying the addresses the other end offers (its *candidates*): the addresses of its interfaces, the address the
switchboard saw, a router mapping. A site node that splices a card offers that card's address, which is an address in the bridged network.

A desktop on a single machine has a jack, and the jack can be given an address in that very network ([0036](0036-the-jack-gets-its-address-from-the-profile.md)),
because that is how it takes part in the bridged network. From then on the desktop's routing table sends every packet for that network
into the jack. The packets that are meant to bring the tunnel up, addressed to the site node's card, go into the tunnel, which is not up yet,
and nowhere else. The call stops being able to connect at the moment the address is set, and the log on the site node shows the other end
joining the line over and over without ever connecting. (`ip route get 192.168.152.2` on the desktop said `dev lw0`.)

The cure other tunnels use is a host route to the peer's endpoint through the real interface, or a socket tied to the real interface
(WireGuard's `wg-quick` uses a firewall mark and a policy rule). Routes and rules need root, which the node does not have and must not ask for
([0004](0004-a-node-never-runs-as-root.md)); a socket tied to a device does not, on Linux 5.7 and later.

## Decision

1. **A second UDP socket, the underlay socket, tied to the real network device** (`SO_BINDTODEVICE`; the device of the default route that is not
   the jack, or the one named with `--underlay DEV`). It is opened the first time it is wanted. Everything a node sends to an address that lies
   inside a network connected to its own jack leaves through it: the punch packets, the replies to them, the handshake, the data. The
   kernel then picks the real road for it, whatever the jack's connected route says. It is polled like the node's other sockets, and the peer
   learns the source it sees, as it does for any path.
2. **The networks of the jack are read from the system**, with their netmasks (IPv4 and IPv6; link-local is no network), and refreshed once a
   second, so an address set by hand, by [0036](0036-the-jack-gets-its-address-from-the-profile.md) or by a DHCP client while the node runs is followed.
3. **If no underlay socket can be had** (an older kernel that lets only root tie a socket to a device, no default route, a device that is not there), an address
   of the other end inside the jack's network is left out of what is tried, counted (`cand_skipped` in `status`), and said now and then (the event
   `address-skipped`, which `longwire watch` narrates and `longwire show` summarises). The loop is then gone, but so is that road.
4. **A node does not offer an address that is on its own jack**: it exists only on the far side of the tunnel. IPv6 addresses inside the jack's network
   are left out as in 3 (the underlay socket is IPv4).
5. Nothing changes for splice, `sock` or `none`, or for a jack with no address.

## Consequences

The first call to a site node in the network you have just joined works without a host route. Not guaranteed everywhere:

- **Reverse-path filtering.** The reply arrives on the real device from an address (the site node's) that the machine believes is behind the jack. A machine
  with `rp_filter=1` (strict) drops it; `0` (off) and `2` (loose, the usual default of the distributions that set one) accept it. If a call still never connects, check
  `sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.default.rp_filter` and the one for the real device.
- **Linux before 5.7** lets only root bind a socket to a device; there the address is dropped (3) and the call needs another road to the site.
- A second default route on a machine (a VPN, a mobile card) means the "real device" is the first one found; `--underlay` names it.

Tested without privilege with the loopback interface standing in for the jack and the device (`tests/jacknet.sh`): which addresses are kept or dropped,
that a packet for an address in the jack's network leaves through the underlay socket and one outside it does not, and that a peer's traffic is chosen the same way.
Not tried: a real tap and a real routing table with the loop in it, which only the owner's desktop and the site node can show.
