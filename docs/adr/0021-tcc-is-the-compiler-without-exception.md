# 0021 — TCC is the compiler, without exception

Status: accepted

## Context

hibr builds with TCC and, in its own ADR 0001, keeps gcc as a documented option for
profiling and sanitizers. longwire began the same way: TCC by default, with a `make CC=gcc OPT=-O2` alternative, and for
two milestones the tests were also run on a gcc build as a cross-check. That alternative is a door left open. A second
compiler gets used "just for speed" or "just for this platform", and then there are two products.

## Decision

**TCC is the official compiler, and the only one, with no exceptions.** Everything that is built is built by `tcc`: the
module, the vendored crypto, the tests' subjects, every release, every package. There is no gcc or clang build path, no
optimisation flags, and no other compiler used for sanitizers, profiling or linting. The Makefile says `override CC =
tcc`, so even `make CC=gcc` runs TCC. Warnings come from TCC itself: `make lint` compiles everything we wrote with
`-Wall -Werror -Wunsupported`, and `make check` runs it first.

Speed is pursued inside the constraint: better algorithms, batching and a packet ring, and hand-written assembly that
TCC's own assembler turns into object code ([ROADMAP](../ROADMAP.md) M7).

## Consequences

The cipher stays at about 290 Mbit/s per core under TCC until the assembly work lands, and that is the figure the
project quotes. TCC has no sanitizers, so memory safety rests on `-Wall -Werror`, small parsers, bounds checked by hand,
the end-to-end tests, and fuzzing with a harness TCC builds *(planned: M8)*.

Every platform must be reachable with TCC. For Linux and the BSDs and macOS that is the same position as hibr's. For the
Android app the engine has to be built by a cross-compiling TCC into a library an Android app can load; whether TCC can
produce that is **unverified** and has to be settled before the app starts, since the rule leaves no fallback
*(planned: M6)*. If it cannot, the decision is the project's to revisit explicitly, not something to route around.
