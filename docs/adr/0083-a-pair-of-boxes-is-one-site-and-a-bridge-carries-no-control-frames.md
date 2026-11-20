# 0083 — A pair of boxes is one site, and a bridge carries no control frames

Status: accepted; built in 0.5.68. The owner asked how two boxes on one segment (vpn-1 and a vpn-2) know which is handling, whether something sits between them, and how spanning tree would react; and that it be 100%, so it was
checked against the code and then against a real bridge. What was found is below.

## Context

Two splice boxes on one LAN segment elect one to forward (site groups, [ARCHITECTURE §4](../ARCHITECTURE.md)): a probe a second on the LAN, the lowest key id forwards, the other stands by and takes over after 3.5 s of silence. Nothing travels
between them over the line for this. Checked and tested:

1. **Spanning tree was tunnelled.** Nothing in the engine filtered the link-local control frames, so a bridge's BPDUs, LLDP, LACP and Cisco's CDP and PVST+ crossed the line (532 copies of a BPDU and a CDP frame crossed in the test with the new code off). Carried, they join two sites'
   spanning trees, trip BPDU guard on the box's port, and make one site's switch a neighbour of another's. And a loop through the line is invisible to spanning tree, whose bridges see two hosts.
2. **Two boxes that both forward loop.** That happens when the election cannot work (probes lost, clocks apart, no shared secret) or in the moments after a box has joined a line, before it has the secret. A member's frame whose source was a local host was learned correctly
   and still written to the LAN; the other box read it as a LAN frame and carried it again: in the test a single broadcast came back to its sender 462 times.
3. **The DHCP guard ([ADR 0080](0080-only-a-member-we-asked-for-may-speak-for-the-network.md)) broke the pair.** A client trusts DHCP only from a member it asked for. Whichever box of a pair is active carries the lease; if that is the one the client did not dial, its lease was refused, and the phone
   that works today would lose its address at its next renewal.

## Decision

1. **The link-local control frames cross in neither direction**, on every kind of port: `01:80:C2:00:00:00` to `0F`, and `01:00:0C:CC:CC:CC`, `:CD` and `01:00:0C:CD:CD:CD`. They are counted (`drop_ctrl`).
2. **A frame that has come round is dropped, and its sender is a segment-mate.** Every frame carried is remembered, by a 64-bit hash of its bytes, for a second. A frame from a member whose bytes are those of a frame that came off our LAN, or a frame off our LAN whose bytes are
   those of one a member just handed us, is the same frame come round: it is dropped (`drop_echo`, and nothing is learned from it). A member that does this three times in five seconds is **on our own LAN segment** (a member at another site cannot have), and nothing is carried between it and the LAN for a minute
   (`loop-guard` event), which ends a loop whatever the two boxes believe about each other. A member's frame from a host that spoke on our LAN within ten seconds is dropped too. A frame with no twin from the other side is never touched: a host's repeats, a burst of identical datagrams, a retransmission.
   The LAN port is read before the line's sockets, so a frame is heard where it was sent before its copy returns. Hashing costs 1.7 µs a 1400-byte frame (6.6 Gbit/s on one core in a TCC build).
3. **A pair is one site to a client.** A splice box tells every member it has a session with which boxes share its segment (`SITE`, every 5 s, authenticated by the session). A member that *asked for* that box remembers them, for as long as it runs, as members that may answer DHCP and advertise routers for it; from anyone else
   it is ignored. So a client that dialled vpn-1 takes its lease from vpn-2 when vpn-2 is the active box, and still can after vpn-1 is gone.

## What it does and does not promise

- Two boxes on one segment and one line are active and standby, in a few seconds (0.6 to 3.6 s in the tests, both key orders); a newcomer that joins while a host broadcasts twenty frames a second causes **no** frame to return to its sender and none to reach the other site twice.
- If the election fails, the loop is stopped by the memory and the segment-mate rule, at the price of duplicates (each box carries each frame) until the election works. The `loop-guard` event says so.
- Spanning tree on the LAN is neither affected nor consulted: the boxes are hosts to it. Two boxes need a shared secret (a line, or `--site-key`) to elect; the memory is a second line, not a replacement.
- **Not done**: the member-side display of the pair in the Switchboard window; the phone's list of several numbers *(planned: M6)*.

## Tests

`tests/guard.sh` (bounds-checked; fails 6 ways with the memory and control filter off): control frames in both directions and what is not one; a frame that comes round; three make a segment-mate, whose frames and the LAN's are not carried, for a minute; the reverse direction; ten identical frames five
milliseconds apart all go through; vouching, by a member that was asked for and not by one that was not, and kept when that member goes. `tests/m2b.sh`: a real bridge, two boxes linked to each other and both forwarding: a broadcast comes back to its sender no times, reaches the
other site at most once from each box, no storm, ping works, BPDU, LLDP and CDP cross in neither direction (fails with 462 echoes and over 500 copies of a BPDU with the new code off), IP multicast is carried. `tests/m2c.sh`: a second box joins through a real switchboard while a host
broadcasts, in both key orders: 0 echoes, at most one copy at the other site, exactly one once settled. `tests/m2d.sh`: a phone-style client dials vpn-1 only and gets, renews, and after vpn-1 stops renews again, its lease from a DHCP server on the LAN, whichever box is active (fails without `SITE`).
