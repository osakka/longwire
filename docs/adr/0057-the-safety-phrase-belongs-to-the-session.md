# 0057 — The safety phrase belongs to the session; the lower key id commits first

Status: accepted; built in 0.5.41. Amends [0040](0040-the-safety-phrase-is-committed-before-it-is-shown.md) (who commits) and [0055](0055-no-legacy.md) (which calls have a phrase).

## Context

The committed exchange (0040) was started for each call when it came up, with the call's direction as the roles and the call's kind deciding whether a phrase was wanted: a
mesh call to a fellow member, or a call from one, was admitted by the line and had none (0055). But a peer and its session are one thing, and the calls that lead to it are
several, seen differently at the two ends:

- two members that dial each other at once (a person's call, and the line's own redial) share one session through two calls; each call restarted the exchange, so each end
  kept whichever came last, and the ends could both wait to be committed to, or both commit;
- a call a person makes to a member whose session dropped is a person's call at one end and admitted by the line at the other: one end waited for a phrase the other had
  decided was not needed.

Either way the end that wanted a phrase heard nothing and, since 0.5.38 (no fallback), ended the call after five seconds. It showed in `tests/m1.sh` as a fourth call that
ended itself, and it would end real calls. The cause is that the exchange had a source of truth per call and per end, where both ends must decide alike.

## Decision

1. **One exchange per peer, started in one place.** A peer reached through the switchboard (one with a number: direct, relayed or mesh) starts its exchange when its session
   is up, once, in the timers; no call path starts it. A peer named by its bundle (`--peer`) has none (`sas`: `none`).
2. **Roles from the two key ids.** The end with the lower key id commits first; both ends know both key ids, so they decide alike, whichever end dialled and however many
   calls crossed. Each committing end still commits before it learns the other's nonce, so a node in the middle still has one try per session (about 2^-64).
3. **Every such session has a phrase**, members' and relayed ones included: there is no "admitted by the line, no phrase" path that one end can take and the other not.
   An end that keeps talking and never takes part for 10 s (retried every second) ends the call. Keeps talking means heard from in the second half of
   that wait: one that spoke just as the session came up and then went silent has gone, and is left to the dead-peer timer (call-lost). Before this, a node
   killed a moment after its call connected was reported as refusing the phrase rather than as lost.
4. **What is shown stays local.** The `sas` event and each peer in the status carry `mesh`: true when no call to that peer was made or answered by a person here. The
   desktop notes a phrase only when `mesh` is false; the pane and the menu list every phrase, and `longwire verify` checks any of them.

## Consequences

A member that the line brings has a phrase like anyone else, which a person may compare; nothing interrupts for it. Status `sas` is `agreed`, `pending` or `none` (`line` is
gone). Every node updates together (0055). Tested: the roles, the one start and crossed calls in `tests/sas.sh`; a relayed session agreeing the same words at both ends in
`tests/m3b.sh`; `tests/m1.sh`, where the failure showed, run repeatedly.
