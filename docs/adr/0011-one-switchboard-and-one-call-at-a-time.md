# 0011 — One switchboard per deployment, and one call at a time

Status: accepted

## Context

Federation and multi-line nodes are large features, and neither is needed to prove the rest.

## Decision

A deployment has one switchboard and numbers are local to it. A node is on at most one call, and a call is between two
nodes.

## Consequences

A second ring while on a call is answered busy. Federation (`number@sb`) *(planned)* and lines of three or more nodes
*(planned: M3)* are later work that does not change the switch's design.

---

[← decisions](README.md)
