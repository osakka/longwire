# 0014 — A default public switchboard, operated by the project

Status: accepted

## Context

Nodes should work out of the box: a first run registers without anyone typing an address.

## Decision

There is a default public switchboard, run by the project owner. Its address and key are a default in the node *script*,
not in the module, so moving or re-keying it is a catalogue update rather than a new package in four repositories.
Anyone can point elsewhere with `--switchboard host:port/lw1…`, and `longwire-switchboard` runs another one.

## Consequences

The host and key have not been supplied yet, so `--switchboard` is required today. Whoever runs a switchboard sees which
numbers are online, from which addresses, and who rings whom until it forgets; for the default one that is the project,
and [THREAT-MODEL](../THREAT-MODEL.md) says so and points privacy-minded users at running their own. Registration limits,
tokens and ring limits are the script's defaults and can be tuned per instance.

---

[← decisions](README.md)
