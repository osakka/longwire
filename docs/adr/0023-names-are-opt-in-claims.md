# 0023 — Names are opt-in claims: a node chooses to announce one, a switchboard chooses to carry it

Status: accepted

## Context

A number says nothing about who is calling, and typing a number is the only way to reach someone. People expect a phone
to show a name when it rings (caller ID) and to have a book to look people up in (a directory). Both put a name on a
server, and a name is a claim: nothing proves it. The switchboard is also the one place that must not learn more than
it has to ([0010](0010-numbers-are-allocated-and-bound-to-keys.md)).

## Decision

Both are optional at both ends, and the switchboard verifies nothing.

- **Node:** `--name "TEXT"` is the name it announces; with none, it announces none. `--listed` puts that name (and the
  number) in the switchboard's directory; without it the node cannot be found by name or looked up by key. Both default
  to *off*: nothing about a node leaks unless its owner asks.
- **Switchboard:** `--caller-id on|off` and `--directory on|off` (both default *on*, so an operator forbids rather than
  permits). Off, the switchboard drops the names it is given (caller ID) or answers no lookups (directory). A node
  learns which are on from a flags byte in `NUMBER`, so it can say "this switchboard has no directory" rather than fail.
- **Caller ID** is the name the switchboard holds for the caller, added to `RING`. The callee's `ring` event carries it,
  and the node says it as a claim: *says it is "…"*. The number and the safety phrase stay the proof.
- **Directory:** `LOOKUP` by key id (which makes a full address `lw1…` dialable) or by name (at most 8 results,
  each with number and key id, so the safety phrase still does the proving). Only listed nodes are found. Names are not
  unique and nothing makes them so.
- A name is at most 32 bytes of UTF-8 with no control characters; a registration carrying a bad one is refused, not
  truncated. Names are held in memory only and re-sent when a node registers, so they are never written to the registry.
- `DIAL` and `RING` carry their optional parts as tagged trailers (`tag ‖ len ‖ bytes`), replacing the single untagged
  admit, so another optional part is one more tag and not another guess at the end of a packet.

## Consequences

The switchboard now holds names it is given (in memory, only for nodes that chose to give one), which
[SWITCHBOARD](../SWITCHBOARD.md) and the [threat model](../THREAT-MODEL.md) say. A malicious switchboard can hand out
a wrong name or key for a lookup; a first call to a looked-up key is still a first call, with a safety phrase to read
out. A private switchboard can run with both switches off. The default for the node is the conservative one and can be
flipped in the node's config without touching the protocol.

---

[← decisions](README.md)
