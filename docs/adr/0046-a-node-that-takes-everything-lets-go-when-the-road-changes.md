# 0046 — A node that takes everything lets go when the road changes

Status: accepted; built in 0.5.27. Follows [0045](0045-everything-through-the-line-is-a-table-of-the-jacks-own.md).

## Context

With everything through the line, every socket of the engine is tied to the device the machine reached the world by when the node started ([0045](0045-everything-through-the-line-is-a-table-of-the-jacks-own.md)). A
laptop moves: from Wi-Fi to a cable, from one network to none. The tied sockets cannot be tied again to the new device without privilege (the kernel lets an ordinary process tie a socket once), so the line
dies. But the node still holds the jack, the jack keeps its carrier, and the kernel's safety net does not fire: the machine would sit with every packet going into a dead line, until someone turned longwire off.
That is the one thing [0045](0045-everything-through-the-line-is-a-table-of-the-jacks-own.md) promised would not happen.

## Decision

1. **The engine looks at the road once a second** (the default route's device that is not the jack, as it chose it at the start). When it is no longer the device its sockets are tied to, or there is none, the
   engine **closes the jack at once**. The carrier goes, the kernel passes over the jack's table, and the machine is on its own road again, whatever the script does next. It then tells the `roamed` event (`from`, `to`; `to` empty when there is no road).
2. **The node script starts the node again** on the new road: it stops the old one, waits for a default route that is not a jack (saying so once, if it has to wait), and starts again with what it was started with.
   The calls are made again as at any start.
3. **Without everything through the line**, the underlay socket of [0037](0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md) is closed when its device stops being the road, and opened again on the new one when next wanted.
4. **A device named with `--underlay`** is not followed: the person said which one.
5. **A node started by hand** without `--everything` on a jack whose table takes everything gets it added by the node script, which looks at the rules first: a node that does not tie its sockets there would send its own
   packets into the line.

## Consequences

Moving while everything goes through the line costs a few seconds of the machine's own road and a fresh start, never a machine with no road. Tested: the engine on a stand-in jack and a route file that changes under it
(the same road keeps the jack; a new road, or none, lets it go: `tests/jacknet.sh`). Not tried: a laptop really moving.
