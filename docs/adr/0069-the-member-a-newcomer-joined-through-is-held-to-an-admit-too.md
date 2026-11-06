# 0069 — The member a newcomer joined through is held to an admit too

Status: accepted; built in 0.5.55. Found by a third independent read (of 0.5.54), given the code and not the author's conclusions. It found no memory-safety bug in the control-message splitting
([0067](0067-a-control-message-too-big-for-one-session-message-is-sent-in-two.md), [0068](0068-a-ring-with-an-admit-is-sent-in-two-to-a-node-that-says-it-can-take-two.md)) and three gaps in the
admit gate of [0066](0066-a-member-shows-its-admit-before-anything-it-sends-counts.md).

## Context

1. **The hub was never asked for an admit.** The gate is set when a session comes up with a member we have on record. A newcomer's session with the node it joins through comes up before the
   newcomer has a line, so there is no record then; when the line message makes the hub a member the gate was never set, and the hub never showed an admit. A hub that was no longer a member (its
   admit ended, or it holds a copied key) could describe a line of its own, with the right line id and operator, to a newcomer holding a good admit, and keep it for as long as the session lasted,
   with no revocation or roll ever reaching it.
2. **A revoked founder could come back after a roll.** The founder's own admit is the root of the line and is held to no end and no roll. A roll drops every revocation older than itself, so a
   member that missed the revocation of the founder (it was offline when it went round) kept the founder, and nothing could now tell it.
3. **A member that had not shown its admit could set the line key.** The key derives the site secret; a member in its first seconds could change it.

## Decision

1. **The hub shows its admit to the newcomer**, and the newcomer, like every member, waits for it (20 s, then the member is off the line) and lets nothing of the hub's count until it holds.
   The same holds for any peer that becomes a member while already in session.
2. **A revocation of the founder is kept for good**: not dropped by a roll, by the horizon or to make room by age. It is one entry, and members re-send what they hold to those who link later.
3. **A line key from a member that has not shown an admit is not taken.** (A key change that arrives in the first moments of a session is lost to that peer; it holds the old key until the next
   change or until it joins again, and the site boxes' secrets agree again when they do.)

## Tests

`tests/switch.sh` (a newcomer waits for the hub's admit; a line key before it is not taken and after it is; the founder's revocation is held after a roll and another pruning).
