# 0036 — The jack gets its address from the profile

Status: accepted. Builds on [0034](0034-a-connection-is-a-profile.md); resolves the "an address for the jack from the pane" item of the roadmap
for the static case.

## Context

A node on a single machine attaches to a tap device, the jack. longwire carries Ethernet frames and hands out no IP addresses, so after
"connected" a `ping` still failed until somebody ran `sudo ip addr add …` on each end, and again after every restart, because the device does
not survive one. That is a terminal step in the middle of an otherwise desktop-first product ([the memory of the user's wish](0034-a-connection-is-a-profile.md):
nobody should make devices by hand).

## Decision

1. **An `address` key in the profile**, a host address with a prefix (`10.77.0.2/24`); empty or absent means "leave the device alone", as before.
   It is only for a jack. Every machine on a line needs its own, in one subnet; longwire does not pick them (two machines picking the same one is
   worse than being asked).
2. **The root helper that already makes the jack puts the address on it**: `longwire-jack [--address A.B.C.D/N] TAP [UID]`. It checks the
   address before it runs anything (four numbers up to 255, a prefix of 8 to 30, not `0.`, `127.` or above `223.`, no other characters), and the
   device is still only `lw0` to `lw99`. The polkit action and the one password stay as they are.
3. **A password only when needed.** `longwire jack --address …`, `longwire on` and the pane's Turn on ask without privilege whether the device
   already has the address (`ip -4 addr show`); only when it does not do they run the helper. After a restart that is once, as for the device.
4. **The pane has an Address row** (jack profiles only), and `profile set NAME address=…` validates the same way.
5. DHCP is not part of this: a line with a DHCP server on it can still be asked with `dhclient lw0`, and the pane does not do it
   *(planned: M4)*.

## Consequences

The helper does a little more as root: `ip addr replace` of a validated address on a device it owns. That is the whole of the new power. It is
tested without privilege against a stand-in `ip` (`tests/jack.sh`), including the refusals. Not tried: a real device on a real machine.
