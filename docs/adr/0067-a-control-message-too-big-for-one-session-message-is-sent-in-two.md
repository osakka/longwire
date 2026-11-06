# 0067 — A control message too big for one session message is sent in two

Status: accepted; built in 0.5.53. Found while looking at the size of a RING (ADR 0066, "Not done").

## Context

A control message (a `LINE`, a `RING`, a `RELAY_OK`) is sent inside one session message, which holds at most `FRAME_MAX` (1514) bytes; `sess_send` refuses anything longer and the caller never
learns. The line message that tells a newcomer who is on the line is 250 bytes and 106 for each member, so with 12 members or more it is 1522 bytes or more and is **never sent**: the founder
lists the newcomer as a member, the newcomer holds no line, and nothing says so. The limit of a line is 16 members, and the 12th, 13th and 14th (measured with `tests/bigline.sh` on 0.5.52: the
founder lists 14, and the last three list none) could not join. Ethernet frames have had a two-part split for a long time; control messages had not.

## Decision

1. **A control message that does not fit one session message is sent in two**, in the way a frame is: `K_CFRAG`, `id(2) part(1) total(2) bytes`, half in each, sent and received by nodes. A message
   that fits is sent whole, exactly as every release before this one sent it: nothing that worked changes on the wire, and a node that has not been updated loses only what it could never have had.
2. **The receiver puts them back together and handles the message as if it had come whole**, through the same handler. It accepts a control message in two parts only when it is larger than one
   session message would have held (a frame's split and a control message's split of the same size and id do not join), within the largest control message (2051 bytes), and with exactly the
   sizes a sender makes. A switchboard takes none (what a node says to it is small, and it would otherwise hold a reassembly buffer for every stranger); it sends them.
3. **The status counts them** (`ctl_split`), so a test can see that it happened.

## Not done

- A `RING` with an admit is about 1.44 to 1.5 KB and still goes whole in this release: see [ADR 0068](0068-a-ring-with-an-admit-is-sent-in-two-to-a-node-that-says-it-can-take-two.md), which splits it
  for the nodes that say they can take two parts.

## Tests

`tests/switch.sh` (the halves, in either order, are handled as one message; half is not; a frame's half and a control message's half do not join; a message that would have fitted is not accepted
in two), `tests/bigline.sh` (a line of 14: every member lists all 14; it fails on 0.5.52 with the last three listing none).
