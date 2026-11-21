# Decisions

longwire is a layer-2 mesh VPN written for hibr and built with TCC. Each decision that shaped it is written down here,
one record per decision, in the order they were taken, in the same form hibr uses.

A record states what the situation was, what was decided, and what that decision costs. The cost matters as much as the
benefit; a decision with no downside is usually a decision nobody had to make. A record whose design isn't implemented
yet carries the same `*(planned: M3)*` marker as the rest of the documentation.

| | | |
|---|---|---|
| [0001](0001-the-name-is-longwire.md) | The name is longwire | accepted |
| [0002](0002-a-software-switch-with-split-horizon.md) | A software Ethernet switch whose ports are encrypted sessions | accepted |
| [0003](0003-whole-sites-first-single-machines-too.md) | Whole sites first, and single machines too | accepted |
| [0004](0004-a-node-never-runs-as-root.md) | A node never runs as root; any privilege is a one-time grant | accepted |
| [0005](0005-standard-primitives-and-a-hybrid-handshake.md) | Standard primitives, and a hybrid X25519 + ML-KEM-768 handshake | accepted |
| [0006](0006-the-v1-target-is-about-300-mbit-s-under-tcc.md) | The v1 throughput target is about 300 Mbit/s, built with TCC | accepted |
| [0007](0007-three-components-all-hibr.md) | Three components, all hibr: a switchboard script, a node script, a module | accepted |
| [0008](0008-hard-nats-are-relayed-by-line-members.md) | Hard NATs are relayed by line members, never by the switchboard | accepted |
| [0009](0009-the-first-ring-asks-and-a-key-is-pinned-once-the-call-is-up.md) | The first ring asks, and a key is pinned once the call is up | accepted |
| [0010](0010-numbers-are-allocated-and-bound-to-keys.md) | Numbers are allocated by the switchboard and bound to keys | accepted |
| [0011](0011-one-switchboard-and-one-call-at-a-time.md) | One switchboard per deployment, and a person is asked about one call at a time | accepted |
| [0012](0012-gpl-3-or-later.md) | The licence is GPL-3.0-or-later | accepted |
| [0013](0013-packages-for-the-module-the-catalogue-for-the-scripts.md) | Packages for the module, hibr's catalogue for the scripts | accepted |
| [0014](0014-a-default-public-switchboard.md) | A default public switchboard, operated by the project | accepted |
| [0015](0015-scripts-decide-c-enforces.md) | Scripts decide policy; C enforces what must not be skippable | accepted |
| [0016](0016-cookies-are-checked-on-every-fragment.md) | Handshake cookies are checked on every fragment | accepted |
| [0017](0017-a-splice-box-makes-a-nic-fit-the-line.md) | A splice box makes what a NIC gives it fit the line | accepted |
| [0018](0018-boxes-on-one-segment-elect-one-to-forward.md) | Boxes on one segment share a secret and elect one to forward | accepted |
| [0019](0019-the-tests-build-their-own-network.md) | The tests build their own network from unprivileged namespaces | accepted |
| [0020](0020-the-docs-are-checked-against-the-code.md) | The documentation is checked against the code | accepted |
| [0021](0021-tcc-is-the-compiler-without-exception.md) | TCC is the compiler, without exception | accepted |
| [0022](0022-lines-form-by-vouching.md) | Lines form by vouching; the founder admits itself; an evicted key stays out | accepted |
| [0023](0023-names-are-opt-in-claims.md) | Names are opt-in claims: a node chooses to announce one, a switchboard chooses to carry it | accepted |
| [0024](0024-a-full-number-is-a-switchboard-code-and-a-local-number.md) | A full number is a switchboard code and a local number | accepted |
| [0025](0025-every-candidate-carries-its-own-length.md) | Every candidate carries its own length | accepted |
| [0026](0026-a-line-may-have-an-operator.md) | A line may have an operator | accepted |
| [0027](0027-ipv6-is-a-second-socket.md) | IPv6 is a second socket, not a dual-stack one | accepted |
| [0028](0028-one-deb-and-the-others-from-it.md) | One deb, the others made from it, and the launchers find what the catalogue installed | accepted |
| [0029](0029-commands-are-their-own-catalogue-kind.md) | Commands are their own catalogue kind | accepted |
| [0030](0030-three-surfaces-one-engine.md) | Three surfaces on one engine: the command, a control panel, and a switchboard app | accepted |
| [0031](0031-a-connection-counts-the-frames-it-carries.md) | A connection counts the frames it carries, and only a node says so | accepted |
| [0032](0032-the-services-share-their-control-socket-with-a-group.md) | The services share their control socket with the group longwire | accepted |
| [0033](0033-a-switchboard-is-built-for-strangers.md) | A switchboard is built for strangers | accepted |
| [0034](0034-a-connection-is-a-profile.md) | A connection is a profile, and the Control Panel turns it on | accepted |
| [0035](0035-longwire-on-the-menu-bar.md) | Longwire on the menu bar and the Control Strip | accepted |
| [0036](0036-the-jack-gets-its-address-from-the-profile.md) | The jack gets its address from the profile | accepted |
| [0037](0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md) | Traffic to the other end never goes into our own jack | accepted |
| [0038](0038-the-jack-may-ask-a-dhcp-server.md) | The jack may ask a DHCP server | accepted |
| [0039](0039-a-site-node-from-the-pane.md) | A site node from the pane | accepted |
| [0040](0040-the-safety-phrase-is-committed-before-it-is-shown.md) | The safety phrase is committed to before it is shown | accepted; stage 1 built in 0.5.21, stage 2 *(planned: M8)* |
| [0041](0041-the-helpers-and-the-launcher-are-hibr-scripts.md) | The helpers and the launcher are hibr scripts | accepted |
| [0042](0042-the-jack-stays-unless-asked.md) | The jack stays unless asked | accepted |
| [0043](0043-strikes-and-bans-count-only-what-cannot-be-faked.md) | Strikes and bans count only what cannot be faked | accepted; built in 0.5.24 |
| [0044](0044-routes-beyond-the-line-go-through-a-gateway-on-it.md) | Routes beyond the line go through a gateway on it | accepted; built in 0.5.25 |
| [0045](0045-everything-through-the-line-is-a-table-of-the-jacks-own.md) | Everything through the line is a table of the jack's own | accepted; built in 0.5.26 |
| [0046](0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md) | A node that takes everything lets go when the road changes | accepted; built in 0.5.27 |
| [0047](0047-names-through-the-line-are-set-on-the-jack.md) | Names through the line are set on the jack | accepted; built in 0.5.27 |
| [0048](0048-a-silent-switchboard-is-said-so.md) | A silent switchboard is said so | accepted; built in 0.5.28 |
| [0049](0049-the-switchboard-window-shows-traffic-and-names-every-key.md) | The Switchboard window shows traffic and names every key | accepted; built in 0.5.29 |
| [0050](0050-a-switchboards-tokens-are-made-in-its-window.md) | A switchboard's tokens are made in its window | accepted; built in 0.5.32 |
| [0051](0051-longwires-datagrams-may-be-split-on-the-way.md) | longwire's datagrams may be split on the way | accepted; built in 0.5.33 |
| [0052](0052-the-strong-phrase-is-the-default-the-echo-has-a-budget-tokens-are-128-bits.md) | The strong phrase is the default, the echo port has a budget, tokens are 128 bits | accepted; built in 0.5.35 |
| [0053](0053-the-switchboard-has-ports-of-its-own.md) | The switchboard has ports of its own | accepted; built in 0.5.36 |
| [0054](0054-a-phrase-is-checked-by-typing-the-other-ends-words.md) | A phrase is checked by typing the other end's words | accepted; built in 0.5.37 |
| [0055](0055-no-legacy.md) | No legacy, no backwards compatibility | accepted; built in 0.5.38 |
| [0056](0056-a-member-can-share-its-internet.md) | A member can share its internet; the gateway is a member's key, not an address | accepted; built in 0.5.39 |
| [0057](0057-the-safety-phrase-belongs-to-the-session.md) | The safety phrase belongs to the session; the lower key id commits first | accepted; built in 0.5.41 |
| [0058](0058-the-device-and-the-whole-machine-are-two-permissions.md) | The device and the whole machine are two permissions | accepted; built in 0.5.45 |
| [0059](0059-name-servers-without-everything.md) | Name servers through the line without everything | accepted; built in 0.5.46 |
| [0060](0060-the-pane-shows-the-password-setting-and-never-writes-it.md) | The pane shows whose password a permission asks, and never writes it | accepted; built in 0.5.47 |
| [0061](0061-a-changed-road-is-found-at-once-and-a-lost-one-is-waited-out-quietly.md) | A changed road is found at once, and a lost one is waited out quietly | accepted; built in 0.5.48 |
| [0062](0062-the-node-can-make-only-the-system-calls-it-needs.md) | The node can make only the system calls it needs | accepted; built in 0.5.49 |
| [0063](0063-inputs-are-bounded-by-the-way-they-are-written.md) | Inputs are bounded by the way the code is written | accepted; built in 0.5.49 |
| [0064](0064-a-members-record-is-a-claim-and-the-way-out-is-never-the-lowest-number.md) | A member's record is a claim, and the way out is never "the lowest number" | accepted; built in 0.5.50 |
| [0065](0065-an-admit-has-an-end-and-a-revocation-does-not-outlive-it.md) | An admit has an end, and a revocation does not outlive it | accepted; built in 0.5.51 |
| [0066](0066-a-member-shows-its-admit-before-anything-it-sends-counts.md) | A member shows its admit before anything it sends counts | accepted; built in 0.5.52 |
| [0067](0067-a-control-message-too-big-for-one-session-message-is-sent-in-two.md) | A control message too big for one session message is sent in two | accepted; built in 0.5.53 |
| [0068](0068-a-ring-with-an-admit-is-sent-in-two-to-a-node-that-says-it-can-take-two.md) | A RING with an admit is sent in two to a node that says it can take two | accepted; built in 0.5.54 |
| [0069](0069-the-member-a-newcomer-joined-through-is-held-to-an-admit-too.md) | The member a newcomer joined through is held to an admit too | accepted; built in 0.5.55 |
| [0070](0070-the-classic-phone-is-two-cells-under-blits-glyphs.md) | The classic phone is two cells under Blit's glyphs | accepted; built in 0.5.56 |
| [0071](0071-a-switchboard-never-starts-a-handshake-to-a-node.md) | A switchboard never starts a handshake to a node | accepted; built in 0.5.57 |
| [0072](0072-every-dropdown-in-the-pane-is-one-declared-list.md) | Every dropdown in the pane is one declared list | accepted; built in 0.5.58 |
| [0073](0073-name-servers-say-so-when-they-cannot-be-asked.md) | Name servers say so when they cannot be asked, and systemd-resolved is suggested | accepted; built in 0.5.59 |
| [0074](0074-tcc-builds-the-engine-for-android.md) | TCC builds the engine for Android | accepted; the build in 0.5.60, the app *(planned: M6)* |
| [0075](0075-the-switchboard-window-says-what-matters.md) | The Switchboard window says what matters | accepted; built in 0.5.60 |
| [0076](0076-the-phone-is-a-member-with-an-adapter-and-an-app.md) | The phone is a member, with an adapter and an app | accepted; built in 0.5.61, not run on a phone |
| [0077](0077-a-phone-is-enrolled-by-a-link.md) | A phone is enrolled by a link | accepted; built in 0.5.62 |
| [0078](0078-a-phone-with-a-face-a-logo-and-a-battery.md) | A phone with a face, a logo and a battery | accepted; built in 0.5.63 |
| [0079](0079-the-phone-says-who-it-is-joined-to.md) | The phone says who it is joined to | accepted; built in 0.5.64 |
| [0080](0080-only-a-member-we-asked-for-may-speak-for-the-network.md) | Only a member we asked for may speak for the network | accepted; built in 0.5.65 |
| [0081](0081-the-phone-tries-again-when-the-network-comes-back.md) | The phone tries again when the network comes back | accepted; built in 0.5.66 |
| [0082](0082-a-name-is-dialled-from-the-address-book.md) | A name is dialled from the address book | accepted; built in 0.5.67 |
| [0083](0083-a-pair-of-boxes-is-one-site-and-a-bridge-carries-no-control-frames.md) | A pair of boxes is one site, and a bridge carries no control frames | accepted; built in 0.5.68 |
| [0084](0084-the-phone-has-backup-numbers-and-gives-up-by-its-own-limits.md) | The phone has backup numbers and gives up by its own limits | accepted; built in 0.5.69 |
| [0085](0085-the-mesh-calls-an-absent-member-less-and-less-often.md) | The mesh calls an absent member less and less often | accepted; built in 0.5.70 |
| [0086](0086-the-switchboard-window-shows-a-pair-as-a-pair.md) | The switchboard window shows a pair as a pair | accepted; built in 0.5.70 |
| [0087](0087-a-leaf-keeps-only-the-sessions-it-asked-for.md) | A leaf keeps only the sessions it asked for | accepted |
| [0088](0088-the-jack-tells-the-dhcp-server-the-machines-name.md) | The jack tells the DHCP server the machine's name | accepted |
| [0089](0089-who-may-speak-for-the-network-is-not-kept-across-a-restart.md) | Who may speak for the network is not kept across a restart | accepted |
| [0090](0090-a-member-may-not-claim-an-address-our-lan-holds.md) | A member may not claim an address our LAN holds | accepted |
| [0091](0091-the-phones-seed-lives-in-the-keystore.md) | The phone's seed lives in the keystore | accepted |
| [0092](0092-a-leaf-keeps-the-other-box-of-a-pair-too.md) | A leaf keeps the other box of a pair too | accepted |
| [0093](0093-a-site-is-one-bracket-in-the-switchboard-window.md) | A site is one bracket in the Switchboard window | accepted |
| [0094](0094-a-phone-learns-the-other-box-of-a-pair.md) | A phone learns the other box of a pair | accepted |
| [0095](0095-the-kill-switch-is-the-systems-and-the-app-opens-it.md) | The kill switch is the system's, and the app opens it | accepted |
| [0096](0096-a-number-is-picked-from-the-contacts.md) | A number is picked from the contacts | accepted |
| [0097](0097-a-member-that-refuses-the-mesh-is-called-every-six-hours.md) | A member that refuses the mesh is called every six hours | accepted |
| [0098](0098-the-phone-carries-ipv6.md) | The phone carries IPv6 | accepted |
| [0099](0099-a-sleeping-phone-and-a-long-keep-alive-do-not-lose-the-line.md) | A sleeping phone and a long keep-alive do not lose the line | accepted |
| [0100](0100-a-ring-says-why-a-member-was-not-let-in.md) | A ring says why a member was not let in | accepted |

---

[← documentation](../../README.md)
