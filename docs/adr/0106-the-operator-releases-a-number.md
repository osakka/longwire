# 0106 — The operator releases a number

Status: accepted; built in 0.5.80. The owner moved a pair of boxes to a new host with fresh state, and asked how to take the old boxes' numbers off the switchboard.

## Decision

- `longwire-switchboard release NUMBER` (engine request `release NUMBER`, module `lw sb release NUMBER`) releases a number now, as a year unheard would: the registry forgets it, the log records it as `released`, and it is **held back** for as long again (`--release-after`), so a stale pin never meets a stranger with that number.
- A number that is **online is not released** (said plainly), and one that is not registered says so.
- Only the user the switchboard runs as, or root, may release (as for lifting a ban).

## Consequences

- Decommissioning a box: evict it from its line (`longwire evict NUMBER` on any member), then release its number on the switchboard.
- Tested (`tests/sbrelease.sh`): refused while online, an unknown number named, released once the node is gone, still released after a restart.
