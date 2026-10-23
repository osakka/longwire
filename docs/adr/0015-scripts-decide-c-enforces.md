# 0015 — Scripts decide policy; C enforces what must not be skippable

Status: accepted

## Context

With the engine in C and the application logic in hibr, the line between them has to be drawn on purpose: too much in C
and every policy change is a release; too much in scripts and a script bug is a security hole.

## Decision

- **Scripts decide policy.** The switchboard script decides who may register (tokens, reserved numbers, a limit per
  address) and whose ring goes through. The node script decides what a ring means. The worker emits each decision as a
  JSON event and takes the verdict back (`lw sb reply`, `lw pickup`, `lw reject`). A switchboard worker whose script does
  not answer in 2 seconds refuses; a node worker whose user does not answer in 60 seconds refuses.
- **C enforces what cannot be skipped.** The pin store is checked in C when the other end's key arrives, so a script
  cannot forget; a changed key drops the call. Keys and the handshake budget never leave C.

A script can make the engine more restrictive, never less.

## Consequences

The part most likely to be tuned for a public switchboard (invite-only, stricter limits, vanity numbers) is a text file.
The price is a round trip through the script for every registration and ring, which is a few events per call and none per
heartbeat.

---

[← decisions](README.md)
