# 0006 — The v1 throughput target is about 300 Mbit/s, built with TCC

Status: accepted

## Context

TCC compiles in about a second and keeps the build dependency-free, but it does little optimisation and has no SIMD
intrinsics or `__int128`. That costs symmetric throughput.

## Decision

Measured on the design machine: TCC runs ChaCha20-Poly1305 at about 290 Mbit/s per core on 1400-byte packets, gcc -O3
about 2 Gbit/s. The brief's target is "300 for starters". The default build is TCC; the same source builds with gcc or
clang (`make CC=gcc OPT=-O2`) when more is needed.

## Consequences

Link speed, not the cipher, is the limit on most real WAN links. End-to-end, the only throughput measured so far is the
test suite's TCP transfer across two splice nodes over veth pairs, about 50 Mbit/s with a TCC build; a proper benchmark,
batching, a packet ring and x86-64 assembly for ChaCha20 *(planned: M7)*.

---

[← decisions](README.md)
