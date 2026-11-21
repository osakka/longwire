# 0110 — An eviction reaches the members that were away

Status: accepted; built in 0.5.80. The owner evicted the old boxes on one member; the desktop, not in touch at that moment, kept them on its record (`longwire peers` still listed them).

## Decision

On a line without an operator, each time two members link, each tells the other every eviction it holds, signed in its own name (`M_EVICT`, as a member's eviction always is). A member that lacks one applies it, as if it had been told at the time; one that has it, or does not have that member on its record, ignores it.

## Consequences

- Evicting on any member is enough: the others take it when they are next in touch.
- Tested (`tests/evictsync.sh`): a member away while another is evicted takes it off its record when it links again.
