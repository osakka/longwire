# 0068 — A RING with an admit is sent in two to a node that says it can take two

Status: accepted; built in 0.5.54. Closes the open item of [ADR 0066](0066-a-member-shows-its-admit-before-anything-it-sends-counts.md) and [ADR 0067](0067-a-control-message-too-big-for-one-session-message-is-sent-in-two.md).

## Context

A `RING` carries the caller's whole bundle (1249 bytes, ML-KEM's public key rides in it), the candidate addresses, the caller's name and, for a call between line members, the caller's admit (147 bytes).
With an admit it is 1.44 to 1.5 KB, a datagram of 1.47 to 1.5 KB: a 1500-byte path carries it as two IP fragments and a path that drops fragments does not carry it at all (the test network's links did
not, when an admit grew by 8 bytes in 0.5.51). Splitting it at the session layer (0067) is the cure, but a node that has not been updated cannot put two parts back together, so a switchboard that
split every RING would take every line call away from the nodes that are not updated yet.

## Decision

1. **A node says it can take a control message in two parts**: its `REGISTER` carries an empty tag `TAG_CTLFRAG` (6). A switchboard that does not know the tag skips it, as every unknown tag is skipped.
2. **A switchboard splits to a node that said so, from 1428 bytes** (the size a frame is split from); to any other node it sends a message whole when it fits one session message, as every release has.
   A node that has not been updated says nothing, so it gets exactly the datagram it always got; an updated node on a switchboard that has not been updated gets the same, because that switchboard does
   not split. There is no order in which they must be updated.
3. **A node takes a control message in two parts of any size above 1428 bytes** (it did above 1514 only, in 0.5.53).
4. **The switchboard's status counts them** (`ctl_split`).

## Tests

`tests/switch.sh` (1450 bytes goes whole to a peer that has not said it can take two, in two to one that has; 1700 bytes in two to anyone; a message that fits one datagram is not accepted in two),
`tests/bigline.sh` (a line of 14: the switchboard sent 83 rings that carry an admit in two parts, and every member lists all 14).
