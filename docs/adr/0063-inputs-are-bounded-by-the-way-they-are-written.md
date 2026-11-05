# 0063 — Inputs are bounded by the way the code is written

Status: accepted; built in 0.5.49. Found by an independent read of every input boundary (0.5.48).

## Context

An independent reviewer read the engine, the switchboard, the control socket and the root helpers without being shown what was believed safe, and found where the code relied on a check
being remembered instead of on a shape that cannot go wrong. Each of these is a class, and each is closed by changing the shape:

1. **JSON written past a buffer.** `snprintf` returns what it *would* have written, and an offset or a size made from that is how a buffer is overrun. The status of a node with 13 or more
   peers (a line of 16 reaches it, and any member can add members) wrote past the control socket's 8 KB buffer.
2. **A list of bad names.** The helper's DHCP hook removed named variables from the environment of the vendor's script. A DHCP server on the line could send option 121 (classless routes),
   which the hook did not name, and the vendor's exit hook installed a default route: a member could take over the machine's traffic under the plain device permission.
3. **A frame type nothing filters on the way in.** The site boxes' own EtherType (0x88B5) is dropped on the way out of a site but not on the way in from a member, who holds the line key and
   can make a valid probe; one frame puts the site's other box in standby.
4. **A local address anyone may claim.** A peer's frame saying it came from a MAC learned on our own LAN moved that host's traffic out over the line.
5. **A time that is the signer's word.** An eviction dated five years ahead bars a key for five years on every node that stores it.
6. **Text that is shown as it arrives.** A switchboard's refusal reached the terminal with its control characters.
7. **The launcher as root.** `sudo longwire` ran the script found in the user's own folder, as root.
8. **Names that are the same thing twice**: `lw07` and `lw7` share a table and its rules; the site helper's card check accepted `.` and `..`.
9. **Forwarding turned on for the way out** let whoever is on its far side route through the exit to the machine's other networks.

## Decision

1. **Every writer of JSON into a buffer is bounded**: `lw_app` appends and returns the new length, never more than the buffer holds; `LW_SN` is `snprintf` with the same promise; the control
   socket never writes more than its buffer. The worst status (32 peers, 32 calls, a line of 16) is under 18 KB and the buffer is 32 KB; a buffer too small gets a cut answer, never a write past it.
2. **The vendor's DHCP script runs from an allowlist environment** (`env -i`, then only `reason`, `interface`, and the address and network route of the lease, each only as an address);
   dhcpcd is told not to ask for or believe the routers, classless routes, name servers, host name or MTU. No list of what to remove.
3. **Site probes from the line are dropped** in `sw_from_peer`.
4. **A locally learned address is held for 10 s against a peer's frame that claims it**; a host that really moved stops speaking and the entry moves, and one speaking here takes it back at once.
5. **An eviction dated more than a day ahead is refused.**
6. **A switchboard's refusal is printable ASCII and nothing else.**
7. **As root, the launcher uses only the machine's copy.**
8. **A jack is `lw0` to `lw99` with no leading zero**, everywhere the name is checked; **a card name starts with a letter or digit**; **forwarding for the way out is limited, when it was off before, to the
   line** (an nftables rule in the jack's own table, undone with it).

## Consequences

What is not done here (the member records and HELLO were done in 0.5.50, [ADR 0064](0064-a-members-record-is-a-claim-and-the-way-out-is-never-the-lowest-number.md)); the round's list was: line member records are claims from whichever member sent them (their numbers, and the signing key they carry, are not tied to the key id, so a
forged low number wins the exit's choice and a forged key can lock out a later member); admits never expire and an operator's revocations are a bounded list; a handshake's time is not checked against
the clock; HELLO answers 125 times what it is asked. These are written in [SECURITY](../SECURITY.md#what-has-not-been-done-and-what-that-means-for-you). Tested: `tests/status.sh` (the worst status under TCC's bounds
checker, and the same test fails on the 0.5.48 engine), `tests/switch.sh` (site frames, address theft, eviction times, refusal text), `tests/jack.sh` (the hook with option 121 and others, the jack
names), `tests/exitnet.sh` (a neighbour upstream cannot route through the exit; it can when the rule is taken out), `tests/pkg.sh` (the launcher as root).
