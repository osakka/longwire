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
| [0030](0030-three-surfaces-one-engine.md) | Three surfaces on one engine: the command, a control panel, a switchboard app | accepted |
| [0031](0031-a-connection-counts-the-frames-it-carries.md) | A connection counts the frames it carries, and only a node says so | accepted |
| [0032](0032-the-services-share-their-control-socket-with-a-group.md) | The services share their control socket with the group longwire | accepted |
| [0033](0033-a-switchboard-is-built-for-strangers.md) | A switchboard is built for strangers | accepted |
| [0034](0034-a-connection-is-a-profile.md) | A connection is a profile, and the Control Panel turns it on | accepted |
| [0035](0035-longwire-on-the-menu-bar.md) | Longwire on the menu bar and the Control Strip | accepted |
| [0036](0036-the-jack-gets-its-address-from-the-profile.md) | The jack gets its address from the profile | accepted |
| [0037](0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md) | Traffic to the other end never goes into our own jack | accepted |
| [0038](0038-the-jack-may-ask-a-dhcp-server.md) | The jack may ask a DHCP server | accepted |
| [0039](0039-a-site-node-from-the-pane.md) | A site node from the pane | accepted |

---

[← documentation](../../README.md)
