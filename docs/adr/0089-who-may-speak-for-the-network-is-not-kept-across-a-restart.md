# 0089 — Who may speak for the network is not kept across a restart

Status: accepted (a decision not to build); superseded for a desktop by [ADR 0101](0101-who-may-speak-for-the-network-is-kept-for-thirty-days.md), which keeps it for thirty days. Ticket LW-14 proposed writing the dialled numbers (`asked`) and the vouched boxes (`vouched`) to the state directory so that a restart need not ask again. It is closed without that.

## Context

[ADR 0080](0080-only-a-member-we-asked-for-may-speak-for-the-network.md) lets a DHCP, DHCPv6 or router reply through only from a member this node asked for, or that an asked member vouches for. Both lists are in memory: `asked` holds the last eight numbers a person dialled, `vouched` the keys an asked member says share its LAN segment.

## Decision

1. **Not kept.** Neither list is written down.
2. **The vouched list refills by itself.** A box says who is on its segment every 5 seconds ([ADR 0083](0083-a-pair-of-boxes-is-one-site-and-a-bridge-carries-no-control-frames.md)); after a restart the window in which the other box of a pair cannot answer DHCP for this node is that long, once its session is up.
3. **The asked list is a person's act.** A restart that comes from a person is followed by what the person says again: the profile's dial, the `--peer`, the phone's list (sent by the app at every start). Keeping it would give a number the power to answer for the network with no end, after the person who gave it has forgotten.

## Consequences

- A file of trust decisions, which would need its own protection, its own expiry and its own tests, is not added to the state directory.
- A node that is restarted by a supervisor with only `longwire up` (no profile, no dial) asks again by the next dial. That is the same as the first start.
