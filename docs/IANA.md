# The application to IANA for a service name and a port number

This is the text of the application for a **service name and a user port number** ([RFC 6335](https://www.rfc-editor.org/rfc/rfc6335), the form at
<https://www.iana.org/form/ports-services>), ready to paste into the form, field by field. It was written against the form as it stands on 2026-11-20 (13 fields and 11 usage questions) and checked against the code and the other documents:
every statement below is in [PROTOCOL](PROTOCOL.md), [CRYPTO](CRYPTO.md), [SWITCHBOARD](SWITCHBOARD.md) or [ARCHITECTURE](ARCHITECTURE.md). It is the owner's to submit: the form asks who the assignee is. **Submitted by the owner on 2026-11-20**; the answer is awaited.

## Before submitting

1. **The assignee and the contact** are the owner's to give (name and e-mail for each): they are left as placeholders below. The assignee is the person or organization responsible for the assignment; the contact may change it afterwards.
2. **A public document.** IANA reads the *Reference* field; it also expects the protocol described in a document the public can read. This repository is public and mirrored at <https://github.com/osakka/longwire>, so the
   specification has stable addresses: <https://github.com/osakka/longwire/blob/main/docs/PROTOCOL.md>, [.../CRYPTO.md](https://github.com/osakka/longwire/blob/main/docs/CRYPTO.md) and
   [.../SWITCHBOARD.md](https://github.com/osakka/longwire/blob/main/docs/SWITCHBOARD.md). Put them in the Reference field as well as the description below.
3. **The number.** The registry (downloaded 2026-11-20, last modified that day) has **no entry for UDP or TCP 6741, none for 9488, and none named `longwire`**, so the number the switchboard has used as its default all along is free to ask for. IANA decides, and says an unassigned number must not be
   used on the public Internet before assignment: if it assigns a different one, the default changes (ticket LW-15 in the [BACKLOG](BACKLOG.md)). Review is by Expert Review and "may take up to 1-2 months".
4. **One port.** IANA wants at most one assigned port per service. A switchboard also answers on a second port, its own plus 2747 (9488 for 6741), so that a node can tell how its NAT maps ports ([PROTOCOL §6](PROTOCOL.md)); that port is **derived by a fixed offset and is not asked for**. If IANA's
   number puts the derived one on top of an assignment, the offset is a constant that can change (a ticket would be cut for it).

## The form

| Field | Answer |
|---|---|
| Assignee Name | *the owner: an organization, company or person* |
| Assignee E-mail | *the owner* |
| Contact Name | *the owner, or whoever is to answer for it* |
| Contact E-mail | *likewise* |
| Resources required | **Port number and service name** |
| Transport Protocols | **UDP** (and no other) |
| Service Code | *(blank: it is for DCCP)* |
| Service Name | `longwire` (8 characters; letters only; it contains neither "service" nor "protocol") |
| Desired Port Number | `6741` (IANA's choice if it is unsuitable) |
| Description | `Rendezvous for longwire nodes: registers a node under a number, keeps its presence, and introduces two nodes to each other before they exchange encrypted Ethernet frames directly.` |
| Reference | the text under **Reference** below, and the public address of the specification: <https://github.com/osakka/longwire/blob/main/docs/PROTOCOL.md> |
| Defined TXT keys | *none* (the name is not used for DNS-SD) |

## The usage questions

1. **Broadcast or multicast?** None at the IP layer: every datagram is unicast UDP. (A site's boxes announce themselves to each other on their *own LAN segment* with a link-local Ethernet frame of EtherType `0x88B5`; it is consumed there and never leaves the segment, and never carries IP.) Frames of the bridged
   network that are broadcast on a LAN are carried as unicast datagrams to each peer, within a budget (below).
2. **UDP: how is traffic limited, and does it react to congestion?** The protocol carries *other* protocols' traffic (Ethernet frames, which carry TCP and the rest) and adds no congestion control of its own; it relies on the encapsulated traffic's own, as RFC 8085 allows for a tunnel
   (§3.1.11). What it generates itself is bounded: a keepalive after 25 s of silence; handshake retransmission at 1, 2, 4, 8 s then every 8 s; a handshake budget of 50 a second on a node and 2000 on a switchboard, beyond which a responder asks for a cookie (proof the source can receive replies) before doing any
   asymmetric work; one unfinished handshake message buffered per source address, for one second; frames flooded to a peer (broadcast, or a destination whose address is unknown) limited to 1000 a second after a burst of 200, per peer; a relayed route limited to about 10,000 datagrams a second; a `HELLO` answer (1.25 KB for a 10-byte request) limited to 20 a second
   in total and 2 a second from one address; the switchboard's second port answers with fewer bytes than it is sent (no amplification); registration, dialling and directory lookups are limited per source address, per caller and per callee, and a source that keeps breaking them has its key banned. It does not slow down in response to loss.
3. **UDP: solely for the discovery of hosts?** The port is for the switchboard: it registers a node, keeps its presence, answers "where is this number", and introduces two nodes (their candidate addresses) so that they can connect directly; it carries **no user data**. Nodes' own sessions, which carry the frames, use ports the node chooses.
   So it is rendezvous and introduction, not only discovery.
4. **Versioning.** The first byte of every datagram is its type, and anything unrecognised is dropped and counted; control messages inside a session carry a message number and a length, and an unknown message or an unknown optional part (a tag-length-value) is skipped. A node's public identity
   ("bundle") starts with a version byte (`0x01`) that names its key and cipher suite, and every key derivation is labelled with its version. A new suite or message is added by number and ignored by older nodes; there is no negotiation field to downgrade.
5. **More than one transport?** No: UDP only.
6. **Security.** Every session is mutually authenticated and encrypted: a Noise-IK pattern with a hybrid key exchange (X25519 and ML-KEM-768, so it holds against a quantum computer too), ChaCha20-Poly1305 with a 4096-packet replay window, BLAKE2b, Ed25519 signatures; keys are replaced every two minutes
   (ten with a switchboard). A node is its key: peers are authenticated and pinned by it, and a call is confirmed by eight words both ends read aloud. Nothing before the handshake is believed or answered at length; a handshake that does not prove knowledge of the responder's key id is discarded before a byte
   is buffered; a switchboard sees who is online and who calls whom, never what is carried ([CRYPTO](CRYPTO.md), [SECURITY](SECURITY.md)).
7. **Why a unique port.** A switchboard is reached by an address its operator gives to the people who use it, and it must be reachable through firewalls and provider rules that allow a UDP port by number: a registered default means one rule, and a client that is given only a host name knows where to knock. A port in the dynamic range
   would be one a host's own outgoing connections may be using; an existing assignment would mean sharing it with a different service.
8. **State of development.** In use: the implementation (a C library and its command-line tools, a desktop interface, packages for three Linux families, and an Android application) is at version 0.5 and the wire protocol is documented and tested (a full suite including a real-kernel network test); it is not an IETF draft.
9. **SCTP**, 10. **SCTP capability**: not applicable.
11. **Anything else.** It is not a VPN to a server: it makes two or more sites one Ethernet network and a switchboard only introduces them, then takes no part (the nodes connect directly, through NAT, and relay through a member of the network only when that is not possible).

## Reference

A *switchboard* is a UDP endpoint that nodes register with and call through; nodes talk to each other directly. A node's identity is a 1249-byte bundle (version byte `0x01`, X25519 and ML-KEM-768 and Ed25519 public keys); its key id is the BLAKE2b-256 hash of it, shown as `lw1` and 52 base32 characters.

**Datagrams.** The first byte is the type: `0x01 HS_INIT` and `0x02 HS_RESP` (a two-message handshake, each sent as two fragments, authenticated by a keyed hash of the responder's key id), `0x03 COOKIE` (reply to a handshake sent under load), `0x04 DATA` (`index:3 ‖ counter:8 ‖ ciphertext ‖ tag:16`,
ChaCha20-Poly1305, nonce from the counter, 4096-packet replay window), `0x05 RELAY` (a datagram carried by a member), `0x06 PUNCH` (opens NAT mappings), `0x07 ECHO` (asks the switchboard's second port for the sender's mapped address), `0x08 HELLO` (fetch or offer a bundle by key id). Integers are little-endian except where stated.

**Session messages.** Inside `DATA` the first plaintext byte is a kind: an Ethernet frame (up to 1428 bytes in one datagram; two parts above that), a keepalive, or a control message `msg:1 ‖ len:2 ‖ payload`.

**With a switchboard** (a session like any other, over the registered port; messages `0x20` to `0x2f`): `REGISTER` (a token, a name, whether to list it) is answered by `NUMBER` (a nine-digit number, the sender's observed address, flags) or `REFUSED`; `HEARTBEAT` (candidate addresses) by `HBACK`; `DIAL` (a number and
candidates) by `RING` to the callee (the caller's number, bundle and candidates); `ANSWER` (accept, reject, busy) from the callee; `GO` to both (a role, a start delay, a one-time token, the other's bundle and candidates) or `REJECTED` (unknown, not online, declined, busy, refused by policy, no answer); `LOOKUP` and `FOUND` (a name or a key id against nodes that
chose to be listed); `SITEINFO` (a box's role in its site group, for the operator's display). After `GO` the two nodes send `PUNCH` datagrams to each other's candidates and run the handshake directly; the switchboard is no longer involved.

**Sequence of a call.** `REGISTER`, `NUMBER`; the caller's `DIAL`; the switchboard's `RING` to the callee; `ANSWER`; `GO` to both; both `PUNCH`; the handshake between the nodes (4 datagrams); `DATA` carrying frames and control messages (a safety phrase committed and revealed, line membership, relay requests).

The full definition is in [PROTOCOL](PROTOCOL.md) (datagram and message formats, timers, NAT traversal, relaying), [CRYPTO](CRYPTO.md) (key derivation, the handshake, the cipher suite) and [SWITCHBOARD](SWITCHBOARD.md) (the messages above and the policy a switchboard applies).

## After the answer

- **Approved with 6741:** record the assignment here and in [PROTOCOL](PROTOCOL.md) (the registry entry as the source of the default) and remove "proposed" wherever the number is described.
- **Approved with another number:** change the default in the switchboard script and in every document and test that names 6741 (and the echo port that follows from it), one release, and say so in the CHANGELOG.
- **Refused or asked to change:** answer in the form; the ticket stays open.
