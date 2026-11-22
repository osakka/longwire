# 0113 — One line and one key, whatever the timing

Status: accepted; built in 0.5.81. Two faults of the same kind, found while the owner's new pair was set up.

1. **Two dials at once made two lines** (BUG-13). A node with no line that dialled a second number before the first callee's line had reached it said "no line" to the second callee, which founded a line of its own; the node ended on one of the two, and the other kept it on record. The two boxes never met.
2. **The line key went apart.** An eviction gives the line a new key, told only to the members in touch at that moment; a member away kept the old one, and one that joined through it got the old one too. A pair's secret is made from the line key: the owner's two boxes, on one line, held different keys, could not hear each other on their segment ("1 on it"), and the Switchboard window drew no line between them.

## Decision

1. **A node with no line holds its other line handshakes while one is under way** (a session whose hello was sent and whose line has not come, for up to 15 s). When the line comes, or the wait is over, the held hellos go: the second callee then meets a node that is on a line, and is admitted to it.
2. **The newest line key wins.** Each key carries the time it was made (`key_gen`, wall clock in ms, saved with the line, carried at the end of `M_LINE` and in `M_LINEKEY`). Members tell each other their key each time they link; a member takes a newer one (the same time: the greater key), passes it to every other member it is linked to, and tells an older one back. Every change reaches every member through whoever is in touch.
3. `longwire peers` shows `key_fp`, four bytes of a keyed hash of the line key: equal on members that hold the same key, and it reveals nothing of it.

## Consequences

- A pair whose keys had drifted apart takes one key at the next link between its boxes (or any member in touch with both), and hears itself again.
- Tested: `tests/linerace.sh` (the first box behind a link of 1.5 s each way; the second number dialled the moment the first call is up: one line, all in touch; 0.5.80 ends on two lines); `tests/evictsync.sh` (after an eviction made while a member was away, the members end with one key).
