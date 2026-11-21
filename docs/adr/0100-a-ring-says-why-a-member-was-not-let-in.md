# 0100 — A ring says why a member was not let in

Status: accepted; built in 0.5.76. The owner's desktop, which answers by hand, was called again and again by vpn-1 and made to wait for `longwire pickup`, while vpn-2's calls were let in by themselves; the line took more than a minute to come up each time. Both boxes are members of the desktop's line, and the desktop said nothing about why it did not recognise one of them.

## Context

A call from a member of the line carries an *admit*, the member's proof that the line let it in. A node lets in by itself a call whose admit it can verify (that is how a mesh builds without a prompt), and asks a person about one it cannot. The check (`admit_verify`) said yes or no. A no for a member of the line, as for a stranger, printed only "is calling… run: longwire pickup": nothing said whether the admit was missing, signed by a key the node has no record of, signed by a founder the node does not think is the founder, out of its lifetime, or older than an eviction. Any of those is a fault to be found, and none could be told from another without the node's state in hand. The test that reproduces the setup (`tests/meshask.sh`: a desktop that asks, a founder, a second box, both restarted) passes, so what differs on the owner's line is in its state.

## Decision

1. **The check returns its reason** (`admit_why`, the same logic as before): no admit came; this node is not on a line; the admit is for another node; it is out of time (its lifetime, or this machine's clock); the operator's signature does not verify; a managed line takes only the operator's admit or its founder's own; the signer is not on this node's record of the line; the member signed its own admit and is not the recorded founder; the signature does not verify against the signer's key on record; the admit is older than the member's eviction.
2. **A ring from a member of the line that is not let in says why** (`why` in the ring event; `longwire watch` prints it: *it is on your line, but not let in by itself: …*). A stranger's ring says nothing: it has no admit to explain.
3. **Nothing about who is let in changes.**

## Consequences

- The next time vpn-1 rings the desktop and is not let in, the reason is on the screen, and the fault can be found from it.
- Tested one reason at a time (`tests/admitwhy.sh`), and the clean case end to end (`tests/meshask.sh`).
