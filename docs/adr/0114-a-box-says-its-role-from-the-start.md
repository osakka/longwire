# 0114 — A box says its role from the start

Status: accepted; built after 0.5.81. On the owner's new pair, vpn-2 said "this box is now active for its LAN segment (2 on it)", and vpn-1, the standby, said nothing at all: a box starts as the standby while it listens, and said "standby" only when it went from active to standby, so a box that was the standby from its start never said so.

## Decision

A box says its role (`site` event, `active` or `standby`, with how many boxes are on its segment) once it has listened and the election has settled, and again at every change. The takeover itself (the frames that move the LAN's switches) is unchanged.

## Consequences

- Each box of a pair says what it is in its own log, the standby included: "this box is now standby for its LAN segment (2 on it)".
- Tested (`tests/pairauto.sh`): both boxes of a pair say their role, and the last role each said is the one its status shows.
