# 0103 — A founder makes its own admit again

Status: accepted; built in 0.5.78. Found from the owner's desktop, which said (ADR 0100) that vpn-1's call was *on the line, but not let in by itself: the admit is for another node*.

## What was wrong

vpn-1 founded the line, and the admit it sends with its calls, the proof that the line let it in, is the one in its saved line state. For the founder that admit is a self-admit, made out for its own key. The one vpn-1 sent was made out for another node's key, so every member that checked it refused it, asked the person to pick up, and waited. The mesh builds only from calls that are let in by themselves, so vpn-1's calls to a desktop hung until the desktop's own call to vpn-1 got through (about a minute), and a DHCP guard that trusts "the founder" through its vouch had nothing to go on in the meantime. How the saved admit came to be another node's is not known; nothing in the code now can write it, and it may have been written by a release before it was fixed. A test that makes the same state (`tests/admitwhy.sh`) shows the mismatch is not tolerated anywhere else.

## Decision

1. **At start, an unmanaged line whose founder is this node checks its own admit** (the same check as every other admit, ADR 0100). If it does not verify, the node makes its self-admit again, saves the line, and says so in an event, `admit-repaired`, with the reason.
2. **A member that is not the founder cannot make its own** (the founder or the operator signs it): it says `admit-bad` with the reason, and leaves it, so the person can see what is wrong and re-join.
3. **A line with an operator is not touched:** its admits are signed by the operator.

## Consequences

- vpn-1 and any founder with the same fault mend themselves on the next start (the next release the boxes roll onto), with no action from anyone.
- Nothing else about who is let in changes.
- Not known: how the wrong admit got there; if it comes back, the `admit-repaired` event says that it did.
