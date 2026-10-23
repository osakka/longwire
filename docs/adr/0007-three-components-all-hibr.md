# 0007 — Three components, all hibr: a switchboard script, a node script, a module

Status: accepted

## Context

The first design had a standalone C binary plus a hibr module. hibr is the user's own runtime, installed wherever this will
run, and the UIs, servers and desktop integration are natural there. A fourth, separate component would only duplicate
what hibr already provides.

## Decision

There are exactly three components: (1) `longwire-switchboard`, a hibr script; (2) `longwire`, the node (client and
server together, with its user interface), a hibr script; (3) `longwire.so`, a hibr module holding the whole engine in C.
**C handles bytes and keys; hibr makes the decisions** ([0015](0015-scripts-decide-c-enforces.md)). The packet path runs in
a C worker the module forks, the same shape as hibr's `hold` server, so the interpreter never touches a packet. The
engine's C never includes `hibr.h`, so the Android app can later reuse it; that is discipline, not a fourth component.
The module tracks `HIBR_ABI`; the engine does not.

## Consequences

Every node and every switchboard needs hibr installed, routers included, and there is no single file to `tcc -run`.
In return the scripts are plain text that can be tuned without a C release, and the desktop pane, the CLI and your own
scripts all use the same `lw` builtin. The module is built out of tree against `../hibr/include/hibr.h` and loads at
ABI 16 without touching hibr; the engine is compiled separately from `hibr.h`, because its helper names would otherwise
collide with hibr's headers.

---

[← decisions](README.md)
