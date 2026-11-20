# 0095 — The kill switch is the system's, and the app opens it

Status: accepted; built after 0.5.74. (Ticket LW-11.)

## Context

Android has a kill switch for a VPN: Always-on VPN with **Block connections without VPN**. It blocks every connection that does not go through the VPN, from the moment the phone starts and whenever the VPN is down. An app cannot turn it on: the person does, for one VPN, in the system's settings.

## Decision

1. **The app does not make a kill switch of its own.** One in an app is a firewall made of the app's own state, and it fails open when the app is killed; the system's is kept by the system.
2. **The app takes the person to it:** a **Kill switch** card says what it does and has a button that opens the system's VPN settings (`Settings.ACTION_VPN_SETTINGS`), where the person turns on Always-on VPN and Block connections without VPN for Longwire. If the phone has no such page, it says so.
3. **What it costs is said:** with it on, nothing leaves the phone while the line is down.

## Consequences

- The strongest guarantee the phone can give, and none of the code for it is ours.
- "Send everything through the line" is a different thing: it chooses where traffic goes while the line is up; the kill switch decides what happens when it is not.
- Not tried on a phone: the button's intent, on any make of phone.
