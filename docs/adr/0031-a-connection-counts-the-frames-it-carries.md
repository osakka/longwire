# 0031 — A connection counts the frames it carries, and only a node says so

Status: accepted. Builds on [0030](0030-three-surfaces-one-engine.md) (the pane) and on [0015](0015-scripts-decide-c-enforces.md).

## Context

The owner asked for bytes and a bit rate, sent and received, next to each connection. The engine already kept `rx_bytes` and
`tx_bytes` per peer, but they did not agree with each other and `status` did not carry them:

- `rx_bytes` counted the Ethernet frames delivered from the peer (a frame in two halves counted once, at its whole length).
- `tx_bytes` counted every encrypted payload sent: frames, but also the keepalives, the line-handshake and every other control
  message. So what one end said it sent could never match what the other end said it received.

A rate is a different kind of thing from a counter: it needs a clock and a previous reading, and the engine keeps neither for this.

## Decision

1. **A counter counts frames, at the frame boundary, in both directions.** `tx_bytes` and `rx_bytes` are the lengths of the
   Ethernet frames carried to and from that peer, each frame once, whole or in two halves. Keepalives, handshakes and control
   messages are not counted, and neither is our own header and tag: this is what the line carried for the people on it, not what
   the wire carried. A frame relayed through a member is counted at its two ends, against the peer it is for; the relay only forwards sealed datagrams it cannot open, and counts those as `relay_fwd`, not as traffic of its own.
   The number a person reads at one end is the number the other end reads, which the tests check against known traffic.
2. **`status` carries them** as `rx_bytes` and `tx_bytes` in each entry of `peers`, beside the frame counts. `longwire peers` is
   the same list, so it has them too. They are totals since the session started and go back to zero with it.
3. **A rate is made by whoever shows it, from two readings.** The control panel reads `status` once a second and divides the
   change by the milliseconds between reads. The engine stays free of clocks and state for this, and a reader that wants a
   different window (the average over a minute) can take one. A reading lower than the one before is a session that started
   again, and has no rate until the next read.
4. **Units.** Totals are bytes (decimal: kB, MB, GB). Rates are bits a second (kbit/s, Mbit/s, Gbit/s), because that is how
   links are rated. The pane shows both on the connection's second line: `rx 47.9 Mbit/s   tx 19.9 Mbit/s    6.0 MB in, 2.5 MB out`.
5. **The command prints totals, not rates.** `longwire peers` is a snapshot, and a rate is a thing between two of them. A rate on
   the command line belongs to a command that watches (`longwire watch`, M4), not to `peers`.
6. **The switchboard gets nothing, and this is the design, not a gap.** Once a call is through, the two nodes talk directly and the
   switchboard is not in the path ([0002](0002-a-software-switch-with-split-horizon.md), [0011](0011-one-switchboard-and-one-call-at-a-time.md)):
   it cannot count what it never sees, and having nodes report their traffic to it would be a record of who talked to whom and how
   much, which it does not keep ([0030](0030-three-surfaces-one-engine.md)). The wires on the switchboard application show that
   a call was put through and when. They never show a rate.

## Consequences

- One line of C moved and two added to `status`; the old `tx_bytes` (all payload) no longer exists. Nothing read it.
- A node that does not export the fields (older than this) shows no rate row and nothing else changes; the pane reads a missing
  counter as zero.
- Rates are only as good as the read interval: a burst inside a second is averaged over it. The pane's rate lags the traffic by at
  most a read.
- The counts are of frames after the switch, so a frame the switch drops (a loop, a flood over budget) is not counted as carried.
- If an owner later wants the traffic reported to a switchboard they run themselves, it would be an opt-in announcement of
  the same kind as a name ([0023](0023-names-are-opt-in-claims.md)), written as its own decision.
