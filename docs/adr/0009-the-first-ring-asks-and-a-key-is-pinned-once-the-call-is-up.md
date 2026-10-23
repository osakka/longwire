# 0009 — The first ring asks, and a key is pinned once the call is up

Status: accepted

## Context

Dialing a number puts the caller on the callee's LAN, so letting anyone in unasked would be a security hole. Separately,
the switchboard hands out the callee's key on the first call, so a lying switchboard could substitute its own.

## Decision

A ring is answered according to the node script's **answer policy**: `ask` (the default; wait up to 60 seconds for
`longwire pickup`), `known` (pick up numbers connected before, ask about the rest), `auto` (headless boxes and tests) or
`never`. Both screens show the same **safety phrase**, six words from the hash of the two key ids; reading them aloud
defeats a lying switchboard on the first call. The key is **pinned** (number → key id) *when the call is up*, not when
the introduction arrives, so a phrase that doesn't match leaves nothing to undo; `longwire unpin` forgets a pin. The pin
check itself is in C ([0015](0015-scripts-decide-c-enforces.md)): a number that answers with a different key than before
drops the call and raises `key-changed`.

## Consequences

The first call trusts the switchboard unless the phrase is compared. If the *caller* drops a call on a key mismatch, the
callee is not told and waits out the 8-second punch timeout. The policy `line` (a member's admit rings through
unprompted) comes with lines *(planned: M3)*.

---

[← decisions](README.md)
