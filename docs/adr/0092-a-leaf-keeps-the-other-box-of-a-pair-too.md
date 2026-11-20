# 0092 — A leaf keeps the other box of a pair too

Status: accepted; built in 0.5.73. A mistake of [ADR 0087](0087-a-leaf-keeps-only-the-sessions-it-asked-for.md), found on the owner's phone and the boxes vpn-1 and vpn-2.

## What went wrong

The phone dialled vpn-1 only. vpn-1 was the standby box of its pair, and a standby box does not forward a member's frames to the LAN (only the active one does, [ADR 0083](0083-a-pair-of-boxes-is-one-site-and-a-bridge-carries-no-control-frames.md)). Before 0.5.71 the phone meshed with the whole line, so it also had a session with vpn-2, the active box, and got its lease through it. As a leaf it refused vpn-2's rings and did not dial it: it held one session, with a box that dropped everything (`drop_standby` rising at vpn-1), and showed "asking its network for an address" for ever. The pair test (`m2d`) passed because its phone was not a leaf, as the app's is.

## Decision

1. **A leaf keeps a session with a member that a member it asked for has vouched for** as on its own segment ([ADR 0080](0080-only-a-member-we-asked-for-may-speak-for-the-network.md), `M_SITE`: the other box of a pair). It dials such a member from the line's roster, with the mesh's backoff, and lets it in when it rings. Nobody else is dialled or let in.
2. **The vouch is the same one that lets that box answer DHCP for the leaf:** it comes only from a member the leaf asked for, so the leaf keeps what it is told of by the boxes it chose and nothing more.
3. **The test's phone is a leaf, as the app's is** (`tests/leafnet.c` sets `leaf`), so `tests/m2d.sh` covers a phone that dials one box of a pair: without this change it fails (the renewed lease through the surviving box), with it it passes.

## Consequences

- A phone that dials either box of a pair holds sessions with both, and gets its address and its traffic through whichever is active, and keeps them when the roles change.
- The phone still keeps no session with the rest of the line.
- Ticket LW-13 (the others stop ringing the phone) is unchanged.
