# 0094 — A phone learns the other box of a pair

Status: accepted; built in the release after 0.5.73. A phone that dials one box of a pair and finds it dead has no way to reach the other: the box that would have told it (the vouch of [ADR 0092](0092-a-leaf-keeps-the-other-box-of-a-pair-too.md)) is the one that is gone, and the vouch is kept in memory only ([ADR 0089](0089-who-may-speak-for-the-network-is-not-kept-across-a-restart.md)). The owner asked for no workaround such as listing both numbers by hand. (Ticket LW-16.)

## Decision

1. **The engine says who has been vouched for:** the control word `partners` lists, as numbers, the members that a member this node asked for has vouched for as on its own segment.
2. **The app keeps them:** while the line is up it asks every ten seconds, and keeps each number it is told, with the time it was last told (`Partners`, in the app's preferences). At the next start they are added to the numbers it may dial, after the ones the person listed, and named to the engine as asked.
3. **It is visible and removable:** the Line card shows them (`learned`) and has a **Forget learned numbers** button. A number not vouched for again in thirty days is dropped by itself.
4. **Trust:** a learned number may answer DHCP for the phone, as a listed one does. That is the one place ADR 0089's reasoning (a number with the power to answer for the network for ever, after the person has forgotten it) is set aside, and only for a number that a box the person chose vouched for, that the person can see and forget, and that lapses.

## Consequences

- A phone that has once been joined to a pair reaches the surviving box after a restart with the dialled box dead, without the person typing a second number.
- A box that was replaced for another keeps its number in the list for up to thirty days unless forgotten; the phone tries it with the others (a failed try costs the usual wait).
- Tested in the engine (`partners` after a join, `tests/m2d.sh`); the app's keeping and its use at start are compiled, not run on a phone, and have no test of their own (LW-18).
