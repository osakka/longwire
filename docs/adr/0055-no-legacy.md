# 0055 — No legacy, no backwards compatibility

Status: accepted; built in 0.5.38. Supersedes the compatibility parts of [0040](0040-the-safety-phrase-is-committed-before-it-is-shown.md), [0052](0052-the-strong-phrase-is-the-default-the-echo-has-a-budget-tokens-are-128-bits.md) and [0053](0053-the-switchboard-has-ports-of-its-own.md).

## Context

longwire is young, its nodes are few and all its owner's, and every compatibility path is code that is kept, tested and explained for a version nobody runs. The owner's rule: no legacy
support and no backwards compatibility. Every node and switchboard is updated together.

## Decision

1. **There is one safety phrase**: the committed eight words. The six-word phrase from the two key ids is gone, with `--allow-legacy-phrase`, the `legacy_phrase` profile key and
   `--require-sas` (any of them is now an unknown option). A direct call whose other end does not do the exchange within five seconds is ended, once anything at all has come from that end (one that has sent nothing may be gone, and the dead-peer timer reports a lost call). A mesh call to a fellow member and a relayed
   call between members are admitted by the line's signatures and show no phrase; the status says so (`sas`: `line`).
2. **There is one echo port**: the switchboard's own + 2747. The answer on the port + 1, `--echo-legacy`, and the node's second probe are gone.
3. **Fields kept only for old readers are gone**: `phrase` in the `ring` and `connected` events and in each call of the status (always empty), and `legacy` in the `sas` event, the peers and
   `who` (a peer has `sas`: `agreed`, `pending` or `line`).

## Consequences

A node before 0.5.21 cannot hold a direct call with one after 0.5.38; a node before 0.5.36 gets no answer from a 0.5.38 switchboard's echo port, and reports its NAT as `unknown`. Update
everything at once. Tested: the phrase without a fallback (`tests/sas.sh`), a relayed call with no phrase (`tests/m3b.sh`), the events and the status without the old fields (`tests/m1.sh`,
`tests/pane.sh`), the echo port alone (`tests/ban2.sh`).
