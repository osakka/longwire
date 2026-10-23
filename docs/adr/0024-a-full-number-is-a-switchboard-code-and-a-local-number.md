# 0024 — A full number is a switchboard code and a local number

Status: proposed

## Context

A number today is 9 digits that mean something only to the one switchboard that issued them
([0010](0010-numbers-are-allocated-and-bound-to-keys.md), [0011](0011-one-switchboard-and-one-call-at-a-time.md)). Redundancy
and federation (M9) put several switchboards in one world, and an operator who runs one wants it to have a name in the
numbering, the way an exchange has an area code. The format reaches the wire (`DIAL`, `RING`, `GO`, `REJECTED`, `NUMBER`, the
line's member records), saved state (`numbers.log`, the pins, the saved line), and every place `NUMLEN` is used. Nothing is
deployed yet, so it can change today for free; the day the default public switchboard issues a number, it cannot.

## Decision

- **A full number is a code and a local number:** `+731042 312-884-076`. The local part is exactly what exists today (9 digits,
  8 random and a Damm check digit, `xxx-xxx-xxx`). It is a separate field: `NUMLEN` does not grow.
- **Dialing your own switchboard's numbers needs no code,** like dialing a local call without the area code. A number on
  another switchboard is dialed in full.
- **The code names a switchboard, and is derived from its key** until a registry exists: six decimal digits taken from the
  hash of the switchboard's key id, so every switchboard has a code from its first minute with no configuration and no one to
  ask. Six digits are about a million codes: with 100 switchboards in one world the chance that any two share a code is about
  0.5%, with 1,000 about 40%, so a node that meets two switchboards with one code refuses the second (and says so) rather
  than guess. The code is never trusted on its own: a node pins a switchboard by its full address (`lw1…`), and the code is
  how a *number* finds it.
- **Short, memorable codes are an alias,** handed out later by a registry of switchboards (like country and area codes), which
  an operator joins by proving control of the switchboard's key. The derived code stays valid underneath, so an alias never
  strands a number. One code field, no area-plus-country tiers: a registry can later give out short codes that no six-digit
  derived code can equal (a derived code always begins with a digit 1 to 9, short aliases are reserved to have fewer digits).
  `+555` is set aside for tests and documentation.
- **A pin, a line member and a status line carry the full number** from the start, even with one switchboard, so nothing
  collides the day there are two. On the wire a full number is the 6-digit code (3 bytes, binary) followed by the 9-digit local
  part.
- **The check digit covers the local part only.** A mistyped code reaches a different switchboard, which will almost always
  answer "unknown number".
- **Display:** a full number is `+` code, a space, the local part grouped by threes, in monospace
  ([BRAND](../BRAND.md)).

## Consequences

Amends [0010](0010-numbers-are-allocated-and-bound-to-keys.md) (a number is unique within a code, so a full number is unique
everywhere a code is) and [0011](0011-one-switchboard-and-one-call-at-a-time.md) (federation now has a place to put a
switchboard). Federation, redundancy and the registry stay in M9; this record fixes only the shape so they do not require a
second migration. Built before the first public switchboard goes live (M4).

---

[← decisions](README.md)
