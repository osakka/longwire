# 0004 — A node never runs as root; any privilege is a one-time grant

Status: accepted

## Context

The brief was: no interfaces, no complication and no root. Layer 2 bridging cannot be done with no privilege at all, so
the question is how little, for how long, and how it is granted.

## Decision

- **splice** needs `CAP_NET_RAW` to open a packet socket. The service manager grants it once, as an *ambient capability*
  to the node's process only. `lw start` opens the socket, forks the engine worker, and then clears every capability set
  (effective, permitted, inheritable, ambient) in both processes and sets `no_new_privs`. No script code that runs
  afterwards holds any privilege.
- **jack** needs a tap device the user owns, created once: `ip tuntap add mode tap user $USER`.
- **shell** uses unprivileged user namespaces and needs nothing *(planned: M5)*.

hibr itself is never given a capability and never made setuid, which keeps hibr's own
ADR 0016 ("privileges are given up, never taken") intact: privilege only ever goes down.

## Consequences

There is one privileged step per attach mode, and `longwire setup splice NIC` and `longwire setup jack` print it. The
privileged moment never searches the environment for code, because the splice unit names the module by absolute path.
One side effect: `no_new_privs` lands on the hibr process that ran `lw start`, so `sudo` stops working in that shell
afterwards ([CLI](../CLI.md#scripting-it-yourself-the-lw-builtin)). The test suite checks that every worker ends with
empty capability sets even when started with the capability.

---

[← decisions](README.md)
