# 0065 — An admit has an end, and a revocation does not outlive it

Status: accepted; built in 0.5.51. The second half of what the 0.5.48 read left open (ADR 0063, ADR 0064).

## Context

On a line with an operator ([ADR 0026](0026-a-line-may-have-an-operator.md)) the operator signs an *admit* for each member and an *eviction* to remove one. Two things are wrong:

1. **An admit never ends.** It is a signature over the line, the key and the time it was made. A laptop that is lost, a person who leaves, a key that was copied: the admit is good for ever
   unless the operator remembers to evict, and every node remembers to keep that eviction.
2. **A revocation is a bounded list.** A node keeps the last 32 evictions (`n->line.rev`, 32 entries, the oldest dropped to make room). An eviction is what makes an old admit useless
   (an admit older than the eviction of its key cannot bring it back). Drop the eviction and the old admit works again: an operator who evicts 33 keys has un-evicted the first. The
   operator is not told, and nothing in the node's status says the list is full.

Both come from the same fact: nothing says how long a thing the operator signed is meant to last. A list of what is revoked has to be as long as the longest thing that can be revoked.

This applies to lines with an operator only. On a line without one ([0022](0022-lines-form-by-vouching.md)) every member vouches for and evicts every other; that line's trust is "all of us
trust all of us", there is nobody to renew anything, and an expiry would only lock out the family at a bad time. It is not changed, and the documents say so.

## Decision

1. **An operator's admit carries its end.** The admit grows from 145 to 153 bytes: `line_id 8 | key_id 32 | role 1 | signer 32 | issued 8 | expires 8 | sig 64`, the signature over the 89 bytes
   before it. No older layout is read (no legacy). `expires` must be after `issued` and no more than **`ADMIT_MAX_LIFE` = 400 days** after it: a longer one is not an admit. The operator's
   tool takes `--days N` (default 90, at most 400) and prints the date.
2. **A node holds a member to the admit it showed.** Members show their admit to each other (`ADMIT`, inside the session) when a session comes up and when it is renewed; a node holds
   each to the end of what it showed, checked every second against its own clock. A member whose admit has ended, or was made before a roll, or was never shown within 20 s of the session
   (the session is ended and made again), is off the line: its sessions end, its record is removed, a `member-expired` event says who; a call from it is refused (*ask its operator for a new
   one*). A node whose own admit ends tells its owner first: the status carries `admit_expires` and `admit_days`, an event `admit-expiring` fires 14 days, 3 days and 1 day before, the pane
   shows it, and when it has ended the node goes quiet (`admit-ended`): every session ends and it makes no calls, but it keeps the line, until a renewed admit comes. Renewal is the operator
   signing a new admit for the same key; the node takes it with `longwire renew ADMIT` while running (or `up --admit` at start), tells the members it is linked to, and goes on. A renewal
   needs no re-vouching and no new key.
3. **A revocation is kept exactly as long as it can matter.** An eviction of a key is useful until the latest admit that could have been signed for it has ended: `issued + ADMIT_MAX_LIFE`.
   The node keeps an eviction until then, plus the clock skew it already allows, and drops it after, never before and never to make room. The list is no longer 32 but **256** entries, stored
   with the line, and a node never drops an eviction that can still matter.
4. **A full list says so and fails closed.** If 256 evictions that can still matter are held, the node takes no new admit from anyone (a new member cannot join until room is made by time or by the
   operator's *roll*, below) and its status (`revocations_full`), an event (`revocations-full`) and the pane say so: the operator is told by the thing that is wrong, not by an old key coming back.
   On a line without an operator nothing is ever dropped either (there are no ends to count on), and it is full the same way.
5. **The operator can roll the line.** A signed *roll* record (`line_id | epoch 8 | time 8 | signer 32 | sig 64`) raises the line's epoch. Admits carry no epoch; a roll says "no admit
   issued before `time` is good any more", which is the one eviction of every older key at once. A node that holds a roll drops every eviction older than it (they are covered), and members
   the operator still wants get new admits. It is the one tool for a list that is full of real revocations and for "every key before today is suspect". It is stored with the line and
   passed to a member that links later, as evictions are.
6. **What stays a clock assumption.** This is the first thing in longwire that trusts a wall clock. The assumption, written down: a node's clock is within a day of true time (the same
   skew `EVICT_SKEW` allows an eviction). A node whose clock is set back keeps an ended admit alive on *that node* until it is corrected; one set forward drops members early and refuses
   calls. Neither lets a stranger in: the admit's signature, the line and the key still have to be right. An attacker who can set a victim's clock can make it accept what has ended, which is
   the reason a comparison of clocks between peers is wanted *(planned: M8)*; the handshake's time is still not checked against the clock the handshake against the clock (WireGuard's choice,
   kept).

## What this does not do

- It does not make a lost key safe at once: it makes it safe within `ADMIT_MAX_LIFE` even if nobody evicts it, and at once only when the operator evicts. Choose `--days` for what a lost laptop should cost.
- It does not apply to a line without an operator.
- It does not protect the operator's own key. That is [0026](0026-a-line-may-have-an-operator.md)'s single point of trust.
- It does not give the operator a way to be reminded to renew: the node tells the member, and the member tells the operator. There is no list of expiries on the operator's side.

## Consequences

- Operators must renew. A member that is not renewed leaves the line on a known date, with 14, 3 and 1 day of warning: that is the point, and it is the cost.
- Every admit already made is void when 0.5.51 is installed (the layout changes, and no legacy is read): operators sign new admits with `--days` once. This is a deliberate break, taken now, while
  the number of operator lines is small.
- The revocation list is bounded and cannot silently lose an entry.

## Tests

`tests/switch.sh` (an ended admit is refused; one dated beyond 400 days, in the future, or ending when it begins is not an admit; a roll voids older admits; the founder's own admit has no end;
a member's shown admit is held to; our own ending makes the node quiet and a renewal brings it back; 256 evictions are held and not one more, none dropped for room; a full list refuses new
admits; they go after the longest an admit can last; a roll covers them), `tests/expiry.sh` (real nodes: an admit ending in 30 s is renewed and the session goes on; one not renewed takes the
member off the line and the node is quiet, and a new one brings it back; the operator's roll takes every older admit off at once and only one made after it is taken), `tests/fuzz.c` (the new
layout), `tests/docs.py` (the commands, events and layout).
