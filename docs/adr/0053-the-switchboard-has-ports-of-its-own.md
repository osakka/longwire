# 0053 — The switchboard has ports of its own

Status: accepted; built in 0.5.36. Follows [0048](0048-a-silent-switchboard-is-said-so.md) and [0052](0052-the-strong-phrase-is-the-default-the-echo-has-a-budget-tokens-are-128-bits.md).

## Context

A switchboard listened on UDP 1877 by default, and answered `ECHO` on the port above, 1878. Both numbers are assigned by IANA to other services (`hp-webqosdb` and `drmsmc`), and two adjacent
ports were an easy fingerprint. The owner asked for ports of longwire's own. A port is not a security control (a scanner finds a UDP service on any number; the handshake guards it); this is
about not squatting on assigned numbers, and about a pair that does not read as one.

## Decision

1. **The default listen port is 6741**, and **the echo port is the listen port + 2747**: 9488 by default. Both were unassigned for TCP and UDP in IANA's registry on 2026-11-02 (ranges 6717–6766
   and 9451–9499), and are away from common unofficial uses. The offset is fixed so a node can find the echo port before it has a session, as it must (the silent-switchboard probe of
   [0048](0048-a-silent-switchboard-is-said-so.md) runs before any session). A listen port too high for the offset uses the listen port − 2747.
2. **Compatibility both ways.** A switchboard still answers `ECHO` on its port + 1 too, for nodes before 0.5.36, until `--echo-legacy off`; a node asks both ports, and the first answer counts, so
   it measures against a switchboard of any version. A switchboard started with `--listen 1877` keeps 1877; its echo port becomes 4624, and 1878 still answers.
3. **Registration with IANA** (`longwire` on 6741/udp, and optionally `longwire-echo` on 9488/udp) is the owner's application to make; until an assignment, these are unassigned ports longwire uses.

## Consequences

A firewall in front of a switchboard opens the listen port and the echo port (6741 and 9488), and the listen port + 1 while older nodes remain. The offset is in the source, so anyone who reads it
can tie the two ports together; the point is only that they are not adjacent. Tested on a real switchboard: ECHO answered on the listen port + 2747 and on + 1 (`tests/ban2.sh`); the NAT and
silent-switchboard checks against both (the suite).
