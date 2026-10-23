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

- **A full number is a code and a local number:** `+0731042 312-884-076`. The local part is what exists today (9 digits, 8
  random and a Damm check digit, `xxx-xxx-xxx`) and is a separate field; `NUMLEN` does not grow. On your own switchboard you
  dial the local part alone, like a local call without the area code. A number with another switchboard's code is refused
  today with a plain message; calling across switchboards is M9.
- **The identity is the switchboard's key, not its code.** What the wire and the saved state carry is the 32-byte key id of the
  number's switchboard plus the local number: each pin and each line member names its switchboard. A code is only how a
  person writes and dials one, so a code may change (derived, then assigned) without touching a pin or a line.
- **Every switchboard has two codes, both valid for good, and they can never be equal:**
  - the **derived code**: `0` and six digits from a hash of its key id (`+0731042 312-884-076`). It needs no registry, no network
    and no permission: any switchboard has one from its first minute, which is what keeps the numbering open. A leading `0` is
    what marks a derived code, and no assigned code ever begins with `0`, so the two kinds are disjoint by prefix, not by
    a size limit, and stay disjoint whatever length assigned codes grow to. There are a million derived codes; with 100
    switchboards in one world two share one with probability about 0.5%, with 1,000 about 40%. A node that meets two
    switchboards with one derived code refuses the second and says so, rather than guess.
  - the **assigned code**, from a registry: short and memorable, the next free one, one per switchboard key, not chosen. A
    person is shown the assigned code when there is one.
- **The plan is prefix-free, and that is what lets it grow.** No assigned code is the beginning of another, as in the
  telephone numbering plan, so a dialed code can be read without a separator (`+1312884076` and `+1 312-884-076` are the same
  call) and a longer length can be opened at any time without renumbering anyone. The rules:
  - `0` is derived codes. `1` is a single-digit code, **proposed for the default public switchboard** (`+1`, as North America
    did); no further one-digit code is ever assigned without a recorded decision, because each takes a tenth of the plan
    for good. `8` and `9` are held back whole, reserved and unassigned, so the plan keeps room for something nobody has
    thought of. `2` to `7` are for assignment.
  - Assignment is shortest first, first come, one at a time, and **at each length only the lower half of the prefixes still free is
    handed out; the upper half is kept to open the next length.** Two-digit codes run `20` to `49` (30 codes), keeping `50` to `79`;
    three-digit codes are built on those (`500` to `649`, 150 codes, keeping the rest), and so on: each length gives about five
    times as many codes as the last, and the space does not run out.
  - Reserved by prefix, never assigned and never the start of an assigned code: `555` (tests and documentation). The
    emergency lookalikes `911`, `112` and `999` fall under the held-back `9` and under `+1`, and so cannot exist.
  - An assigned code carries no check digit; it travels as an optional part of a message (`NUMBER` tells a node its
    switchboard's code), and the 9-digit local number is a fixed field everywhere, as before.
- **The registry is a switchboard.** A switchboard, on its first start, registers with the project's registry over the same
  authenticated handshake a node uses and is given its code the way a node is given a number: first come, bound to its key,
  persisted, refused if taken. Theft is solved by the handshake (a code cannot be claimed without the key), overlap by the
  registry's uniqueness, and "automatic" is `REGISTER` and `NUMBER`. There is no second protocol. A private or air-gapped
  switchboard names no registry and keeps its derived code. The registry's own address is, like the default public switchboard,
  not yet supplied: until it is, no switchboard is assigned a short code *(planned: M3)*. A code the registry gives is
  also announced to the switchboard's nodes in `NUMBER`, so they can show it.
- **Trust.** A node pins its own switchboard by its full address (`lw1…`) and never takes the registry's word about it. The
  registry can lie only about *other* switchboards' codes, as a switchboard can lie on a first call, and that is covered
  in the threat model.
- **The check digit covers the local part only:** a mistyped code reaches a different switchboard, which will almost always
  answer "unknown number".
- **Display:** `+` code, a space, the local part grouped by threes, in monospace ([BRAND](../BRAND.md)). The space is for reading; a
  dialed number may leave out any separator.

*Amended after review:* the first version made assigned codes "1 to 99999" and derived ones "100000 and up", which stops being
true the day an assigned code has six digits, and left the plan without a rule for growing. Prefix-free allocation fixes both.

## Consequences

Amends [0010](0010-numbers-are-allocated-and-bound-to-keys.md) (a number is unique within a switchboard, so a full number is
unique wherever its switchboard is) and [0011](0011-one-switchboard-and-one-call-at-a-time.md) (federation now has a place to put a
switchboard). Member records grow by 32 bytes each; a line of 16 is still well within a message. What is built: the derived
code, the full number everywhere it is shown and typed, pins and line members that name their switchboard. What is not: the
registry and assigned codes *(planned: M3)* (a gate on the first public switchboard, M4), and federation, redundancy and
the registry's directory entry for a switchboard's listen address *(planned: M9)*.

---

[← decisions](README.md)
