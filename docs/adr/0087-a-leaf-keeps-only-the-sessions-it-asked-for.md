# 0087 — A leaf keeps only the sessions it asked for

Status: accepted; built in 0.5.71; amended by [ADR 0092](0092-a-leaf-keeps-the-other-box-of-a-pair-too.md) (a leaf keeps the other box of a pair too). A phone in a line of five called every member it had no session with, and answered every member that called: a radio and a battery spent on peers the phone never wanted, and a switchboard told who is in the line. (Ticket LW-3.)

## Context

A member of a line meshes: it dials every member it lacks a session with, so the line heals, and it lets in any member that dials it ([ADR 0085](0085-the-mesh-calls-an-absent-member-less-and-less-often.md) made that calmer, not absent). For a laptop that is right. A phone is a member of a LAN to reach what is on it (the router, the NAS) through the numbers its owner listed; it needs no session with the other members, and each one costs its battery.

## Decision

1. **A leaf is a node started with `leaf` set** (`longwire up --leaf on`; the phone's engine always). It is not a different protocol: the other members see an ordinary member.
2. **It does not dial the line.** The mesh timer returns before it calls any member, and does not ask a member to relay for it. It still learns the line, so it knows who is on it.
3. **It lets in only the numbers it asked for:** the ones a person dialled, and the ones named to it with `asked` (the phone's list). A ring from any other member of the line is refused at once, whatever the script's policy would have said, and the script is not told of it. A ring from a stranger is refused as before.
4. **What is unchanged:** its lease, its DHCP guard, its failover (a dial to a backup number is a number asked for), and the calls the app makes.

## Consequences

- A phone in a line of five holds sessions with the numbers in its list and no others (`tests/leafline.sh`: the founder's only).
- The other members still ring the leaf, with the backoff of ADR 0085 (four calls an hour each), and it refuses each ring: that cost to them is not removed. Telling them to leave it alone is the keep-quiet message *(planned)*, ticket LW-13.
- The list the phone is joined to holds at most eight numbers at a time in the engine (`asked`); the app's list is shorter.
- Not tried on a phone: the battery saving this should bring is not measured.
