# Changelog

What changed, newest first. The reasons are in [docs/adr](docs/adr/README.md); what is still to come is in
[docs/ROADMAP.md](docs/ROADMAP.md). `git log` is the full history.

## 0.5.18

*Published:* the catalogue (`hibr-apps` `97f8f0b`) and the packages (`hibr-apt` `992109e`, `hibr-rpm` `fac116a`, `hibr-arch` `b1e95d2`). `make check` passed (656 checks).

- **A security contact and a disclosure policy.** [SECURITY](docs/SECURITY.md#reporting-a-problem) and `man longwire-security` now say where to report a flaw (the maintainer's address, for now), what to send, that details stay private for
  90 days or until a fix, and that a reply is best effort. Documentation only; the design of the committed safety phrase is [ADR 0040](docs/adr/0040-the-safety-phrase-is-committed-before-it-is-shown.md).

## 0.5.17

*Published:* the catalogue (`hibr-apps` `80abc69`) and the packages (`hibr-apt` `695134e`, `hibr-rpm` `7ed4b32`, `hibr-arch` `48df562`). `make check` passed (656 checks). Not tried: a real service, a real card, polkit's dialog, and installing from the repositories on a clean machine.

- **A site node from the pane.** A profile with Attach = splice no longer ends in a note about a terminal command. **Turn on** asks for your administrator password once and a second root helper
  (`/usr/lib/longwire/site`, polkit action `org.longwire.site`) writes `/etc/longwire/CARD.conf` and starts the service `longwire@CARD` with the profile's switchboard, token, name, answer policy and `listed`; the
  pane then follows the service through its **Node** row, and **Stop the service** stops it. From a terminal: `longwire site CARD [PROFILE]` and `longwire site --stop CARD`. The options reach the helper in a file only the
  user can read (the token never shows in the process list), and the helper lets through five options only, each checked ([ADR 0039](docs/adr/0039-a-site-node-from-the-pane.md)). Tested against stand-ins for
  `systemctl` and `setfacl` (`tests/site.sh`), the script and the pane against a stand-in helper, and the packages (`tests/pkg.sh`). Not tried: a real service, a real card, polkit's dialog.

## 0.5.16

*Published:* the catalogue (`hibr-apps` `ec37821`) and the packages (`hibr-apt` `9897f6c`, `hibr-rpm` `63a6f81`, `hibr-arch` `0677460`). `make check` passed (620 checks). Not tried: a real DHCP lease, and installing from the repositories on a clean machine.

- **The jack may ask a DHCP server.** `address = dhcp` in a profile (the pane's **Address** row, or `longwire jack --address dhcp`): the root helper that makes the jack also starts a DHCP client on it in the same
  password step (`dhclient`, else `dhcpcd`, else `udhcpc`), which keeps asking until the line answers. It takes the address and the network route only: a hook removes the default route, the name servers and the rest,
  so a line cannot take over where the machine's traffic goes. A client of ours from before is stopped first; `on` and the pane ask without privilege whether one runs, so there is no second password
  ([ADR 0038](docs/adr/0038-the-jack-may-ask-a-dhcp-server.md)). Tested against stand-in clients (`tests/jack.sh`); not tried against a real lease.

## 0.5.15

*Published:* the catalogue (`hibr-apps` `87eff9e`) and the packages (`hibr-apt` `55c2ce0`, `hibr-rpm` `aa18fe8`, `hibr-arch` `3ece90d`). `make check` passed (606 checks). Not tried: the Out of step rows on a real desktop, and installing from the repositories on a clean machine.

- **Out-of-step warnings.** The scripts come from the catalogue and the engine from the package, and a running node keeps the engine it started with, so a fix could sit in one of the three and look as
  if it did nothing (it did, for hours). `longwire doctor` now says whether the scripts, the installed engine and the running node agree, and what to do if not (update the package; refresh the catalogue; turn the node
  off and on). `longwire show` and `longwire on` print the same warnings, and the Control Panel has an **Out of step** heading at the top with the same words. The engine answers `lw version` for this; an
  engine older than 0.5.15 does not know it, which is itself read as "older". Tested by `tests/doctor.sh`, a stand-in control socket in `tests/events.py`, and the pane tests.

## 0.5.14

*Published:* the catalogue (`hibr-apps` `2388745`) and the packages (`hibr-apt` `065ffbb`, `hibr-rpm` `ed55a3e`, `hibr-arch` `4c9e9dc`). `make check` passed (597 checks). The row appears once hibr 0.99.374 (which maps the large glyphs) and Blit 0.3.0 are installed. Not tried: installing from the repositories on a clean machine.

- **Phone Icon Size.** The menu bar telephone can be one cell (**small**, the default) or **large**, two cells wide, written as two codepoints side by side as Blit 0.3.0 draws them (modern U+100023 to U+10002A, classic
  U+10002B to U+100032). The pane has a **Phone Icon Size** row, shown only where the glyph set has the large icons (`GL[phoneoff2]` and the rest, `GL[phoneoffc2]` for the classic ones: hibr's glyph table has to map them);
  what is asked for is used when it exists, otherwise the same style in one cell, then the modern one; the Control Strip has one cell and always uses the small icon. Tested with stand-in glyph tables; not seen on a real desktop.

## 0.5.13

*Published:* the catalogue (`hibr-apps` `2fe101a`) and the packages (`hibr-apt` `adc3425`, `hibr-rpm` `e071002`, `hibr-arch` `8fa0f84`). `make check` passed (586 checks). Not tried: the Node row on a real desktop with two real nodes, and installing from the repositories on a clean machine.

- **Choosing among several nodes in the pane.** When more than one node answers on a machine (your own, and a service such as `longwire@eth0`), the pane has a **Node** row that picks which it shows and
  controls (the menu bar and the Control Strip follow it); a node that has gone gives way to the first that answers. The setting is kept with the other desktop settings.
- **Tokens without `openssl`.** `longwire-switchboard token new [N]`, `token list` and `token remove TOKEN` make, show and take out registration tokens in the tokens file (mode 0600, rewritten whole beside itself),
  with no switchboard running. Tested by `tests/tokens.sh`. Not built: tokens in the switchboard application on the desktop (a switchboard is usually on another machine, and its file belongs to the service).
- The pane's rate test no longer fails when the clock ticks over a second between two of its lines.

## 0.5.12

*Published:* the catalogue (`hibr-apps` `420417f`) and the packages (`hibr-apt` `94b098d`, `hibr-rpm` `b236b39`, `hibr-arch` `62b9be3`). `make check` passed (571 checks). Not tried: installing from the repositories on a clean machine.

- **Fixed: events torn in two, in `longwire up`, `watch` and `find`.** The scripts read events with `read -t 1`, whose deadline covers the whole read: an event that began just before the second ran out was
  cut, the first half was thrown away, and the second half was read as an event of its own (`hibr: json: malformed input near ,"phrase":"…`). In `longwire up` that could lose a ring, which
  is the event the answer policy acts on. The first half is now kept and joined by the rest. A test (`tests/events.sh`) sends an event in two halves with a pause longer than the deadline through a stand-in control socket; it fails on the old script
  and passes on the new. `watch` also prints the raw text of an event it still cannot read, so a bad one can be looked at.

## 0.5.11

*Published:* the catalogue (`hibr-apps` `71bd2c5`) and the packages (`hibr-apt` `b4f65d2`, `hibr-rpm` `273190d`, `hibr-arch` `fcedaec`). `make check` passed (567 checks). Not tried: installing from the repositories on a clean machine.

- **`longwire show` names the engine and the underlay.** The scripts (from the catalogue) and the engine (from the package) are updated separately, and a stale engine under new scripts had made a fix
  look as if it did not work. `show` now has an `engine` line with the version that is running (an engine older than 0.5.10 shows as "unknown"), and, when your jack has an address, an `underlay` line saying which
  device the traffic to peers in the jack's network leaves by, or that none could be had ([ADR 0037](docs/adr/0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md)). The same are in `status` as
  `version`, `underlay`, `underlay_state` and `jack_nets`. The upgrade note: the package carries the engine (`sudo apt install --only-upgrade longwire`, then turn the node off and on, because a running
  worker keeps the old engine); the catalogue carries the scripts.

## 0.5.10

*Published:* the catalogue (`hibr-apps` `3dc4353`) and the packages (`hibr-apt` `0973b4e`, `hibr-rpm` `a27a28f`, `hibr-arch` `a6f85a3`). `make check` passed (563 checks). Not tried: installing from the repositories on a clean machine.

- **Turn on with a slow switchboard is no longer "did not turn on".** If the node runs but the switchboard has not given it a number within the wait (now 15 s, not 8), `longwire on` says
  "on, waiting for the switchboard to give a number" and succeeds; only a switchboard that says no (a refused token) or a node that does not start is a failure. The pane's note shows every line of what
  the command said, not only the last one: a note reading ". on line d7c39a with 2 members" had hidden both the reason and the fact that the node was running.

## 0.5.9

*Published:* the catalogue (`hibr-apps` `8dfdcd8`) and the packages (`hibr-apt` `0917c54`, `hibr-rpm` `ec7ce3b`, `hibr-arch` `86854bd`). `make check` passed (562 checks). Not tried: the real loop on the owner's desktop, and installing from the repositories on a clean machine.

- **Traffic to the other end never goes into our own jack.** Found with the first real site node: a desktop whose jack had been given an address in the same network as the site node's card
  sent the packets that bring the tunnel up into the tunnel itself (`ip route get` said `dev lw0`), and the call kept starting and never connected. A node now sends everything for an address inside
  its jack's network through a second UDP socket tied to its real network device (the default route's, or `--underlay DEV`), which the jack's route cannot capture, and does not offer the address it
  has on its own jack. Without a device to tie it to (before Linux 5.7, or no default route) the address is left out and said so (`longwire watch`, `longwire show`, `cand_skipped` in `status`).
  See [ADR 0037](docs/adr/0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md) for the two ways it can still fail (strict reverse-path filtering, old kernels). Tested without privilege with the loopback
  interface standing in for the jack and the device (`tests/jacknet.sh`); not tried with a real tap and the real loop, which the owner's desktop and the site node will show. The registration wait in `tests/m1.py` is now 45 s, not 20: it failed twice on a loaded machine and passed alone.

## 0.5.8

*Published:* the catalogue (`hibr-apps` `3238f6b`) and the packages (`hibr-apt` `779774f`, `hibr-rpm` `7244241`, `hibr-arch` `657ce48`). `make check` passed (547 checks). Not tried: a real device getting its address, and installing from the repositories on a clean machine.

- **An address for the jack, so nobody runs `ip addr`.** A profile can carry `address = 10.77.0.2/24` (the pane's new **Address** row, or `longwire profile set home address=…`). The root helper that
  makes the jack puts the address on it (`longwire jack --address A.B.C.D/N`), after checking it, and **Turn on** / `longwire on` ask for the password only when the device does not already have it
  ([ADR 0036](docs/adr/0036-the-jack-gets-its-address-from-the-profile.md)). The helper is tested against a stand-in `ip` (`tests/jack.sh`), as an ordinary user; not tried on a real device. An address from a
  DHCP server on the line is still `dhclient lw0` by hand.

## 0.5.7

*Published:* the catalogue (`hibr-apps` `e791bd8`) and the packages (`hibr-apt` `7da4c5c`, `hibr-rpm` `ab77048`, `hibr-arch` `d2fab86`). `make check` passed (509 checks). `make lint` also passes under TCC 0.9.28rc (the Cix packaging ran it). Not tried: installing from the repositories on a clean machine.

- **`make lint` under newer TCC, the rest of it.** TCC 0.9.28rc gives `strchr`/`strrchr` a `const` result when the argument is `const`; 0.5.6 fixed one such assignment and the Cix packaging
  found a second (`engine/call.c`: `lw_resolve` and `sb_connect`). Every `strchr`, `strrchr`, `strstr`, `memchr` and `strpbrk` in `engine/` and `module/` was then read: the others take
  buffers the code owns and may write, so they stay as they are. Only 0.9.27 is installed here; the Cix packaging confirmed it under 0.9.28rc.

## 0.5.6

*Published:* the catalogue (`hibr-apps` `ddf965a`) and the packages (`hibr-apt` `978c18c`, `hibr-rpm` `d36557c`, `hibr-arch` `9a8d58f`). `make check` passed (509 checks). Not tried: `make lint` under TCC 0.9.28rc (only 0.9.27 is installed here), and installing from the repositories on a clean machine.

- **`make lint` passes under newer TCC.** `engine/port.c` held the result of `strchr` on a `const char *` in a plain `char *`; TCC 0.9.28rc (the one Cix pins) rejects that
  ("assignment discards qualifiers"). It is now `const char *`. Found by the Cix packaging.

## 0.5.5

*Published:* the catalogue (`hibr-apps` `35da11f`) and the packages (`hibr-apt` `0715712`, `hibr-rpm` `ce39aef`, `hibr-arch` `cc71523`). `make check` passed (509 checks). Not tried: installing from those repositories on a clean machine.

- **Rename a profile.** `longwire profile rename OLD NEW`, and a **Rename profile** row in the Control Panel pane. The settings, the token, the place in the list and being the default all stay; a name
  already taken, a name that is not letters, digits, `.` `_` `-`, and a profile that is not there are refused plainly.

## 0.5.4

*Published:* the catalogue (`hibr-apps` `dc5a595`) and the packages (`hibr-apt` `3dd9915`, `hibr-rpm` `71e33b1`, `hibr-arch` `a04dff8`). `make check` passed (498 checks). Not tried: installing from those repositories on a clean machine.

- **Fixed: Turn on said `lw start: need --splice NIC, --jack TAPNAME …`.** The pane shows a profile that never set **Attach** as "jack", but the node read an unset Attach as no attach mode at all. An unset
  Attach now means jack everywhere, as the pane shows it. A new test starts a profile with no Attach and checks that it tries the tap.

## 0.5.3

*Published:* the catalogue (`hibr-apps` `a256da7`) and the packages (`hibr-apt` `e8027f7`, `hibr-rpm` `744fbb0`, `hibr-arch` `e0579d6`). `make check` passed (497 checks). Not tried: installing from those repositories on a clean machine.

- **Fixed: Turn on in the Control Panel showed an empty note titled "Desktop" and Longwire stayed off.** The callback that reports the result of `longwire on` used a variable it had
  not declared, which hibr's strict mode refuses, so it stopped before it could say anything, whether the node had started or not. It now shows your number, or "Longwire did not turn on:" and
  the reason the command gave. The earlier tests replaced the job runner, so they never ran this callback; a new test runs it as the desktop does, once with a switchboard that works and once
  with a key that does not.

## 0.5.2

*Published:* the catalogue (`hibr-apps` `10d9b0b`) and the packages (`hibr-apt` `8ddc209`, `hibr-rpm` `2561f2e`, `hibr-arch` `bbf7215`). `make check` passed on the tree before the last
documentation-only edits (494 checks, `4c581fe`); the package test was run again on the exact packages published. Not tried: installing from those repositories on a clean machine, and the
units under a live systemd.

- **Documentation, properly.** Four manual pages (`man longwire`, `longwire-switchboard`, `longwire-profiles`, `longwire-security`) installed with the package, and five guides:
  [QUICKSTART](docs/QUICKSTART.md) (two machines on one line, on the desktop or the command line), [DESKTOP](docs/DESKTOP.md) (the pane, the menu bar telephone, the strip, the Switchboard window),
  [OPERATING](docs/OPERATING.md) (run a switchboard or a site, identity and backups, upgrades), [TROUBLESHOOTING](docs/TROUBLESHOOTING.md) (a message or a symptom, its cause, the fix) and
  [SECURITY](docs/SECURITY.md): the plain-language account of what longwire protects, which parts of "quantum-safe" are true (the key exchange, the cipher, the handshake's authentication)
  and which are not (the Ed25519 signatures that vouch for line membership), what has been checked and what has not. It deliberately does not say "ultra safe". The manual pages are
  linted and checked against the scripts by `tests/man.sh`, so they cannot fall behind the commands. The README no longer says "quantum-safe" without saying which part, nor "audited" of
  code whose audits are not ours.
- **Phone Style.** Blit has two sets of telephone icons (a modern handset, and a classic rotary phone from Blit 0.2.45); a **Phone Style** row in the pane (modern / classic, shown only where the
  glyph set draws telephones) picks one for both the menu bar telephone and the Control Strip, falling back to the modern set and then to characters. The icon names are agreed with the
  `hibr` session (`GL[phoneoffc]` and the rest); Blit 0.2.45 and hibr 0.99.366, which maps them, are out; without them classic simply shows the modern phone.
- **Fixes found while writing them.** The bundle `longwire` left out the command the pane runs, so the pane could not have turned anything on from a bundle install; the pane now `# needs:`
  `cli/longwire`, the bar item and the strip module need the pane, and the bundle lists all four. The switchboard script printed its address on standard output, which hibr holds back when it is
  redirected, so under a service it might not reach the journal; it is on standard error now. A stale comment in the Switchboard window said a service switchboard cannot be reached.

## 0.5.1

*Published:* the catalogue (`hibr-apps` `11a949f`) and the packages (`hibr-apt` `bb26450`, `hibr-rpm` `27c0f38`, `hibr-arch` `bb804c3`), each of the last three in one commit
that also removed the 0.6.0 file. `make check` passed on the exact tree (`e2623ca`). Not tried: installing from those repositories on a clean machine, and the units
under a live systemd.

*Renumbered:* this content was first published as 0.6.0 and withdrawn a few hours later, because the project stays on 0.5.x (the middle number moves
only when the user says so). It is the same menu bar item, strip module and bundle, plus the three commands below. The 0.6.0 files are gone from the
catalogue and the three package repositories (the git history of those repositories still holds them).

- **Longwire on the menu bar and the Control Strip** ([ADR 0035](docs/adr/0035-longwire-on-the-menu-bar.md)). A telephone on the bar that says whether the line
  is off (`☎ ○`), waiting for the switchboard (`☎ ◌`), on (`☎ ●`, with the number of people connected) or ringing (`☎ ◉ 447-120-933`), a **ring that
  interrupts** (a note, and the dialog with the caller and the six-word safety phrase, once per call), and a menu that follows the state: Turn
  On / Turn Off, Pick up, Dial, who is connected with their rates, and Settings. A Control Strip telephone coloured by the theme (dim, info, good or warning). One switch for
  it in the pane (and in Menu Bar Items). All three are one catalogue bundle, `longwire`. Where a glyph set has telephones that carry the state (Blit's, from Blit
  0.2.44: outline, dashed, filled, and with ring waves), the item uses them; otherwise it uses characters. Tested against real nodes with the desktop's functions
  stubbed (`tests/bar.sh`), and the files load under hibr's own desktop code. Not tested: how it is drawn.

- **`longwire`, `longwire show` and `longwire watch`.** The three roadmap items that answer "shouldn't it just work?" now that a connection is a profile. `longwire` alone turns the
  node on from the default profile if it is off and then says how it is (and with no profile yet says how to make one, and that Control Panel > Longwire does it too);
  `show` is the readable status (your number, the switchboard and NAT, who is connected with what they have carried, the line, any ring with its safety phrase); `watch`
  narrates the node's events as sentences as they happen, and leaves the node alone when you Ctrl-C. Tested against real nodes (`tests/profile.sh`: 30 checks).

## 0.5.0

*Published:* the catalogue (`hibr-apps` `288c876`) and the packages (`hibr-apt` `c6e32da`, `hibr-rpm` `eab116d`, `hibr-arch` `784d551`). `make check` passed on the exact tree
(`3a1187b`). Not tried: installing from those repositories on a clean machine, and the units under a live systemd.

- **A connection is a saved profile, and the Control Panel turns it on** ([ADR 0034](docs/adr/0034-a-connection-is-a-profile.md)). 0.4.0
  left you typing the switchboard and token each time, in a command that sat in the foreground, with a pane that had no token field and
  a tap device you made by hand as root. Now: `longwire profile set home switchboard=… token=… attach=jack` (one 0600 file the command, the
  pane and the menu bar share; the token can be kept off a command line), `longwire on` / `off` (in the background, saying the number it got),
  `longwire up` with nothing said using the default profile, `longwire jack` (a small root helper through polkit makes the tap after one
  password; `on` does it for you), and in the pane a profile picker, a token field, the rest of the form, and **Turn on / Turn off**, which
  makes the jack and asks for the password in the desktop. Tested: the commands against a real switchboard that wants a token
  (`tests/profile.sh`), the pane's rows and actions (stubs and hibr's own desktop code), the helper and policy in all three packages.
  Not tested: the form drawn on a real desktop, `pkexec` behind the desktop's pty job, a real tap. Not built: the menu bar item and control
  strip switch (next), picking the attach mode automatically, an address for the jack.

## 0.4.0

*Published:* the catalogue (`hibr-apps` `b44d5e8`: `cli/longwire`, `cli/longwire-switchboard`, the switchboard application and the pane) and the
packages (`hibr-apt` `393b065`, `hibr-rpm` `109d744`, `hibr-arch` `211d968`). `make check` passed on the exact tree before publishing (`2c59160`).
Not tried: installing from those repositories on a clean machine, and the units under a live systemd.

- **A switchboard built for strangers** ([ADR 0033](docs/adr/0033-a-switchboard-is-built-for-strangers.md)). Before putting one online
  we attacked it: every parser a stranger's bytes reach was read by three readers, and the engine now runs under TCC's own bounds
  checker against raw datagrams, a real handshake bent byte by byte, and a stranger's encrypted session (`tests/fuzz.sh`; a planted
  bug is found in half a second, and 4 seeds of 150,000 iterations per stage were clean before the fixes). It found real problems,
  three of them reproduced by a test that fails on the old code: **strangers could fill a switchboard's places for good** (a handshake
  and then silence; 24 filled 24 and the next honest node was refused), **a registration token could write into the event the policy
  script reads** (replacing the address its limit counted), and **a switchboard could connect a call nobody answered**. Fixed, with
  the smaller ones: unconfirmed sessions lapse and unregistered ones give way when the table is full; numbers and refusal text from
  a switchboard are checked and escaped; one registration pending per asker and at most a line's worth of dials and calls; a 1,024-place hashed table for introductions and a
  512-slot one for handshake fragments; a keyed per-source limit; IPv6 cookie replies; a one-step session lookup; a budget per
  session (messages, registrations, dials); `--max-numbers` and an indexed registry; the script counts an IPv6 /64, empties its
  counters, and refuses an option with its value missing; stricter names; pins written whole. The units are sandboxed (a system-call
  filter checked against a trace of the node and switchboard, no namespaces, a memory limit, ...) and say **`Restart=always`**
  (decided for the owner, one word to reverse: a group member's `stop` is now only a bounce). **Not verified, and said so in the
  record:** the handshake has had no formal analysis or outside review, the vendored crypto no published known-answer vectors (X25519, BLAKE2b, ChaCha20-Poly1305 and Ed25519 agree with independent implementations on 2,100 cases; ML-KEM was not cross-checked), the units no live
  systemd, and open registration can be used up to `--max-numbers` by anyone with enough addresses (use `--tokens`).
- **The services share their control socket with the group `longwire`** ([ADR 0032](docs/adr/0032-the-services-share-their-control-socket-with-a-group.md),
  amends 0004). `--group NAME|GID` gives a worker's control socket to a group at mode 0660, and then the file mode is the check; the
  two units run in the group with a runtime directory under `/run`, and the package makes the group (a `sysusers.d` file). The control
  panel pane drives a service node, and the switchboard application a service switchboard, when the person is in the group. Tested with
  three real users (`tests/group.sh`: owner, a user in the group only by a supplementary group, and a user in neither), the start
  refusing an unknown group, the pane and the application reading a socket under another `/run`, and the units and group in all
  three packages. Not tested: a running systemd, the group being made on a clean install, a desktop after `usermod`. This widens who
  may control a service node, so it should be a minor version.

## 0.3.1

- **The counters, published.** 0.3.0 went out without them; 0.3.1 has the byte counters and the rates in the pane
  ([ADR 0031](docs/adr/0031-a-connection-counts-the-frames-it-carries.md)), in the packages and in the four catalogue entries (`hibr-apps` `5ed137e`, `hibr-apt` `2435444`, `hibr-rpm` `814b4b9`,
  `hibr-arch` `6d7f182`). 0.3.0 stays in the repositories, so it can still be named. The whole suite passed before publishing, on the
  third full run: two earlier runs each failed `tests/m1.sh` once (a direct connection through a strict NAT, 80 s), under heavy
  load from other work on the machine; it has passed alone 16 times, and its cause is not found.

## M4: what a connection carried

- **Bytes and bit rates next to each connection** ([ADR 0031](docs/adr/0031-a-connection-counts-the-frames-it-carries.md)). `status` carries
  `rx_bytes` and `tx_bytes` for each peer (`longwire status`; `longwire peers` is the line's members and has none), now counted at the frame in both directions (`tx_bytes` used to include
  keepalives and control messages, so the two ends could not agree); the pane shows `rx` and `tx` in Mbit/s and the totals in MB,
  with the rate made from two reads. Tested: the two ends agree exactly after 600 frames each way and after frames split in two;
  the formatting and the rate arithmetic are tested against known numbers. Not tested: the rows drawn on a desktop, and a rate on real links.
  The switchboard shows no rates, by design.

## M4: published

- **longwire 0.3.0 is published.** The deb, the rpm and the pacman package are in hibr's apt, dnf and pacman repositories
  (`hibr-apt` `6db9f39`, `hibr-rpm` `d8e386f`, `hibr-arch` `917d8ac`), and hibr's catalogue has `cli/longwire`,
  `cli/longwire-switchboard`, `apps/switchboard` and `control-panel/longwire` (`hibr-apps` `3312e50`). `HIBR_MIN` is 0.99.315,
  the first hibr with the `cli` kind ([ADR 0029](docs/adr/0029-commands-are-their-own-catalogue-kind.md)). The whole suite
  passed against hibr 0.99.336 before publishing. Not tried: installing from the repositories on a clean machine.
- **A test that failed one run in 256 is fixed.** `tests/operator.py` mangled an eviction by setting its first byte to `00`,
  which is the line id's first byte and so changed nothing when it already was. It flips the byte now. One full run also failed
  `tests/m1.sh` ("the easy end dials" through a strict NAT, 80 s) while another session was running its own tests; it passed on
  the next eight runs, and a cause was not found.

## M4: the switchboard application

- **`apps/switchboard.hibr`** ([ADR 0030](docs/adr/0030-three-surfaces-one-engine.md)): the switchboard as a board: gauges and sparklines, the nodes
  with the names they gave, and the calls being set up as they happen, forgotten a few seconds after. Read-only.
  `/dev/longwire/events@PATH` opens another worker's event stream (a switchboard's, beside your node). `tests/sw.sh`. Drawing it
  on a real desktop is untested; no token management, and a switchboard run as a service is named, not reached.

## M4: the control panel pane

- **`control-panel/longwire.hibr`** ([ADR 0030](docs/adr/0030-three-surfaces-one-engine.md)): the node's number, name,
  switchboard and NAT; calls, with a ring shown as a row that carries the caller, the name as a claim and the safety phrase, and
  asks before picking up; peers to hang up on; the line and its members, with eviction; the answer policy; Dial; and Start for a
  jack node. A ringing call's `name` and `phrase` are now in `status`, which is what lets the pane (or anything polling) show them.
  `tests/pane.sh`. Its drawing is untested; there is no ring pop-up or menu-bar ☎ yet, because the desktop has no hook for them.

## M4: what the desktop reads

- **The data for the control panel and the switchboard app** ([ADR 0030](docs/adr/0030-three-surfaces-one-engine.md)): a node's
  `status` carries its `name` and `listed`; a switchboard lists its registry a page at a time (`lw sb nodes`, no addresses and
  no keys), reports its handshake gauges, and emits `call-ring`, `call-go` and `call-end` events that hold numbers only.
  The screens themselves are not built yet. `tests/panel.sh`.

## M4: a catalogue kind for commands

- **The scripts are commands, not desktop apps, so the catalogue has a tenth kind, `cli`** ([ADR 0029](docs/adr/0029-commands-are-their-own-catalogue-kind.md)).
  It was added in hibr (`catalogue install cli/NAME` files a script under `$XDG_DATA_HOME/hibr/cli`, which the desktop never
  scans; the Catalogue window calls it Commands) and in hibr-apps (`addapp --kind cli`, the licence a script declares, a dash in
  a name). The launchers look there first. The bundles are published.

## M4: packages

- **The module as a deb, an rpm and a pacman package** (`make packages`; [ADR 0028](docs/adr/0028-one-deb-and-the-others-from-it.md)).
  The deb is built first and the other two are made from its files, so they hold the same bytes; each depends on hibr 0.99.315 or
  newer (0.99.265 before the `cli` kind existed). They carry the module, two launchers that find the scripts the catalogue installed, two systemd
  services (`longwire-switchboard`, and `longwire@NIC` for a spliced site, with `CAP_NET_RAW` only), and the
  documentation; the scripts come from the catalogue and declare their `app` and `# modules:` headers. `make install`
  installs the same layout; `VERSION` is the one version. `tests/pkg.sh` builds all three and checks their contents,
  dependencies, that the module in them loads and that the launcher runs a script. Not done then: signed repositories to install
  from, publishing the scripts to the catalogue (both done: see above), Homebrew (it waits for macOS), and a package tried on a machine that is not
  this one.

## M3: the documentation review, and a limit on guessing numbers

- **A fresh read of the documentation against the code** found about twenty places where the prose was stale or wrong
  (an admit described as an untagged trailer, `ECHO` described as authenticated, a default of 16 peers that is 32,
  "planned" marks on things that are built, and so on); all are corrected, and what is left of M3 is named.
- **A node gets ten answers a minute to dials that find no one.** A dial to a number that is unknown or not online is
  answered as such ten times a minute per node; after that the answer is "refused by the switchboard" whichever it was, so
  guessing numbers to find who exists is slow and, past the budget, tells nothing. Tested in `tests/names.sh`.

## M3: IPv6

- **Nodes and switchboards speak IPv6** ([ADR 0027](docs/adr/0027-ipv6-is-a-second-socket.md)): a second socket beside the
  IPv4 one, global IPv6 addresses as host candidates, `--listen` as a list (`[::1]:4001`, a bare port for both), a
  switchboard that may be reached over either, and a global IPv6 address counted as public for relaying. The switch answers
  IPv6 neighbour solicitations for hosts across the line, as it does ARP. Tested (`tests/ipv6.sh`): everything over IPv6
  only, and two nodes that reach the switchboard over IPv4 but each other only over IPv6, with 100 frames each way;
  `tests/m2.sh` pings across two LANs by IPv6 and sees the local answer. Not tested: a real IPv6 network.

## M3: crossed handshakes settled by a tie-break

- **Two nodes that start handshakes at the same moment now always end on one session.** The one whose initiator has
  the lower key id stands; the other end forgets its own handshake and answers. Before, each end finished its own and
  discarded the session it had made for the other's, so about one start in three (two statically configured peers that
  both dial) left each end on a session the other had forgotten, and frames were dropped until the next rekey. Found
  through the DHCP test, which failed intermittently for that reason; `tests/start.sh` starts twelve pairs together
  (about a third fail on the old code). Two related changes: the end that is not the session's initiator rekeys after
  `rekey + min(10 s, rekey/4)`, which at a short interval is inside the hard limit (it used to be after it), and an end
  that is answering the other's handshake does not start one of its own meanwhile.

