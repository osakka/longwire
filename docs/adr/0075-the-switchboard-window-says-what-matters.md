# 0075 — The Switchboard window says what matters

Status: accepted; built in 0.5.60. The owner's remarks on the window: a key shown that way is senseless, the window should be more organised, with a compact mode, and show what is relevant.

## Context

The window's heading was the switchboard's own address cut to 30 characters and an ellipsis, then its short code, uptime and policy. A key cut short can be neither read, nor compared, nor copied; it said nothing
about the switchboard. What a person running one wants to know at a glance is whether it is well: how long it has been up, which release, how full it is, whether anything is being banned, and whether nodes are being
refused for a reason they would not guess. That last is no idle matter: a node whose clock was set back is refused by the handshake's replay check with no sign anywhere ([ADR 0071](0071-a-switchboard-never-starts-a-handshake-to-a-node.md) counts it now).

## Decision

1. **The heading names the switchboard and how it is**: `Switchboard`, its short code, uptime, its policy and its version (the status now carries `version`: an older switchboard shows none, and a person can see at once
   that it is not the release they think).
2. **One line says what is wrong, and nothing is said when nothing is**: bans in force, handshakes refused as replays, no short code. It is in the warning colour, under the numbers.
3. **The whole address is one key away**, not on the screen: `a` (Node > Copy Switchboard Address) puts it on the clipboard, for a node's `--switchboard HOST:PORT/ADDRESS`, with a note saying so.
4. **A compact mode**: `m` (View > Compact, kept as `DT_SWCOMPACT`) keeps the heading, the online bar, one line of the numbers (rates, registered, rings, through, bans) and the nodes; the graphs and the second
   counters row go. A window of fewer than 16 rows is compact whatever was asked.

## Tests

`tests/sw.sh`: the heading names the switchboard, policy, uptime and version and has no key; compact draws fewer lines and keeps the numbers and the nodes; a small window is compact; the alert line says bans and replays and
says nothing when there are none; `a` copies the whole address; `m` toggles; Compact is in the View menu.
