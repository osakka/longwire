# 0099 — A sleeping phone and a long keep-alive do not lose the line

Status: accepted; built in the release after 0.5.74. The owner's phone (pixon) showed as gone at the switchboard now and then, and when it did the line did not work, though the system's key icon and the app said it was up; opening the app and browsing brought it back, and the switchboard showed it again. Two causes, found together.

## What was wrong

1. **Presence lapsed.** The switchboard calls a node online if it was heard in the last 80 seconds, and only a registration, a heartbeat or a dial is hearing it (a keep-alive is not). A node sent its heartbeat every *keep-alive* seconds: 25 s was inside 80 s, but the battery saver's 55 s, and anything near it, left 25 s of slack for the network and the radio, and a late heartbeat showed the phone offline until the next. `tests/presence.sh` reproduced it: a node with a 120 s keep-alive was last heard 169 s before.
2. **A sleeping phone stops its timers.** The app held no wake lock: with the screen off the processor sleeps, the keep-alive timers do not run, the router forgets the phone's UDP mapping in about half a minute, and the boxes' packets for the phone go nowhere. The line is dead, the key and the notification say it is up, and the first packet the phone sends after something wakes it makes a new mapping and revives it. That is the "opened the app and browsed" the owner saw.

## Decision

1. **A node's heartbeat to its switchboard is at most 30 seconds apart,** whatever its keep-alive (`HB_MAX`). Nothing changes for the default 25 s. The switchboard needs no change and an older one keeps its 80 s.
2. **The app holds a partial wake lock and a high-performance wifi lock while the line is on,** by default, with a switch (*Keep the line up while the phone sleeps*) to turn it off. Released when the line is turned off or the service ends.

## Consequences

- A phone with the battery saver on stays online at the switchboard, at the price of a heartbeat every 30 s (a small packet) instead of 55 s.
- The line stays up while the phone sleeps. The cost is battery: the processor is not allowed to sleep. A phone that is on charge all day does not care; the switch is for one that is not.
- Android may still stop an app that has not been let off the battery optimisation (the *Always on* card); a wake lock does not survive Doze for such an app.
- Not tried on a phone: the wake lock and the wifi lock are compiled, and the owner's phone is the test.