## M3: port spray

- **A strict NAT against an easy one now connects directly.** The candidate list carries a marker saying the sender
  is strict; the strict end opens 256 sockets and sends from each to the other end, the easy end probes random ports of
  the strict end's address at 100 a second, and the socket a probe lands on becomes that peer's own. Two strict
  NATs still go through a relay. Tested with a cone and a symmetric NAT model, calling from each end in turn, with 100
  frames each way after (`tests/m1.sh`, `sym` mode, which used to expect the call to fail).

## M3: operator-managed lines

- **A line may have an operator** ([ADR 0026](docs/adr/0026-a-line-may-have-an-operator.md)). The founder names an
  authority key (`up --authority`); from then on only that key's admits and evictions count. A newcomer brings the
  operator's admit (`up --admit`), `longwire revoke` carries an eviction, and `longwire operator` signs both on a
  machine that runs no node. Members keep the operator's evictions and tell each member they link to, so a node that
  was offline still hears. Status gains `managed`, `authority` and `id_full`. Tested in `tests/operator.sh`: join with
  an admit, refused without one or with another key's, a member's own evict refused, an eviction carried by a member,
  and a founder that was offline hearing of it on its return.

## M3: `longwire answer`

- **Change the answer policy while the node runs.** `longwire answer known` (or `ask`, `auto`, `never`); with no
  argument it shows the current one. The running `up` reads it at each ring. Tested in `tests/names.sh`.

