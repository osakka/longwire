# 0061 — A changed road is found at once, and a lost one is waited out quietly

Status: accepted; built in 0.5.48. Amends [0046](0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md) (the road was looked at only with everything through the line, and only while packets flowed).

## Context

A node learned that its network had changed in one way: its sessions died of silence, three keepalives and a second later (76 s by default), and only then did it start again from a
back-off that never grew past 8 s, the same for every member, so after an outage every node retried in step. A person who moves between networks, or whose Wi-Fi drops for a
minute, saw a line that stayed dead long after the network was back; a node with no network at all kept sending a handshake every few seconds into nothing.

## Decision

1. **One watcher of the road, always on.** Once a second, whether or not a packet flows (`engine/road.c`), the node takes a fingerprint of the road: the default route that is not the
   jack's, and every address on a device that is up (the jack and the loopback aside). It replaces the check that ran from the jack's per-packet refresh; the jack's own
   socket and everything-through-the-line follow the same watcher (0046).
2. **A changed road is said to everyone at once.** The node emits `road-changed` (`changed`), tells the switchboard its new address now (a heartbeat and a registration), sends every
   live session a keepalive from the new road, so that the far end follows the new address as it already does for any authenticated packet (`sess.c`), and lets every handshake
   and every member's redial that was backing off try now.
3. **A lost road is waited out quietly.** When there is a road and then none (no default route and no address on a device that is up), the node emits `road-changed` (`down`) and
   starts nothing: no handshake, no retry, until one is back; then `road-changed` (`back`) and the same as 2. A node that never had a road (a test on a loopback) is never
   quiet.
4. **Back-off is capped and spread.** A handshake that goes unanswered is retried after 2, 4, 8 and 16 s and then every 30 s, each time by 75 to 125 % of that, so that members
   that lost the same road do not retry in step, and a peer that stays gone costs one handshake in half a minute.
5. **A call that waits for the switchboard lives and dies with the switchboard session it was made on.** A dial sent on a session that is then lost or replaced is ended at once (a
   person's own call says so), instead of holding the line back from dialling again for its 65 s. A *mesh* call, a member dialling a member, which no person is asked about, waits
   12 s (`MESH_RING`) at the caller and at the switchboard instead of 65 s: the other node answers it at once or is not there. Found in the network test: a member that dialled a
   node during its outage, within the 76 s the switchboard still counts it online, was stuck dialling it for the 65 s after it was back.
6. **It is said.** The status has `road` (`state`, `changes`, `changed`, `sent`), the terminal says it, and `show` says when there is no network.

## Consequences

A laptop that changes network is back in seconds; one with no network sends nothing and reconnects the moment there is one. What is not changed: with everything through the line,
a lost road still lets the machine's own traffic out unencrypted (0045); a kill switch that blocks everything while the line is down is a separate decision *(planned: M4)*.
A dead peer is found after three keepalives as before; finding it faster *(planned: M7)* would cost probes in steady state. Tested on the kernel in namespaces
(`tests/road.sh`): an idle node that changes address is reached again in seconds; a link that goes down and returns; the datagrams a node sends with no network and for a peer
that stays gone.
