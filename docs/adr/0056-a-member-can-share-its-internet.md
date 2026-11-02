# 0056 — A member can share its internet; the gateway is a member's key, not an address

Status: accepted; built in 0.5.39. Amends [0045](0045-everything-through-the-line-is-a-table-of-the-jacks-own.md) (everything through the line no longer needs a gateway typed in).

## Context

Sending everything through the line (0045) needed a gateway: the address of a router on the line that takes the traffic onward. Asking a person for an address on a network they may
not run is the wrong question, and the far machine had to be made to forward and masquerade by hand. Most lines have one obvious way out: a server, like vpn-1, whose internet the others
want to use. The owner asked for it to be automatic, and simple: no DHCP server on the exit, nothing set up on the far side by hand.

## Decision

1. **An exit says so.** A node started with `--exit` (profile key `exit = yes`, the pane's *Share this machine's internet*, `longwire exit on`) tells every member it has a session with,
   inside that session, every 10 s: control message `M_EXIT` (0x0c) carrying the MAC of its jack. A member is an exit to another while it said so in the last 35 s on a session that is up,
   and only a current member of the receiver's line is ever believed. A spliced site can name another machine on its LAN as the way out with `--exit-mac MAC`.
2. **The gateway is not a machine.** A node that sends everything through the line with no gateway named (`everything = yes` and no `gateway`; `--use-exit`) has its jack's table point
   the default route at 169.254.77.1, with the MAC 02:6c:77:65:78:74 fixed beside it by the jack helper (`ip neigh ... nud permanent`): no ARP is ever asked or believed. The switch hands
   every frame for that MAC to the chosen exit's session, with the exit's own MAC put in, and to nobody else; with no exit it drops the frame and counts it (never floods it). So the
   gateway is a member's key: another member cannot become it by claiming an address, and it works through a relay as any member's traffic does.
3. **Which exit.** The one named (`exit_through = NUMBER`, `--exit-through`), if it offers; else the only one; else the lowest number. The node says which in an `exit` event (`number`,
   `wanted`, `offers`) whenever that changes, and in the status (`exit`: `share`, `use`, `wanted`, `using`, `offers`, `sent`, `dropped`; each peer's `exit`). No exit: nothing goes out
   through the line, and every surface says so; turning longwire off puts the machine back on its own road, as before.
4. **The exit's side is its kernel's**, set up by the jack helper with `--share` (one password): forwarding on for the jack and for the device the machine's own default route leaves by,
   and an nftables table of the jack's own (`longwire-lwN`) that masquerades the line's network out through that device, lets only answers back, and lets nothing from the line reach
   the machine's other networks. What was changed is written in `/run/longwire/share-lwN` and undone exactly when the helper runs without `--share` or removes the jack. It needs a fixed
   address on the jack (the line's network is what is let out) and `nft`. A node does not share and use an exit at once.
5. **No DHCP anywhere in this.** Every member already needs an address on the line to use it; the exit uses the one it has. The client side works with a fixed address or a DHCP lease.

Also: the pickup prompt says what picking up means (the caller joins your network, stays a member and reconnects by itself until evicted), on the desktop and in the terminal.

## Consequences

The exit sees everything its users send outside the line, as their ISP would, and its address is what the internet sees: share only from a machine the line trusts that much. While the
node runs with no exit, traffic for the internet stops (it does not leak to the local network); when it stops, the machine is on its own road again (0045's fail-open is unchanged).
An exit that uses Docker or another firewall whose forward policy drops may still drop the line's traffic: its own rules come first. IPv6 is still not carried out (0045). Tested: in the
engine, real nodes on sock ports (`tests/exit.sh`: the frames reach only the chosen exit with its MAC; two exits, the lowest or the one asked for; the exit going; refusals); on the real
kernel in namespaces (`tests/exitnet.sh`: the client's table and fixed neighbour, the packet leaving into the jack addressed to the gateway MAC, fail-open; the exit's forwarding and
masquerade, the line kept off the exit's LAN, nothing let in, undone exactly; it needs `nft` and is skipped without it); the helper's refusals (`tests/jack.sh`); the pane (`tests/pane.sh`).
Not tried: a real tap under a real node end to end, a real exit on the internet.