## M3: self-describing candidates

- **Each candidate carries its length** (`len type port address`), so a reader can skip an address family it does
  not know. This is the one IPv6 change that had to come before a public switchboard exists
  ([ADR 0025](docs/adr/0025-every-candidate-carries-its-own-length.md)). IPv6 itself stays last in M3.

## M3: NAT-PMP

- **A mapped address as a candidate.** A node asks its default gateway for a mapping of its UDP port (NAT-PMP, RFC 6886),
  shows it as `mapped` in status, and offers it to the other side. `--nat-pmp off` turns it off. Tested against a fake
  gateway in `tests/pmp.sh`; no real router has been tried.

## M3: NAT-type detection

- **`ECHO`.** A switchboard answers on its port plus one with the address it sees; a node compares the two and reports
  `nat` as `easy`, `strict` or `unknown` in its status, and says so when a call fails because of it. Tested through
  the cone and symmetric NAT models in `tests/m1.sh`.

## M3: number release

- **A number unheard for a year is released, then held back for another** (`--release-after`, default `365d`): the
  registry stores when each number was last heard, nobody else can be given a held number, and its own key coming home
  gets it back. Tested with a release time of 8 seconds by `tests/release.sh`, across a restart of the switchboard.

## M3: smaller things

- **A longer rekey for switchboard sessions** (`--sb-rekey`, 600 s), the same on the node and the switchboard.
- **A warning when two DHCP servers share a line:** the switch reads the offers that pass and emits `dhcp-conflict`
  once, with both addresses. Tested by `tests/dhcp.sh`.

