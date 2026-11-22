# 0111 — A member sees a pair as one

Status: accepted; built in 0.5.80. The owner could not see which boxes were a pair, and asked whether a pair should be one number. It stays two numbers (a number is one key; a pair is active and standby, never both forwarding, ADR 0083), and is shown as one.

## Decision

- A box of a pair says which it is, active or standby, in the message it already sends its members about its segment (`M_SITE`, one byte more after the key ids). A member keeps it for display only; it decides nothing.
- `longwire status` gives each peer a `site` field (`active`, `standby`, or empty when it has not said so in 15 s). `longwire show` prints one line for the pair (`pair  402-339-003 active · 259-619-514 standby`) and the role beside each box; the Control Panel shows the role on each box's row.
- Dialling either box is enough: a member joins the line and holds a session with both.

## Consequences

- Tested (`tests/pairauto.sh`): a desktop dialled into a pair sees both boxes with their roles, and `longwire show` gives the pair in one line.
