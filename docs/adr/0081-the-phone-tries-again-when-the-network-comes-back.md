# 0081 — The phone tries again when the network comes back

Status: accepted; built in 0.5.66. The owner, on the redial: should it back off, by growing the wait, until the line comes back; and can it be configured?

## Context

[ADR 0079](0079-the-phone-says-who-it-is-joined-to.md)'s app redialled the number every 5 s, doubling to two minutes, and started again from 5 s once the line was up. That is a growing wait, not a knock. It has two faults: a wait earned on one network is
served out on the next (leave the wifi, arrive in coverage, and the phone sits idle for up to two minutes), and a session that was up when the network changed is left to time out, because the engine cannot always see a
change of network for itself on Android (what it can list of the phone's own interfaces is restricted).

## Decision

1. **The system's word is listened to.** The service registers for the default network (`ConnectivityManager`): when a network is available or its properties change, the back-off is forgotten (the next try is at once, then 5 s) and the engine is told.
2. **A new control word, `road`**, does what the engine's own road watcher does when it sees a change: tell every peer from the new road (an authenticated keep-alive, so the far end follows our address), start every handshake that was backing off again, redial the mesh, and
   register again with the switchboard. The word answers `{"road":"kicked"}`.
3. **The longest wait is a setting**, **When the other end is not answering**: 30 s, 2 min (the default) or 10 min. The first retry is always after 5 s, since someone watching the screen wants a quick answer; the wait doubles up to the chosen longest.
   It takes effect the next time the line is turned on.

## Not done

Not run on a phone: that Android gives the callback for the underlying network (the app is left out of its own tunnel), and that `road` brings a line back across a change from wifi to mobile data, are for the first device test. The test shows the word works and the line goes on after it.

## Tests

`tests/leafnet.sh`: `road` answers, and packets cross after it, on a real line.
