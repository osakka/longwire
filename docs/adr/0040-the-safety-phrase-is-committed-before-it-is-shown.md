# 0040 — The safety phrase is committed to before it is shown

Status: accepted as a design; not built *(planned: M8)*. Amends nothing yet; replaces the phrase of [PROTOCOL](../PROTOCOL.md) when built.

## Context

The safety phrase is the one check a person can make that depends on neither the software nor the switchboard. Today it is six words (48 bits) from a hash of the two key ids
([SECURITY](../SECURITY.md#the-safety-phrase-honestly)). A switchboard that is itself the attacker gives each node a substitute key of its own choosing, so it can **grind**: try key pairs offline until the phrase
that the caller will see with its key equals the phrase the callee will see with the other, a birthday search of about 2^24 pairs. Making the phrase longer helps only by the square root (eight words: about 2^32), and all of it
is offline work that costs the attacker nothing visible.

The fix is the one ZRTP uses for the same problem: make the attacker **commit before it knows what to match**, so that each attempt is a single, visible, online try.

## Decision

1. **A commitment in the dial.** The caller picks a random 16-byte `nA` and sends `cA = H("lw-sas-commit" ‖ nA ‖ callerKeyId ‖ calleeKeyId)` in the dial. The ring carries `cA`; it carries no phrase.
2. **The callee's nonce in the answer.** On pickup the callee picks `nB` and sends it with its answer. Whoever relays it (the switchboard) sees it, but `nA` is still secret.
3. **The caller reveals.** In `GO` the caller sends `nA`; the callee checks it against `cA` and drops the call if it does not match.
4. **The phrase is derived from both nonces and the session.** `sas = H("lw-sas" ‖ nA ‖ nB ‖ callerKeyId ‖ calleeKeyId ‖ h)`, with `h` the handshake hash of the direct session, shown as **eight words** (64 bits). A node in the middle holds two
   sessions with different `h`, and had to fix its keys and nonce before it learned the other nonce: it can no longer search offline. One attempt wins with probability 2^-64, and the attempt is a call somebody notices.
5. **The check moves from the ring to the admission.** The phrase exists only once the session does, so a first call from a number whose key is not pinned connects into a *held* state: frames are neither forwarded nor delivered, and the line is not joined, until the
   callee confirms the phrase they were read (or `known`/`auto` policy applies, as today). A number whose key is pinned needs no phrase, as now.
6. **Compatibility.** The new fields change the call messages, so a node advertises `proto` when it registers and the switchboard only introduces nodes that share a version; an older node keeps the old phrase against another older node, and is
   told plainly when it cannot call a newer one.

## Consequences

This closes the 2^24 search, replaces "hang up at once if the words differ" with "do not confirm until they match", and costs a held state, two nonces, a registration field and changes to every place that shows a ring (the CLI, the pane, the menu bar and the watch). Because
it changes the wire and the meaning of Pick up, it is a milestone of its own (M8) and wants the protocol review and a Tamarin or ProVerif model of the handshake that M8 already lists. Nothing here is built; until it is, [SECURITY](../SECURITY.md#the-safety-phrase-honestly) describes the phrase as it is.
