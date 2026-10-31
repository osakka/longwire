# 0048 — A silent switchboard is said so

Status: accepted; built in 0.5.28. Follows [0043](0043-strikes-and-bans-count-only-what-cannot-be-faked.md).

## Context

A site node restarted several times sat at "waiting for a number" for good, and its log said nothing: not an attempt, not an error. Finding out why took a person with a
packet capture. A handshake that gets no answer was retried in silence, and [0043](0043-strikes-and-bans-count-only-what-cannot-be-faked.md) makes a banned key's handshake get exactly that: no answer, on purpose, so that a ban
tells an attacker nothing. On the switchboard, a ban was visible only to someone who asked (`bans list`), and that command did not find the service's control socket by itself.

The silence towards a banned key stays: an answer would be an oracle. What changes is that each side says what it can.

## Decision

1. **The node says when its switchboard does not answer.** With no session after 12 seconds, it asks the echo port of the switchboard's host once (the switchboard's port + 1: answered
   for anyone, with no handshake and no ban, which is what NAT detection already uses). At 15 seconds it tells the `switchboard-silent` event, with the address and what the host did:
   - `answers`: the path is open both ways and the switchboard is ignoring this node. Its key may be banned there, or the key after `/` in the switchboard address is not the switchboard's.
   - `silent`: nothing comes back from the host. A firewall on the way (UDP), or no switchboard running.
   The node script writes it as a sentence in the log and in `longwire watch`, and `longwire show` adds a line for as long as it lasts (from `sb_quiet` and `sb_host` in the status).
2. **The switchboard writes each ban to its log** (the journal, under the service): the key's first 16 hex digits, how long, which ban in a row, why, and the command that lifts it. A net ban
   names the network. No address of a node is written.
3. **An honest node no longer earns strikes for being patient.** A node repeated an unanswered registration every 2 seconds, and the switchboard struck a second registration within 5 seconds of
   the first: a slow or lost answer made a good node strike itself. The switchboard now ignores a registration repeated within 5 seconds, without a strike (a flood is still bounded by the
   message budget), and the node waits 6 seconds before it asks again.
4. **`longwire-switchboard bans`** finds the switchboard as the Switchboard application does: the one run by hand as you, or else the service's (`/run/longwire-switchboard/ctl`).

## Consequences

The case above is two lines, one at each end, instead of a packet capture. Tested on a real switchboard (`tests/ban2.sh`): a node given the wrong switchboard key is told the host answers and the
switchboard is ignoring it, and `show` says so; a node pointed at nothing is told nothing comes back; each ban is in the switchboard's log.
