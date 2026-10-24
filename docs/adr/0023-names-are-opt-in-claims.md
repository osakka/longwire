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
  each with number and key id, so the safety phrase still does the proving). Only listed nodes are found.
- **A listed name is unique, like a number.** The switchboard binds it to the first key that lists it and refuses anyone
  else, whether they list it or only announce it ("that name is listed by another node"), so a listing cannot be
  impersonated. Names compare without regard to ASCII case and byte for byte beyond that (no Unicode folding). The binding
  is registry data, kept in `names.log` beside `numbers.log` and so it survives a restart; it is released when its owner
  registers with a different name or without listing. A name that nobody has listed is a plain claim and may be shared.
- A name is at most 32 bytes of UTF-8 with no control characters; a registration carrying a bad one is refused, not
  truncated. The names themselves are re-sent whenever a node registers; only the binding of a listed name to its key is written down.
- `DIAL` and `RING` carry their optional parts as tagged trailers (`tag ‖ len ‖ bytes`), replacing the single untagged
  admit, so another optional part is one more tag and not another guess at the end of a packet.

*Amended after review:* the first version let several nodes use one name, on the model of a phone book. That is wrong for
an operator-grade service, where the namespace belongs to the operator; a listed name is now treated as a number is.

## Consequences

The switchboard now holds the names it is given (only from nodes that chose to give one) and the bindings of listed ones, which
[SWITCHBOARD](../SWITCHBOARD.md) and the [threat model](../THREAT-MODEL.md) say. A malicious switchboard can hand out
a wrong name or key for a lookup; a first call to a looked-up key is still a first call, with a safety phrase to read
out. A private switchboard can run with both switches off. The default for the node is the conservative one and can be
flipped with `--name` and `--listed` without touching the protocol.

---

[← decisions](README.md)
