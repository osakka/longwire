# 0071 — A switchboard never starts a handshake to a node

Status: accepted; built in 0.5.57. Found by an independent read of the code, asked for by the owner: nodes lost their registration, and only a restart of the switchboard service brought them back.

## Context

A session is rekeyed by the node that began it. A node rekeys when the session is as old as `--sb-rekey` (600 s); the other end rekeys ten seconds later if the first has not, in case it never does. That
fallback is right between two nodes. A switchboard is the other end for every node, and it **cannot finish a handshake of its own towards a node**: it takes answers from no one but its registry (a
switchboard "asks no one else"), so every answer is dropped as garbage and the handshake stays open for ever, retried every 30 seconds.

A node that rekeys late (its first handshake datagrams were lost, a laptop that slept, a roam, a Wi-Fi that dropped) was enough: the switchboard started its own at 610 seconds. It then also **ignored the
node's later handshakes** whenever its own key id was the lower of the two, by the rule that settles two ends starting at once (the lower key id's handshake stands), and it never answered. About half of all
nodes, the same ones every time; restarting the node did not help, because the stuck state was in the switchboard's entry for it. Only a restart of the switchboard cleared it, and the node's own registration
had nothing to do with it: it was not registered because it had no session.

Two smaller things were found on the way:

- A registered node that was refused (a name another key holds, a policy) asked again every 6 seconds on the same session, where a node that had never been registered waits for its next session. Each refusal
  is two strikes, and ten strikes are a ban of 15 minutes, doubling up to a day. (Only a restart of the switchboard clears a ban.)
- A node whose clock is set back is refused by the replay check on its handshake until real time passes the stamp the switchboard kept, and nothing said so.

## Decision

1. **A switchboard never starts a handshake to a node.** The rekey fallback is for nodes; on a switchboard a node's keys that are past their hard limit are erased and the node, which holds the way back, shakes hands
   again. Its one session that it does start is the one towards its registry.
2. **A switchboard always answers a node's handshake**, whatever key ids the two have: it has none of its own to keep.
3. **A registered node that is refused waits for its next session**, as a node that was never registered does.
4. **The status counts what was silent**: `hs_replay` (handshakes refused as replays: a node whose clock was set back looks like this) and `hs_crossed` (a handshake of our own that stood over a peer's: it should
   stay at 0 on a switchboard now), on a node and on a switchboard.

## Not done

- A node whose clock is set back stays refused until the switchboard's stored stamp passes. It is counted now (`hs_replay`) and not changed: the stamp is what keeps a captured handshake from being replayed.
- A switchboard that runs an older release keeps the fault: the switchboard is what has to be updated.

## Tests

`tests/switch.sh`: a switchboard whose session with a node is past the rekey and still heard starts no handshake (and the same session on a node does, so the check can fail); a registered node refused on its
session does not ask again until the next one, and asks on a new session.
