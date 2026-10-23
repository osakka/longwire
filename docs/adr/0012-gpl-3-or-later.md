# 0012 — The licence is GPL-3.0-or-later

Status: accepted

## Context

hibr is LGPL-3.0-or-later. longwire's engine is loaded by hibr and will later also be linked into an Android app.

## Decision

Everything in the repository is GPL-3.0-or-later: the engine, module, scripts and docs. The text is in
[`COPYING`](../../COPYING), and each source file carries `SPDX-License-Identifier: GPL-3.0-or-later`. The "or later" form
matches hibr's own.

## Consequences

A GPL module loaded by an LGPL host is fine; the combination is GPL. The vendored libraries are compatible: Monocypher is
CC0 or BSD-2, PQClean's ML-KEM `clean` is public domain. The future Android app inherits GPL-3.0, which matters if it is
ever distributed through an app store.

---

[← decisions](README.md)
