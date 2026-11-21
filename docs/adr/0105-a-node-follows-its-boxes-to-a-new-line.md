# 0105 — A node follows its boxes to a new line

Status: accepted; built in 0.5.79. The owner's pair of boxes was moved to a new host with fresh state: new keys, new numbers, a new line. Every desktop and phone was on the old line, and two nodes on different lines hang up on each other ("that node is on another line"), so each would have had to leave its line by hand before it could join the boxes again.

## Decision

1. **A node leaves its line for another one by itself when all of these hold:** a person dialled the number (not the mesh, not a ring); its line is not an operator's; and no other member of its line is in touch (no session up). A line whose members have all gone is no line to stay on, and a person dialling a box of another line has said where they want to be.
2. **It says so in the line hello** (the first byte, 2: "on a line, and leaving it for yours"), forgets its line (`line-left`, with the reason), and the box admits it as a newcomer, as for any node with no line. An older box reads the byte as "on a line" and hangs up, as before: nothing is lost, the person dials again once the box is updated.
3. **With a member of its line up, it stays,** and the call fails with "that node is on another line", as before: a desktop that is on a working line does not wander to another because a number was dialled.

## Consequences

- After a move of the boxes, a desktop or phone needs the new number dialled once (the new boxes have new numbers: that is the pairing) and nothing else.
- What the node knew of its old line (members, its admit) is gone; the numbers it trusted for DHCP are kept only as far as they are members of the new line (ADR 0101).
- Tested (`tests/moveline.sh`): staying while a member is up, leaving when none is, joining the new line, both new boxes in touch, every ring let in by itself.
