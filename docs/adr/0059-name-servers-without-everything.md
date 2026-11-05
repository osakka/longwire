# 0059 — Name servers through the line without everything

Status: accepted; built in 0.5.46. Amends [0047](0047-names-through-the-line-are-set-on-the-jack.md) (names through the line went only with everything).

## Context

Name servers in a profile were set on the jack only while everything went through the line. A person who routes a few networks through the line, where their own
name servers live (`routes = 192.168.15.0/24`, `dns = 192.168.15.200,192.168.15.100`), had the servers in the profile and no effect: names went to the machine's own
name server, which does not know the far side's names.

## Decision

1. **`dns` applies with or without everything.** It is set on the jack in systemd-resolved for every name (`~.`), as before, whenever the profile has it.
2. **Without everything, a name server must be one the line reaches**: inside the jack's own network (a fixed address) or one of the routes. `longwire jack` (and
   Turn on) say so before any password, and the root helper refuses one that is not, before it changes anything. With everything, any server is reached through
   the line's way out, as before.
3. It is the device's permission (`org.longwire.jack`, [0058](0058-the-device-and-the-whole-machine-are-two-permissions.md)): a name server on the line changes no route.
4. As before, systemd-resolved passes over a jack with no carrier, so names return to the machine's own name server whenever the node is gone; `longwire show` names
   the servers in use.

## Consequences

The pane's *Name servers* row is there whenever the jack is; empty, it reads *this machine's own*. Every name goes to the listed servers while the node runs, not
only the far side's; asking them for one domain alone is *(planned: M5)*. Tested: a server on the line's network and on a routed network set, one the line does not reach
refused by the helper and by `longwire jack` before any password (`tests/jack.sh`); the row with everything off (`tests/pane.sh`).
