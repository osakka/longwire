# Backlog

What is open, in the order it is done. **One ticket at a time**: a ticket is taken, built, tested (the full check), published and closed before the next is taken; a ticket that is small may share a release with the one before it. A ticket that is a design carries
its milestone (`*(planned: M6)*`) until it is built. Each closed ticket names the release and its ADR; the [CHANGELOG](../CHANGELOG.md) and the [ROADMAP](ROADMAP.md) are the record of what shipped.

| # | Ticket | Status |
|---|---|---|
| LW-1 | The Android app shows its version | built, ships in 0.5.70 |
| LW-2 | The mesh calls an absent member less and less often | built, ships with LW-1 in 0.5.70 |
| LW-3 | The phone keeps one session (it does not mesh with the whole line) | built in 0.5.71 ([ADR 0087](adr/0087-a-leaf-keeps-only-the-sessions-it-asked-for.md)) |
| LW-4 | The desktop jack takes a whole DHCP lease: gateway, name servers and routes, within limits | open, needs the owner's lease (`/run/longwire/dhcp-lw0.leases`) |
| LW-5 | The desktop jack tells the DHCP client the machine's own host name | built in 0.5.71 ([ADR 0088](adr/0088-the-jack-tells-the-dhcp-server-the-machines-name.md)) |
| LW-6 | A member's claim to an address a local host holds is refused (ARP and neighbour inspection) | built in 0.5.71 ([ADR 0090](adr/0090-a-member-may-not-claim-an-address-our-lan-holds.md)) |
| LW-7 | The profile and the pane set `--dhcp-guard` and `--dhcp-server` | built in 0.5.71 |
| LW-8 | The Switchboard window shows which nodes are one site | built, ships with LW-1 and LW-2 in 0.5.70 |
| LW-10 | The phone's seed is kept in the Android Keystore, and a recovery phrase is shown | built in 0.5.72 ([ADR 0091](adr/0091-the-phones-seed-lives-in-the-keystore.md)) |
| LW-11 | The phone offers the system's kill switch | built after 0.5.74 ([ADR 0095](adr/0095-the-kill-switch-is-the-systems-and-the-app-opens-it.md)) |
| LW-12 | The phone dials by name from Android's contacts | built after 0.5.74 as a picker ([ADR 0096](adr/0096-a-number-is-picked-from-the-contacts.md)); a name typed in the list is not looked up |
| LW-13 | The phone asks its home to keep quiet (the leaf role's keep-alive) | built in 0.5.74 as a longer wait toward a member that refuses the mesh ([ADR 0097](adr/0097-a-member-that-refuses-the-mesh-is-called-every-six-hours.md)); the home's keep-alive rate is not slowed |
| LW-14 | The numbers a person dialled, and the boxes vouched for, survive a restart of the node | closed without building: [ADR 0089](adr/0089-who-may-speak-for-the-network-is-not-kept-across-a-restart.md) |
| LW-15 | The IANA application for the name `longwire` and a port (UDP 6741 proposed) is submitted, followed up, and the default changed if IANA assigns another | drafted ([IANA](IANA.md)); the owner submits |
| LW-16 | **Redundancy: a phone that restarts with its dialled box dead still reaches the pair.** A leaf learns the other box only from a live vouch (kept in memory, [ADR 0089](adr/0089-who-may-speak-for-the-network-is-not-kept-across-a-restart.md)): with only a dead box in its list it cannot reach the survivor, which the 0.5.70 mesh did from the line roster. The app learns and shows the partner boxes (visible, removable) and keeps them in its list | built after 0.5.73 ([ADR 0094](adr/0094-a-phone-learns-the-other-box-of-a-pair.md)) |
| LW-17 | **The Switchboard window shows a site as a tree**: a parent row for the site (its boxes counted, active and standby) with each box a child, so redundancy is seen at a glance; a ghost child from a claim two minutes stale is cleared when the node goes offline (BUG-5) | built after 0.5.73 as a bracket ([ADR 0093](adr/0093-a-site-is-one-bracket-in-the-switchboard-window.md)); a parent row and the BUG-5 ghost remain |
| LW-18 | **The phone app's supervisor under failover**: what "up" means when the dialled box is dead and its partner carries traffic; the give-up limits and an engine restart (which drops the vouch) must not fire while the line is up through the survivor. A JVM test with a stand-in engine (BUG-8) | built after 0.5.74: the decisions are in `Failover`, tested on a JVM (`tests/failover.sh`); the loop around it (status, notification, the network callback) is still untested |
| LW-19 | **The leaf dials a vouched box at once**, not on the mesh's backoff (which grows to 15 minutes): a vouch proves the box is alive on the segment now. And a failover time bound in `m2d` | built after 0.5.73 ([ADR 0092](adr/0092-a-leaf-keeps-the-other-box-of-a-pair-too.md)); the time bound in `m2d` is in |
| LW-20 | **The release roll of a pair**: the boxes restart within about 40 s of each release; roll the standby first, verify the pair and a client's lease, then the active (with cix) | written as a procedure in [OPERATING](OPERATING.md#updating-a-pair-of-boxes); an automatic roll that does one box at a time is the operator's to set up (cix's image) |
| LW-21 | **BUG-9 reproduced production-shaped**: a 0.5.69 switchboard, 0.5.70 nodes as a splice site pair behind one NAT address, restarted; and the switchboard peer's state on a stuck node (`handshaking` or `idle`) recorded before any restart | done: the pair and the older switchboard were both tried (`tests/pairrestart.sh`) and recover; NAT and a real registry remain untried |
| LW-22 | **hibr 0.99.555: `dt_set` forces no redraw for a catalogue entry's own setting.** The bar item's change key already includes `DT_LWSTYLE`; the window's Compact toggle is a key action (it redraws after it). Run the window, the bar item and the pane against hibr main before 0.99.555 ships, as hibr asked | closed: the window, bar item, pane and panel tests pass on hibr 0.99.554 and on the 0.99.555 release candidate bb7dfbb0; the one failure seen was my pair test assuming the order the switchboard lists nodes in (fixed) |
| LW-23 | **Resume after a stall is tested:** the phone engine stopped (SIGSTOP) for two minutes and continued: the time to its first echo through the line is bounded (the engine now counts time with the clock that runs through suspend, `CLOCK_BOOTTIME`, [ADR 0099](adr/0099-a-sleeping-phone-and-a-long-keep-alive-do-not-lose-the-line.md); a stop of the process does not stop that clock, so this tests the stall, not a real suspend) | open |
| LW-24 | **A receiver's dead-peer timeout follows the interval the sender announces in the session** (a message in the session; older nodes ignore it), which makes a long phone interval (the battery saver) safe and the only way a heartbeat or keep-alive setting could be offered | open |
| LW-9 | The phone carries IPv6 | built after 0.5.74 ([ADR 0098](adr/0098-the-phone-carries-ipv6.md)) |

## Bugs and gaps found

Found while auditing the documents against the code and the tests (2026-11-20), and while building the pair and the phone. Each names how it shows, what is known of the cause, and what closes it. A bug is taken like a ticket, in this order, before the feature tickets that touch the same code.

| # | Finding | Severity | Status |
|---|---|---|---|
| BUG-1 | `tests/m1.sh` (a NAT test: "the strict end dials", 80 s) failed once in a full check under load and passed alone, twice; the cause is not found | medium: a flaky test hides a real regression | open |
| BUG-2 | `tests/operator.sh` ("the evicted node is refused if it rings back") failed once in a full check and passed alone; the cause is not found | medium | closed in 0.5.77: the test rang the founder before it was back on the switchboard (a ring to a node that is not online is turned back there): it now waits for the dial tone, and for the test's own watcher of the founder's events to be connected (it gave up after ten seconds on a loaded machine) |
| BUG-3 | `tests/expiry.sh` failed intermittently in full checks: a rejoined member is judged at its 20 s deadline and the test waited exactly 20 s; and a "pre-roll" admit was made five seconds before *now*, not before the roll. Fixed in the test (0.5.67) and run six times under load | low | closed in 0.5.67 |
| BUG-4 | The phone's count of failed tries only counts calls made, so with a switchboard that cannot be reached only the time limit gives up; the screen does not say so | low | open |
| BUG-5 | A box's claim of its site role is kept by the switchboard for two minutes: after a box stops, the window can show a pair, and "active", for up to two minutes | low | documented ([ADR 0086](adr/0086-the-switchboard-window-shows-a-pair-as-a-pair.md)); a refinement would clear it when the node goes offline |
| BUG-6 | A ring is named from the address book only when the caller was saved by phone number: a person saved by full address only is not named ([ADR 0082](adr/0082-a-name-is-dialled-from-the-address-book.md)) | low | open |
| BUG-7 | `drop_echo` counts two different things (a frame that came round by its bytes, and a member's frame from a host of our own LAN); the status does not say which | low | open |
| BUG-10 | The owner's phone showed as gone at the switchboard now and then, and the line did not work, with the key icon and the app saying it was up, until the app was opened | high | fix built, **unconfirmed on the phone**: presence is reproduced and fixed (`tests/presence.sh`); the wake lock and the clock are reasoned, not observed. To confirm: cix watches pixon's peer `state`, `age` and `rx_frames` on vpn-2 across a screen-off period on 0.5.75; the owner says whether the app is let off battery optimisation (a wake lock does not survive Doze without it) |
| BUG-11 | **Battery saver was unsafe.** Every receiver erases a session after 3 keep-alives of its own (76 s at the default 25 s), and the saver sent every 55 s: one late or lost keep-alive was 110 s of silence, and the line was down for minutes | medium | closed after 0.5.78: the saver's keep-alive is 35 s, so one lost packet is tolerated ([ADR 0104](adr/0104-the-battery-saver-keeps-one-lost-keep-alive-in-hand.md)); not tried on a phone |
| BUG-13 | **Two dials at once can make two lines.** A node that dials a second number before the first one's line has reached it has no line yet: the second callee founds a line of its own, and the node ends on one of the two while the other keeps it on record; the two boxes never meet (seen in `admitheal` under load; 0.5.79 has it too) | medium | open: a node that has a call coming up into a line should hold its other line handshakes until that line has come (or a second LINE for another line should be refused while one is pending); the tests now wait for the line between dials |
| BUG-12 | **A desktop refuses every DHCP answer after a restart** (the guard asked for nobody: the dialled numbers and vouches were in memory only, and a desktop has no number to dial), and the 169.254 address stays; found from the owner's `longwire watch` | high | closed in 0.5.77: [ADR 0101](adr/0101-who-may-speak-for-the-network-is-kept-for-thirty-days.md), `tests/trust.sh` |
| BUG-9 | **Unconfirmed, and it may not be a bug in longwire.** After the srv-uk-01 switchboard (0.5.69) was restarted at 15:49 UTC on 2026-11-20, vpn-1 and vpn-2 (0.5.70) logged no answer from it, showed "no switchboard" for 90 minutes, and registered at once when each was restarted. That is all the evidence, and it is the nodes' own logs. Not reproduced in four shapes: a switchboard restarted under two nodes (down 3 s and 150 s), a 0.5.69 switchboard under today's nodes (73 s), and a 0.5.69 switchboard under a splice pair that sends its site claim (down 5 s: 5 s; down 120 s: 34 s) (`tests/sbrestart.sh`, `tests/pairrestart.sh`). Not tried: NAT between the nodes and the switchboard, a real registry, the cix container network. Other causes that fit the evidence and are not longwire's: a firewall or rate limit at the host that dropped the nodes' flow after the restart, the provider's NAT state, or the switchboard being unreachable for longer than the restart. It is settled by the switchboard's counters and ban list at the time (`longwire-switchboard status`, `bans list`), which were not taken | medium until it is confirmed | open: kept so that the next occurrence is measured before anything is restarted |
| BUG-8 | The Android app's logic (the number list, failover, give-up, the network callback) has no test of its own: only compilation, and the engine's side | medium | partly closed: the failover and give-up decisions are tested (`tests/failover.sh`); the rest of the supervisor and the screens are not |

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

**LW-16 to LW-21** are the redundancy tickets the advisor review of 2026-11-20 raised after the phone failed to get an address through a standby box ([ADR 0092](adr/0092-a-leaf-keeps-the-other-box-of-a-pair-too.md)); the owner asked for no workarounds and a tree in the window. They come before LW-9.

**LW-9: IPv6 on the phone** (SLAAC and an NDP responder); until then it is dropped, and black-holed with "everything through the line".

**LW-10: the seed.** A file in the app's private storage today. Keep it in the Android Keystore, and show the 33 words (`longwire key export`) so a lost phone can be replaced.

**LW-11: kill switch.** Android's "Block connections without VPN" is the kill switch; the app could say so and open the setting.

**LW-12: names on the phone**: Android's contacts, a different mechanism from the desktop's ([ADR 0082](adr/0082-a-name-is-dialled-from-the-address-book.md)).

**LW-13: keep quiet.** The home node sends keep-alives at its own rate and the phone's radio wakes for them. A leaf could ask it to slow down.

**LW-14: restarts.** The dialled numbers and the vouched boxes are kept for the life of the node and are not written down; a restart asks again. Keep them in the state directory.

**LW-15: the port.** The application is written ([IANA](IANA.md)) and checked against the registry (no entry for 6741, 9488 or the name). The owner supplies the assignee and contact and a public address for the specification, and submits it at <https://www.iana.org/form/ports-services>. When IANA answers: 6741 is recorded;
another number changes the default everywhere it is written (one release); a refusal is answered. The echo port is derived from the listen port by a fixed offset (2747) and is not asked for.

Not on the list: Windows and macOS members *(planned: M6)*; the old-switchboard fallback (dropped: no compatibility is kept).
