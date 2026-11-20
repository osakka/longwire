# Backlog

What is open, in the order it is done. **One ticket at a time**: a ticket is taken, built, tested (the full check), published and closed before the next is taken; a ticket that is small may share a release with the one before it. A ticket that is a design carries
its milestone (`*(planned: M6)*`) until it is built. Each closed ticket names the release and its ADR; the [CHANGELOG](../CHANGELOG.md) and the [ROADMAP](ROADMAP.md) are the record of what shipped.

| # | Ticket | Status |
|---|---|---|
| LW-1 | The Android app shows its version | built, ships in 0.5.70 |
| LW-2 | The mesh calls an absent member less and less often | built, ships with LW-1 in 0.5.70 |
| LW-3 | The phone keeps one session (it does not mesh with the whole line) | open |
| LW-4 | The desktop jack takes a whole DHCP lease: gateway, name servers and routes, within limits | open, needs the owner's lease (`/run/longwire/dhcp-lw0.leases`) |
| LW-5 | The desktop jack tells the DHCP client the machine's own host name | open |
| LW-6 | A member's claim to an address a local host holds is refused (ARP and neighbour inspection) | open |
| LW-7 | The profile and the pane set `--dhcp-guard` and `--dhcp-server` | open |
| LW-8 | The Switchboard window shows which nodes are one site | built, ships with LW-1 and LW-2 in 0.5.70 |
| LW-9 | The phone carries IPv6 | open |
| LW-10 | The phone's seed is kept in the Android Keystore, and a recovery phrase is shown | open |
| LW-11 | The phone offers the system's kill switch | open |
| LW-12 | The phone dials by name from Android's contacts | open |
| LW-13 | The phone asks its home to keep quiet (the leaf role's keep-alive) | open, after LW-3 |
| LW-14 | The numbers a person dialled, and the boxes vouched for, survive a restart of the node | open |

## The tickets

**LW-1: version in the app.** The owner wants to know what they are on. The main screen shows `longwire 0.5.70 (5070)` (the app's version and its build number), and the notification's detail does not. *Done when* the screen shows it, and the APK's
version is the release's.

**LW-2: calmer mesh.** Every member of a line calls every member it has no session with, once a minute for ever, and the switchboard sees each call. The wait grows to a quarter of an hour ([ADR 0085](adr/0085-the-mesh-calls-an-absent-member-less-and-less-often.md)).
Built and unit-tested; ships in 0.5.70.

**LW-3: one session.** The phone is a member and meshes with the whole line: a call to every member it lacks a session with, which costs its battery and tells the switchboard who is in the line. A leaf (the planned role, [ARCHITECTURE §5](ARCHITECTURE.md#5-member-mode-a-phone-as-an-ethernet-host))
calls only the numbers it was given and keeps those sessions. *Done when* a phone in a line of five holds sessions with the numbers in its list and no others, and still has its lease, its DHCP and its failover; the engine's mesh is off for a leaf.

**LW-4: whole DHCP lease for the jack.** Address and subnet only today, by design ([ADR 0063](adr/0063-inputs-are-bounded-by-the-way-they-are-written.md)): no router, no name servers, no routes. The owner found it half-baked. The design: the router if it is inside the leased subnet and private; routes of the lease
(option 121) only to private networks, a default route only with "everything through the line"; name servers only if private. *Needs* what the owner's server actually sent.

**LW-5: host name.** The phone now tells the DHCP server its own name; the jack runs the system's client and sends what its configuration says. Pass the machine's host name explicitly to `dhclient`, `dhcpcd` and `udhcpc`.

**LW-6: address claims.** A member's frame is refused if its source is a host speaking on our LAN ([ADR 0083](adr/0083-a-pair-of-boxes-is-one-site-and-a-bridge-carries-no-control-frames.md) does it by bytes and by MAC). ARP and neighbour advertisements that *claim* an address a local host holds, whatever the frame's source MAC, pass through. *Done when* they are refused, with a count and an event.

**LW-7: the pane and the profile** carry the DHCP guard and the named servers, as the command line does.

**LW-8: the pair in the window.** The switchboard knew nothing of sites: two boxes were two entries. A splice box in a site group now tells its switchboard which it is and who it is paired with, and the window lists the pair together ([ADR 0086](adr/0086-the-switchboard-window-shows-a-pair-as-a-pair.md)). Display only.

**LW-9: IPv6 on the phone** (SLAAC and an NDP responder); until then it is dropped, and black-holed with "everything through the line".

**LW-10: the seed.** A file in the app's private storage today. Keep it in the Android Keystore, and show the 33 words (`longwire key export`) so a lost phone can be replaced.

**LW-11: kill switch.** Android's "Block connections without VPN" is the kill switch; the app could say so and open the setting.

**LW-12: names on the phone**: Android's contacts, a different mechanism from the desktop's ([ADR 0082](adr/0082-a-name-is-dialled-from-the-address-book.md)).

**LW-13: keep quiet.** The home node sends keep-alives at its own rate and the phone's radio wakes for them. A leaf could ask it to slow down.

**LW-14: restarts.** The dialled numbers and the vouched boxes are kept for the life of the node and are not written down; a restart asks again. Keep them in the state directory.

Not on the list: Windows and macOS members *(planned: M6)*; the old-switchboard fallback (dropped: no compatibility is kept).
