# 0024 — A full number is a switchboard code and a local number

Status: accepted

## Context

A number today is 9 digits that mean something only to the one switchboard that issued them
([0010](0010-numbers-are-allocated-and-bound-to-keys.md), [0011](0011-one-switchboard-and-one-call-at-a-time.md)). Redundancy
and federation (M9) put several switchboards in one world, and each wants a name in the numbering, the way an exchange has an
area code. The format reaches the wire (member records), saved state (the pins, the saved line) and every place a number is
shown. Nothing is deployed yet, so it could change for free; the day the default public switchboard issues a number, it
cannot. The goals are numbers that are familiar, memorable and extensible; a numbering plan as open as possible; and no
overlaps, no squatting, and no theft.

## Decision

- **A full number is a code and a local number:** `+731042 312-884-076`. The local part is what exists today (9 digits, 8
  random and a Damm check digit, `xxx-xxx-xxx`) and is a separate field; `NUMLEN` does not grow. On your own switchboard you
  dial the local part alone, like a local call without the area code. A number with another switchboard's code is refused
  today with a plain message; calling across switchboards is M9.
- **The identity is the switchboard's key, not its code.** What the wire and the saved state carry is the 32-byte key id of the
  number's switchboard plus the local number: each pin and each line member names its switchboard. A code is only how a
  person writes and dials one, so a code may change (derived, then assigned) without touching a pin or a line.
- **Every switchboard has two codes, both valid for good:**
  - the **derived code**, six digits (100000 to 999999) from a hash of its key id. It needs no registry, no network and no
    permission: any switchboard has one from its first minute, which is what keeps the numbering open. Six digits are about
    900,000 codes; with 100 switchboards in one world two share a code with probability about 0.5%, with 1,000 about
    40%. A node that meets two switchboards with one derived code refuses the second and says so, rather than guess.
  - the **assigned code**, 1 to 99999, from a registry of switchboards: short and memorable, the next free number, one per
    switchboard key, not chosen. A person is shown the assigned code when there is one. The two ranges cannot overlap, so
    a dialed code is unambiguous. `+1` is proposed for the default public switchboard. 0, 555 (tests and documentation) and the
    emergency lookalikes (911, 112, 999) are reserved and never assigned.
- **The registry is a switchboard.** A switchboard, on its first start, registers with the project's registry over the same
  authenticated handshake a node uses and is given its code the way a node is given a number: first come, bound to its key,
  persisted, refused if taken. Theft is solved by the handshake (a code cannot be claimed without the key), overlap by the
  registry's uniqueness, and "automatic" is `REGISTER` and `NUMBER`. There is no second protocol. A private or air-gapped
  switchboard names no registry and keeps its derived code. The registry's own address is, like the default public switchboard,
  not yet supplied: until it is, no switchboard is assigned a short code *(planned: M3)*.
- **Trust.** A node pins its own switchboard by its full address (`lw1…`) and never takes the registry's word about it. The
  registry can lie only about *other* switchboards' codes, as a switchboard can lie on a first call, and that is covered
  in the threat model.
- **The check digit covers the local part only:** a mistyped code reaches a different switchboard, which will almost always
  answer "unknown number".
- **Display:** `+` code, a space, the local part grouped by threes, in monospace ([BRAND](../BRAND.md)).

## Consequences

Amends [0010](0010-numbers-are-allocated-and-bound-to-keys.md) (a number is unique within a switchboard, so a full number is
unique wherever its switchboard is) and [0011](0011-one-switchboard-and-one-call-at-a-time.md) (federation now has a place to put a
switchboard). Member records grow by 32 bytes each; a line of 16 is still well within a message. What is built: the derived
code, the full number everywhere it is shown and typed, pins and line members that name their switchboard. What is not: the
registry and assigned codes *(planned: M3)* (a gate on the first public switchboard, M4), and federation, redundancy and
the registry's directory entry for a switchboard's listen address *(planned: M9)*.

---

[← decisions](README.md)
