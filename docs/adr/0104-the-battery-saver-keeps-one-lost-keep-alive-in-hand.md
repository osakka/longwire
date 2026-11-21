# 0104 — The battery saver keeps one lost keep-alive in hand

Status: accepted; built in the release after 0.5.78. (Bug BUG-11, found by the advisor review of the phone's redundancy.)

## What was wrong

A node erases a session when it has heard nothing for three of its own keep-alives and a second (76 s at the default 25 s). The battery saver sent the phone's keep-alive every 55 s, so one late or lost keep-alive was 110 s of silence: a box could erase the session while the phone still believed in it, and the phone, which stops hearing the box once it has erased the session, took another 166 s (three of its own 55 s) to notice and handshake again: a line down for minutes for one lost packet.

## Decision

1. **The saver's keep-alive is 35 s, not 55 s.** One lost keep-alive is then 70 s of silence, inside a box's 76 s: one packet may be lost without the session being erased. The saving over 25 s is smaller (a quarter fewer wake-ups, not a half), and it holds a line that does not fall for one lost packet.
2. **The rule it follows:** a sender's interval must be below half of the receiver's dead-peer time (76 s at the default keep-alive of 25 s). A box started with a shorter `--keepalive` than the default takes less from the phone; announcing the interval in the session so that the receiver follows it is ticket LW-24.

## Consequences

- With the wake lock on ([ADR 0099](0099-a-sleeping-phone-and-a-long-keep-alive-do-not-lose-the-line.md)) the phone's processor is awake anyway, and the saver's saving is the radio's alone: the owner may leave it off.
- Not tried on a phone.
