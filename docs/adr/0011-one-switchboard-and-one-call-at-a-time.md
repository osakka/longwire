# 0011 — One switchboard per deployment, and a person is asked about one call at a time

Status: accepted

## Context

Federation and multi-line nodes are large features, and neither is needed to prove the rest.

## Decision

A deployment has one switchboard and numbers are local to it. A node is on at most one call, and a call is between two
nodes.

## Consequences

Lines of three or more nodes have since been built ([0022](0022-lines-form-by-vouching.md)), so a node can be on many calls
at once; only a person is asked about one ring at a time (a second unvouched ring is answered busy). Federation
(`number@sb`) *(planned: M9)* is later work that does not change the switch's design.

---

[← decisions](README.md)