## M3: the identity as words

- **`longwire key export` and `longwire key import`.** The seed as 33 words from the safety-phrase list (one per byte, then a
  checksum word); import checks it and refuses to overwrite an identity, and the words come back as the same key and number.
  Tested by `tests/key.sh`.

## M3: the registry of switchboard codes

- **A switchboard is given its short code the way a node is given a number** ([ADR 0024](docs/adr/0024-a-full-number-is-a-switchboard-code-and-a-local-number.md)):
  `longwire-switchboard --registry HOST:PORT/KEY` registers with a registry (any switchboard started with `--issue-codes`) over the normal
  handshake and is told its code in `NUMBER`; its nodes then show `+20 312-884-076`. The plan is prefix-free: `+1` goes to the
  switchboard named by `--public-switchboard`, then `20`..`49`, `500`..`649` (without `555`), `6500`.. and so on, each length keeping the
  upper half of its free prefixes to open the next. The derived `+0…` code keeps working beside the short one.
- `--registry` is required (`none` for a private switchboard, which keeps its derived code). A registry limits askers (`--codes-per-ip-day`,
  default 2), refuses when it is not a registry, and remembers its book in `codes.log`. New: event `code-assigned`, `lw plan`, status
  fields `switchboard_codes` (nodes) and `code` / `code_refused` / `codes_issued` (switchboards).
