# 0102 — One DHCP client for the phone and the desktop

Status: accepted; built in 0.5.77. It supersedes the DHCP-client parts of [ADR 0063](0063-inputs-are-bounded-by-the-way-they-are-written.md) (the hook that cleaned a client's environment) and [ADR 0088](0088-the-jack-tells-the-dhcp-server-the-machines-name.md) (the per-client flags).

## What was wrong

A desktop's jack asked for an address by starting whichever of `dhclient`, `dhcpcd` or `udhcpc` the machine had, each with its own flags, its own retry rule, its own way of reading a server's answer, and a hook to stop it taking a default route or name servers. Three parallel paths for one job: a fix to one (a retry, a host name) was not a fix to the others, the owner's machine ran the one that was not fixed, and the phone had a fourth client of its own in the engine.

## Decision

1. **One client.** The DHCP client in `engine/member.c`, which the phone's adapter runs, runs on a jack too, in a program of its own, `lw-dhcp` (`packaging/lw-dhcp.c`, installed as `/usr/lib/longwire/lw-dhcp`). Its rules are written once: asks every few seconds (4, 8, 16, 32, 64) until a server answers and never gives up, renews at half the lease, drops the address when the lease runs out, sends the machine's host name, and refuses a lease that is not a LAN's (10/8, 172.16/12, 192.168/16, 100.64/10, a mask of 8 to 30 bits: `lease_sane`, which the phone uses too).
2. **Two processes.** The one that reads what the network sends is nobody: it opens its sockets as root, gives up every privilege and is closed to a short list of system calls (the engine's sandbox, [ADR 0062](0062-the-node-can-make-only-the-system-calls-it-needs.md)). The one that keeps the privilege reads one fixed message from it, checks it again with `lease_sane`, and puts the address on the jack with rtnetlink: no default route, no name servers, no routes but the one the kernel makes for the address, and the address off again when the lease is lost or the client stops.
3. **The three branches and the hook are gone** from the jack's helper, and the three clients are no longer suggested by the packages.
4. **What it says:** `/run/longwire/dhcp-lw0.state` holds `searching` or `bound ADDRESS`.

## Consequences

- The same behaviour on every distribution and every machine, with or without a DHCP client installed; the retry, the host name and the lease rules are in one place and tested there (`tests/dhcpnet.sh` on a real interface, `tests/leaf.sh` for the lease rules, `tests/jack.sh` for the helper).
- A fix to the phone's client is a fix to the desktop's.
- No IPv4 link-local address is made when nobody answers: the jack has no address until one does.
- DHCPv6 and DNS from the lease are left out (as before, the lease takes no name servers) *(planned: M6)*; a default route from the lease is ticket LW-4.
- The dropping of privilege and the filter on a real root start has not been tried on a real root start (the tests run the client inside a user namespace, where nobody has no number, with `--no-drop`; the filter is in force there), nor against a real router's DHCP server.
