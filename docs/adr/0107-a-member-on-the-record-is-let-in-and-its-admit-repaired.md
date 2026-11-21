# 0107 — A member on the record is let in, and its admit is repaired

Status: accepted; built in 0.5.80. The owner's desktop held an admit that named another key ("the admit is for another node: it names d7c39af0, the caller is 7750f9fe"); every box of its line, which had the desktop on its own record of the line, still refused to let its calls in by themselves, and with headless boxes nobody could pick up. How the desktop's admit came to name another key was not found; the fix does not depend on it.

## Decision

1. **On a line without an operator, a caller that is on this node's own record of the line, and not evicted, is let in by itself**, whatever admit it shows. The record is the line's word that it is a member; an admit is the proof for a node that has no record of it. (On a line with an operator nothing changes: the operator's admit is the only word.)
2. **Admits are repaired between members.** A member whose own admit does not verify for it shows it to each member that has it on record when they link (`M_ADMIT`); that member makes it a new admit, signed in its own name, and sends it back; the member takes it if it names its own key and verifies, saves it, and says `admit-repaired` (with `by`, the member that made it).

## Consequences

- A lost or wrong admit no longer locks a member out of its own line, and it heals at the first contact.
- Tested (`tests/admitheal.sh`): a member restarted with another member's admit is in touch with both by itself, every ring let in, and holds a new admit for its own key; released 0.5.79 refuses those rings with the owner's message.