- **Tests:** `tests/registry.sh` (the plan, +1, +20, the per-address limit, a non-registry refusing, restarts keeping the book).
  `LW_KEEP=1` keeps a test's logs and state for a look.

## M3: full numbers

- **A full number is a switchboard code and a local number** ([ADR 0024](docs/adr/0024-a-full-number-is-a-switchboard-code-and-a-local-number.md)):
  `+0731042 312-884-076`. Every switchboard has a code derived from its key (a `0` and six digits; assigned codes never begin with `0`), with no registry and nothing to configure;
  your own switchboard's numbers are still dialed without it. `registered` events and `status` carry the full number; `dial`, `who`,
  `unpin`, `hangup` and `evict` take either form; another switchboard's code is refused plainly (federation is M9). Separators are optional: the plan is prefix-free.
- **Pins and line members name their switchboard by key id**, on the wire and in `known` and the saved line, so nothing
  collides the day there are two switchboards. Older files are read as belonging to your own switchboard. Members are 32
  bytes larger on the wire. Not yet built: the registry that assigns short codes.

## M3: caller ID and the directory

- **Names are opt-in claims** ([ADR 0023](docs/adr/0023-names-are-opt-in-claims.md)): `up --name TEXT` announces a name when
  you ring someone, `--listed` puts it in the switchboard's directory. Both default to off. A switchboard carries names and
  answers lookups unless started with `--caller-id off` / `--directory off`, and tells each node what it offers.
