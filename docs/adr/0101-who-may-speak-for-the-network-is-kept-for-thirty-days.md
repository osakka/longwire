# 0101 — Who may speak for the network is kept for thirty days

Status: accepted; built in 0.5.77. It supersedes [ADR 0089](0089-who-may-speak-for-the-network-is-not-kept-across-a-restart.md) for a desktop; a phone's app keeps its own list ([ADR 0094](0094-a-phone-learns-the-other-box-of-a-pair.md)).

## What went wrong

The owner's desktop connected to the pair vpn-1 and vpn-2 and got only a 169.254 address, while a static address worked. `longwire watch` said it: *107-958-349 answered as DHCP for this machine's network and was refused: it is on the line, but you did not ask for it.* The DHCP guard ([ADR 0080](0080-only-a-member-we-asked-for-may-speak-for-the-network.md)) lets through a DHCP answer only from a member the node asked for or one such a member vouches for. The numbers asked for and the vouches were in memory only (ADR 0089 chose that), and a desktop's profile has no number to dial: it comes up with the line it saved and calls its members. After any restart it had asked for nobody, so **it refused every DHCP answer until a person dialled by hand**, and when vpn-2 was the active box and vpn-1 had not connected, nothing could vouch. ADR 0089 reasoned that a restart is followed by the person's dial; for a desktop that is not so.

## Decision

1. **What a person chose of the line is kept** in the state directory (`trust`, mode 0600) **for thirty days**: the numbers dialled (or named to be joined to) and the boxes a member asked for has vouched for, and **only those that are members of this node's line**: a number dialled and never joined is a stranger's and is not kept. An entry is refreshed when it is dialled or vouched for again and expires thirty days after. A damaged or older entry is not loaded. A switchboard keeps none; a phone's engine (embedded) keeps none, its app has its own list.
2. **The founder of the line is one the person chose by joining it:** a node treats the member that founded its line as asked for, so its vouch (for the other box of a pair) is believed, with nothing dialled.
3. **Nothing else changes:** a member that was not asked for, not vouched for and not named is refused as before.

## Consequences

- A desktop that was ever on the pair gets its lease through either box after a restart, with nothing dialled and nothing configured, and does not wait for the box it first met.
- The trust given to a number lasts thirty days from the last time it was dialled or vouched for, not for ever; deleting `trust` in the state directory and restarting the node forgets it.
- It is a loosening of ADR 0089: a box that was vouched for keeps the power to answer DHCP for this machine for thirty days after it has gone from the line. Only a box that is a member of the line, and was vouched for by a member the person chose, has it.
- Tested (`tests/trust.sh`): what is kept and what is not, the mode, the thirty days, a damaged file, and the founder.
