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

---

[← documentation](../../README.md)
