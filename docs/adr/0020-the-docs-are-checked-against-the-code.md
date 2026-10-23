# 0020 — The documentation is checked against the code

Status: accepted

## Context

Documents describing a design drift from the code that implements it, and drift went unnoticed here for two milestones:
files that were never written, options renamed, a limit quoted from an earlier draft.

## Decision

`tests/docs.py` runs with `make check` and fails when the docs disagree with the code: every link and anchor resolves,
every repository path a document names exists, every `lw` verb and `longwire` command exists and every one that exists is
documented, every event the workers emit is documented, the message ids and datagram types in the tables match the
headers, and the sizes and limits the documents quote are the code's. Designed-but-unbuilt design carries one marker,
`*(planned: M3)*`, naming the milestone that builds it, and no other wording ("not yet", "not built") is used outside the
roadmap. Decisions are recorded one per file here, in the order taken, as hibr does.

## Consequences

A document can be wrong in ways the check cannot see, such as prose, so the check complements reading and does not
replace it. A line that carries the marker is exempt from the checks that would otherwise fail on it, which is the point:
the marker is the one place unbuilt design may live.

---

[← decisions](README.md)
