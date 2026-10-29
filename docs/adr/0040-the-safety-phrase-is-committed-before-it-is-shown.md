# 0040 — The safety phrase is committed to before it is shown

Status: accepted; stage 1 built in 0.5.21 (the commitment, the exchange, the eight words, `--require-sas`). Stage 2, holding an unconfirmed call off the line *(planned: M8)*, is not built.

## Context

The safety phrase is the one check a person can make that depends on neither the software nor the switchboard. It was six words (48 bits) from a hash of the two key ids ([SECURITY](../SECURITY.md#the-safety-phrase-honestly)). A switchboard that is itself the attacker gives each node a substitute
key of its own choosing, so it can **grind**: try key pairs offline until the phrase the caller will see with its key equals the phrase the callee will see with the other, a birthday search of about 2^24 pairs. A longer phrase helps only by the square root, and all of
it is offline work that costs the attacker nothing visible. The fix is the one ZRTP uses for the same problem: make the attacker **commit before it knows what to match**, so that each attempt is a single, visible, online try.

## Decision

1. **The exchange runs inside the direct session, not through the switchboard.** The first design carried the commitment and the nonce in the dial, the ring, the answer and the go. It was dropped: the ring of a mesh call (with the line's admit) is already near the
   size a control message may have, and the switchboard would have had to carry more of a call it should know less of. Inside the session, nothing about the switchboard changes. Four small messages (`M_SAS`, kind 0 to 3), each repeated until answered:
   the caller sends `commit = H(na, its key id)`; the callee answers with a random `nb`; the caller reveals `na`; the callee checks it against the commit and acknowledges.
2. **The phrase** is `H("lw-sas", na, nb, caller key id, callee key id, bind)` as **eight words** (64 bits), where `bind` is a hash of the session's two keys in an order both ends agree on: the same at the two ends of one session and different on the two sessions of a node in
   the middle. The commit is bound to the caller's key id, so a commitment cannot be passed on to a different callee that sees another key.
3. **Why it holds.** A node in the middle must commit toward each end before it knows the nonce of the other, and the phrase on each leg is fixed only when the last number is revealed. It cannot search for a collision offline: one attempt is one call, succeeding with probability
   about 2^-64.
4. **A node that does not answer within five seconds is taken to be older**, and the old six-word phrase is shown, marked `legacy` (events, status, the scripts' wording). Something on the path that drops the messages can force that, so `--require-sas` refuses such a call; a mesh call to a fellow member
   (admitted by the line's signatures) is exempt. A callee that was committed to but is not given the reveal within 20 seconds ends the call.
5. **The ring shows no phrase.** It does not exist yet. The scripts, the pane, the menu bar and `watch` say it comes once connected, and print it on the `sas` event.

## Consequences

The 2^24 search is closed, and the cost is four messages and a few hundred milliseconds. Not closed, and said so in [SECURITY](../SECURITY.md#the-safety-phrase-honestly): the call is on the line while the words are compared, because pickup still admits (stage 2 would hold an unconfirmed
first call off the line until the callee confirms); and a downgrade to the old phrase by an on-path attacker, unless `--require-sas`. Relayed calls between line members keep the old phrase (their admission is the line's signatures). Tested one end at a time with the engine under TCC's
bounds checker (`tests/sas.sh`: commit, repeat, right reveal, wrong reveal, a commitment for another caller, silence, `--require-sas`, two sessions) and end to end in the call tests. Not model-checked: that is M8's Tamarin or ProVerif model of the handshake, which should include this.
