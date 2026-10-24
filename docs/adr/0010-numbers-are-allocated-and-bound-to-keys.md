# 0010 — Numbers are allocated by the switchboard and bound to keys

Status: accepted

## Context

Each node needs a short number to be dialed by, tied to a key or token so that only that holder has it. A number derived
from the key was considered: a short derived number can be ground out by brute force, and a long one is no longer a phone
number.

## Decision

The switchboard allocates 9 digits, 8 random plus a Damm check digit, and binds each to a key, first come, for good.
Registering again with the same key returns the same number; a different key can never take it. A token can reserve a
specific number. The full address (`lw1…`, the key id in base32) is available for anyone who wants to trust the
switchboard for nothing.

## Consequences

The check digit catches every single-digit typo and adjacent swap before anything is sent. A number follows the 32-byte
seed. A number unheard for a year is released and held back for another (`--release-after`, [SWITCHBOARD](../SWITCHBOARD.md)); its owner coming back in that time gets the same one. A re-issued number would show up on every node that pinned the old key as `key-changed`.

---

[← decisions](README.md)
