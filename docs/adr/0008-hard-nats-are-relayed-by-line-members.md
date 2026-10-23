# 0008 — Hard NATs are relayed by line members, never by the switchboard

Status: accepted *(planned: M3)*

## Context

Connectivity must not touch the switchboard once a call is up. But some NAT pairs (both endpoint-dependent, or carrier
NAT on both sides) cannot be punched, and failing outright is not acceptable for a product meant to "just work".

## Decision

A call that cannot go direct is relayed by another member of the same line that has a public address. The relay sees
only sizes and timing, because the traffic stays end-to-end encrypted between the two endpoints; the switchboard stays
out of the data path. Before relaying there is a port-spray rung for the case where one side is easy and the other hard
([PROTOCOL §4](../PROTOCOL.md#4-nat-traversal)).

## Consequences

Relaying depends on lines of more than two nodes existing, so it is part of M3, not M1. Until then two strict NATs fail
cleanly with a plain message and nothing left half-open, which the symmetric-NAT tests check (against a cone NAT, and against another symmetric one).

---

[← decisions](README.md)
