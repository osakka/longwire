# 0086 — The switchboard window shows a pair as a pair

Status: accepted; built in 0.5.70. The owner, with vpn-1 and vpn-2 up: should the Switchboard window show active and standby, as a group, a pair? (Ticket LW-8.)

## Context

The switchboard knows registrations, not lines or sites: vpn-1 and vpn-2 are two numbers and two names, with nothing to say they are one site and that one of them carries the traffic. Only the boxes know (the site group: probes on their own segment).

## Decision

1. **A splice box in a site group tells its switchboard** which it is (active or standby) and which boxes share its segment (`SITEINFO`, [PROTOCOL](../PROTOCOL.md)): when either changes, and every 30 s while it holds. Over the session it already has, authenticated by it. A box alone on its segment says nothing.
2. **The switchboard keeps it in memory** (never written to a log), as what the node claims, for two minutes since it was last heard, and lists it with each node (`site`: `active`, `standby` or empty; `with`: the numbers of the boxes it names that are registered here). **It decides nothing by it**: it is for the operator's eyes.
3. **The window shows it**: the nodes of a pair sit together, each row says its role and its partner (`active with 353-768-703`, `standby with 107-958-349`), and the heading counts the sites with a standby.
4. **It can be turned off**: `--site-info off` on the box. A switchboard run by someone else learns which nodes share a segment from this (it could already tell by their public address, which is the same behind one NAT); a node that does not want that says so.

## Not done

The window does not show *why* a role changed, or the history of takeovers; a pair is shown only while both boxes keep telling the switchboard.

## Tests

`tests/m2c.sh`: on the real join through a real switchboard, in both key orders, the switchboard lists both boxes with the right roles and each other as partner, and what it holds is what the boxes say of themselves. `tests/sw.sh`: a pair is listed together, each row says its role and partner, and the heading counts sites.
