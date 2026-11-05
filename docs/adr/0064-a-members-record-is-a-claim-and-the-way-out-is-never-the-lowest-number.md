# 0064 — A member's record is a claim, and the way out is never "the lowest number"

Status: accepted; built in 0.5.50. The first half of what the 0.5.48 read left open (ADR 0063).

## Context

A line's member list is carried between members: each record has a number, a key id, a signing key and a role, and any member can send a record for any other. Nothing tied the number or the
signing key in a record to the key id, so:

1. **A forged signing key.** A member could write another member's record with a signing key of its own. When that member later asked to evict, or was checked, the forged key was the one
   believed: a later member could be locked out, or a forged eviction accepted.
2. **A forged number, and the exit.** The exit was "the one named, else the only one, else the lowest number". Any member can offer itself, and can write itself a low number, so the choice
   belonged to whoever made it.
3. **A mesh call that answers with another key.** A member dialled by number could be answered by a different key under that number.
4. **HELLO amplification.** A 10-byte request for a bundle was answered with 1251 bytes: 125 times, to whatever source address the request claimed.

## Decision

1. **A key we hold the bundle of is the bundle's.** `mem_add` takes the signing key from the member's own bundle when it is connected to us; `line_call_up` writes it over the record when a session comes up; an eviction
   from a member is checked against the bundle of the session it came on, never the record. A record is believed only for a member we have never met, and is corrected when we do.
2. **A number is one key's.** `line_number_is` says whether a number on the line is this key's, another's, or nobody's. A mesh call whose answering key differs from the record under that number is
   dropped, with a `member-mismatch` event, and the peer is let go.
3. **No choice by number order.** The exit is the one named in `exit_through` if it offers *and* its number is pinned to its key on this node (`pin_check`), else the only one that offers; when
   several offer and none is named, none is used and the node says so in the `exit` event and the pane (*N members share their internet and none is named*). The status asks this rule afresh, so what
   it says is what the rule gives now.
4. **A request is as long as its answer.** A HELLO request is padded to the size of the bundle answer and a shorter one is not answered, so a forged source address gains nothing: the reflector
   returns no more than it was sent. The existing per-source and global rates remain.

## Not done

- **Admits never expire**, and an operator's revocations are a bounded list. Needs an epoch or re-issue design; written in [SECURITY](../SECURITY.md).
- **A handshake's time is not checked against the clock.** Kept monotonic-only, as WireGuard's is: a clock the attacker can move is a worse thing to trust. Replays are bounded by the session index and the
  per-peer timestamp.
- **The control socket** is made by path and its mode set after it exists; the folder is the owner's. Low; recorded.

## Consequences

With two members sharing and none named, a node uses no way out and says why: the person names one. That is the cost of not letting a member decide. A node with one sharing member works as before.

Tested: `tests/switch.sh` (a forged record's key is the bundle's; a session coming up corrects it; a number is one key's; the exit with two offers is none; a named exit unpinned or pinned
to another key is none, pinned to its own is chosen), `tests/exit.sh` (two members sharing and none named: none used; a short HELLO is unanswered and a full one is answered with no more than it was sent).