- **Directory:** `longwire who NAME` (3+ characters, up to 8 results) and `who lw1…`, and `longwire dial lw1…` to call a
  full address. Only listed nodes are found; a node may look up once a second (burst 5) and is told when it is too fast.
- **A listed name is unique, like a number:** bound to the first key that lists it (`names.log`, so it survives a restart),
  refused to anyone else whether they list it or only announce it, compared without regard to ASCII case, released when
  its owner drops it. The `register` event the switchboard script sees now carries the name and whether it is listed.
- **Behind it:** `DIAL`, `RING` and `REGISTER` carry optional parts as tagged trailers (the admit moved into one); names are
  validated (UTF-8, no control characters, 32 bytes) and escaped wherever they reach JSON. New messages `LOOKUP`, `FOUND`. `ring` events gain `name`; new event `found`; status gains `switchboard_offers`.
- **Tests:** `tests/names.sh` (two switchboards, quotes and backslashes in a name, unlisted nodes not found, rate limit).

## M3, stage 2: relaying and path upgrade

- **Relay** ([ADR 0008](docs/adr/0008-hard-nats-are-relayed-by-line-members.md), [PROTOCOL §5](docs/PROTOCOL.md#5-relay-built-m3-stage-2)):
  when two direct tries to a line member fail, a node asks a connected member to carry the pair's packets. The pair
  runs its own handshake and session through the relay, which sees only sizes and timing; a relay is a member with a
  public address (`--relay auto`), forwards by 24-bit route id after checking the sender's address, and holds 64 routes
  that expire after 60 s idle. The switchboard still never relays.
- **Path upgrade:** a relayed peer calls its member again through the switchboard (`--direct-retry`, 60 s doubling to
  10 min); when the punch lands, session traffic goes to the punched address as well, and the first authenticated datagram
  straight from the other end drops the relay. Event `path`; `connected` has `path` `relay` and `via`.
- **Behind it:** every datagram to a peer now goes through one function that knows the peer's path; a datagram that came
  out of a relay never moves a peer's address; `NODE_STATUS` gains `relay` and each peer's `path`; the packet dispatcher
  became a function of its own. `FRAME_SINGLE` went from 1432 to 1428 so a wrapped full frame still fits a PPPoE link.
- **Per-source `HELLO` limit:** 2 a second (burst 4) from any one address, on top of the global 20 a second.
- **Crossed handshakes:** `tests/crossed.sh` starts both ends at once behind a delaying proxy; the link comes up and keeps
  rekeying. A lower-key-id tie-break was tried and dropped: it made that case worse, and no stall could be reproduced.
- **`longwire who NUMBER`:** what the node knows of a number (full address, pin, line membership, path, safety phrase). It is
  local and asks the switchboard nothing; `LOOKUP` and dialing a full address stay planned.
- **Tests:** `tests/m3b.sh`: two members behind symmetric NATs and a public third; frames (and full-size, fragmented ones)
  cross the relay, the NATs are loosened, the pair goes direct, then the relay is shut down and the pair carries on. The
  NAT simulator moved to `tests/lwnat.py`.

## M3, stage 1: lines of more than two nodes

- **Lines** ([ADR 0022](docs/adr/0022-lines-form-by-vouching.md)): up to 16 nodes on one L2 segment, joined by number.
  A node joins by calling any member, which vouches for it with a signed (Ed25519) admit and hands it the line; the
  newcomer then calls every other member, presenting the admit, and a member that holds a valid admit lets it through
  without a prompt under every answer policy but `never`. The founder admits itself. An admit is valid if its signer is in
  the *persisted* member list and it is newer than any eviction of that key.
- **Eviction:** `longwire evict NUMBER` signs and sends an eviction, drops the member everywhere, rotates the line key and
  bars the key from ringing back in. Lines persist (`~/.longwire/lines`, the key sealed under the seed), so a restarted member
  rejoins on the admit it kept, with nobody asked. A node that finds itself on no complete mesh says so (`line-partial`).
- **Behind it:** the single call became a call table (a line is many calls at once); a call that creates a mesh edge is
  quiet when it fails and retried with a back-off; two members dialing each other at once are resolved by key id;
  `DIAL` and `RING` carry the admit as an opaque blob, and `REJECTED` now names the number so a caller with several dials
  open knows which one failed; the switchboard knows nothing of lines.
- **The site secret** for two boxes on one segment is derived from the line key (a rotated key is accepted alongside the
  old one for 30 seconds); `--site-key` still overrides it.
- **New commands and verbs:** `peers`, `evict`, `pickup ID`, `reject ID`, `hangup NUMBER`; new events `line-joined`,
  `member-joined`, `member-left`, `member-evicted`, `evicted`, `line-partial`, `line-complete`; `ring` gains `admit`.
- **Also:** handshake ids are unique among a node's concurrent handshakes (a line starts many at once); a node holds 32
  peers by default.
- **Tests:** `tests/m3.sh` builds four LANs as one subnet, joined by number: the mesh builds with no prompt on any member,
  all twelve host pairs ping, a broadcast reaches every site, a fourth site joins through a different member, a member is
  evicted and cannot dial back in while the rest are untouched, a killed member restarts and the mesh heals from its
  persisted state, and a member leaves.

## M2: splice, and the documentation made to match the code

- **TCC only, no exceptions** ([ADR 0021](docs/adr/0021-tcc-is-the-compiler-without-exception.md)): the gcc option and the
  gcc cross-check runs are gone, the Makefile says `override CC = tcc` (a compiler named on the command line changes
  nothing), `make lint` is TCC's own `-Wall -Werror`, the Android plan no longer names the NDK (whether TCC can build
  that library is an open item, ROADMAP M6), and `tests/docs.py` fails if another compiler is named.
- **Fixed after an independent review of the docs against the code:**
  - a switchboard started with the *node's* handshake budget (50, not 2000) because the role was read before it was set;
  - a token's reserved number, if taken, was silently replaced by a random one; now the registration is refused;
  - expired and superseded session keys, and a finished handshake's secrets, are erased, not just marked unused;
  - a registration finishing in milliseconds could complete before the node script subscribed to events, so the user saw
    nothing; a new subscriber is now told the registration state (`registered` or `register-refused`) on connecting;
  - the node narrated on stdout, which hibr holds back when redirected, so a service's log lines appeared late; it now
    narrates on stderr;
  - `longwire up` ignored SIGTERM while waiting for an event (hibr runs traps between commands); it now wakes every
    second, so a service manager's stop is prompt and stops the worker;
  - a restarted `longwire up` failed because a worker was already running; it now attaches to it;
  - `longwire setup splice` now prints `Environment=LONGWIRE_MOD=` so the privileged moment names the module by path;
  - the switchboard script's control socket now follows `--state`; the Makefile's `HIBR` is `HIBR_SRC` so it no longer
    clashes with the `HIBR` the tests use; the `site` event is narrated.
  - Documentation: the example number `312-884-071` fails its own check digit (now `312-884-076`); the symmetric-NAT test
    covered symmetric-against-cone only (a symmetric-against-symmetric run was added); the order of the capability drop and
    the fork, the seed's brief life in the module process, the MTU wording, the probe-replay window, the endianness
    list, what the tests assert (`at least two rekeys`), and a dozen smaller points were corrected.

- **Splice:** an `AF_PACKET` port on a NIC in promiscuous mode, with no interface created. Frames the box injects are not
  read back (`PACKET_IGNORE_OUTGOING` and a direction filter); unfinished TCP/UDP checksums from virtual NICs are
  completed; coalesced TCP frames of up to 64 KB are cut into line-sized segments instead of being dropped.
- **The switch:** frames over 1432 bytes travel as two datagrams and are rejoined; TCP MSS is clamped on SYN and SYN-ACK;
  flooded frames have a budget per peer; ARP requests for hosts across the line are answered locally; a dropped peer's
  learned MACs are flushed and traffic for them is flooded again.
- **Site groups:** boxes sharing one LAN segment and a secret announce themselves with authenticated probes, the lowest
  key id forwards, the others stand by and keep a list of the remote hosts, and on takeover re-announce them so the LAN's
  switches move at once. Tested: kill the active box, traffic resumes in about 4 seconds (the test allows up to 14).
- **`call-lost`:** a call that was up notices when its other end dies, reports it, and goes idle; the script decides
  whether to dial again.
- **Handshake cookies:** past half the handshake budget a node answers an `HS_INIT` fragment without a valid cookie with a
  59-byte `COOKIE` and does no work. A legitimate handshake completes during a sustained flood (tested).
- **`longwire setup splice NIC` / `setup jack`** print the one-time privileged step; `up --splice NIC --site-key FILE`.
- **Documentation:** the single `DECISIONS.md` became `docs/adr/`, one record per decision (20 of them); the architecture,
  protocol, switchboard, CLI, crypto, threat-model and build documents were rewritten to say what the code does, with
  one marker, `*(planned: M3)*`, for what it does not yet do; `tests/docs.py` compares them with the code on every
  `make check`; the roadmap is status only.
- **Tests:** `tests/m2.sh` builds two LANs as one subnet from unprivileged namespaces and bridges, and runs real ping, ARP,
  a 3 MB TCP transfer, broadcasts, a broadcast storm, the capability drop, and the failover. The helpers shared by the
  suites moved to `tests/lwtest.py`.
- **Fixed on the way:** a coalesced-frame drop that cost 10% TCP retransmissions (4 Mbit/s became about 50); a 16-byte
  cookie hashed as if it were 32; a frame-size limit that split every clamped segment; a failover that left the LAN
  pointing at the dead box.

## M1: dial tone

- **The switchboard** (`longwire-switchboard`, a hibr script over the module's `lw sb` worker): 9-digit numbers with a
  Damm check digit, bound to keys; token registration, a token that reserves a number, limits per source address and per
  callee; the engine mechanism with the script's policy in `register` and `dial` events.
- **Calls:** `dial`, `pickup`, `reject`, `hangup` between two nodes, with the answer policies `ask`, `known`, `auto` and
  `never`, a six-word safety phrase on both screens, and key pinning (written once the call is up; `unpin` forgets one).
- **NAT hole punching:** host and reflexive candidates, simultaneous authenticated `PUNCH`, verified through a userspace
  cone NAT; two strict NATs fail cleanly with a plain message.
- **Bootstrapping:** `HELLO` fetches a bundle from a key id and lets a stranger introduce itself to a switchboard.
- **Fixed on the way:** `registered` was announced again at every rekey; a refused node asked again every 2 seconds;
  pins were written before the safety phrase could be compared; the caller gave up on a ring at 30 s while the callee rang
  for 60.

## M0: first frame

- **The engine** (`engine/`): identity from one 32-byte seed; a hybrid X25519 + ML-KEM-768 Noise-IK handshake in two
  fragments; ChaCha20-Poly1305 data packets with a 4096-packet replay window; rekeying, keepalive and dead-peer
  detection; the learning switch with split horizon.
- **The hibr module** (`module/longwire.c`): the `lw` builtin and the `/dev/longwire/events` scheme; a forked worker
  that closes inherited descriptors, locks its key memory and clears every capability.
- **The node script** (`scripts/longwire.hibr`), the Makefile (`make`, `make check`), and the vendored Monocypher and
  PQClean ML-KEM-768, built with TCC unchanged apart from a five-line shim.
- **Tests:** `tests/m0.sh`: three nodes across a proxy that duplicates and bit-flips datagrams.
