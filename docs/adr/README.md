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
| [0011](0011-one-switchboard-and-one-call-at-a-time.md) | One switchboard per deployment, and one call at a time | accepted |
| [0012](0012-gpl-3-or-later.md) | The licence is GPL-3.0-or-later | accepted |
| [0013](0013-packages-for-the-module-the-catalogue-for-the-scripts.md) | Packages for the module, hibr's catalogue for the scripts | accepted *(planned: M4)* |
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

---

[← documentation](../../README.md)
