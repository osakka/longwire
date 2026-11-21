# 0109 — Two boxes named to each other pair by themselves

Status: accepted; built in 0.5.80. A pair of boxes became one site only after one dialled the other and someone picked up (OPERATING, "Two boxes for one site"). The owner's boxes are headless containers with no console: nobody can type `longwire dial` or `longwire pickup` there.

## Decision

- `longwire up --pair NUMBER` names the other box of the pair. The box dials that number every 30 s until a call with it is up or under way, and answers a ring from it by itself (under any answer policy but `never`). Given on both boxes, each with the other's number, the two consent to each other and pair with nobody acting.
- The number is the switchboard's word on first contact; from then on the key that answered is pinned, and a different key under that number is refused (as for any number).
- Nothing else changes: the pair is two numbers, one active and one standby (ADR 0083); a member holds a session with both.

## Consequences

- A pair is set up in the boxes' configuration; restarts and rolls need nothing.
- Tested (`tests/pairauto.sh`): two fresh splice boxes stay apart without it, pair by themselves with it (one active, one standby, each counting the other), and again after a restart.
