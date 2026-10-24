# Threat model

What longwire defends against, what it does not, and what is still open. Designed-but-unbuilt defences carry
`*(planned: M3)*`.

## What longwire is protecting

1. **Confidentiality and integrity of every frame** on the line, against anyone on the path, both today and for a
   future quantum attacker who recorded it ([CRYPTO](CRYPTO.md)).
2. **Admission.** Only a node that you picked up (or, with the `auto` policy, chose to pick up for anyone), or that you
   configured with `--peer`, ever reaches your LAN.
3. **Identity binding.** Once you've been connected to a number, nobody can impersonate it to you. That includes the
   switchboard: the number is pinned to its key, and a different key drops the call.
4. **Availability of the data path,** independent of the switchboard. A call that is up survives the switchboard going
   down.

## Adversaries

| Adversary | Can | Cannot |
|---|---|---|
| **Passive network observer** (ISP, Wi-Fi, state), quantum-capable later | see that two addresses exchange UDP, plus sizes and timing | read frames, learn the initiator's identity, or decrypt recordings later (hybrid KEM) |
| **Active network attacker** | drop, delay, replay or inject packets; flood handshakes | forge or replay data (AEAD and the replay window), complete a handshake without the keys, or make a responder do public-key work without knowing its key id and, once it is under load, a cookie for the attacker's own address |
| **Malicious or compromised switchboard** (or, once it exists, the registry of switchboard codes) | see presence, addresses and who rings whom, and the names and listings nodes gave it; invent or change a name; hide or fake a directory entry; refuse service; on a *first* call, hand out the wrong key for a number | read or alter traffic; impersonate a number you have already been connected to (the key is pinned); make you accept a call (a ring asks, under the default policy) |
| **Name impersonator** | announce a name nobody has listed, or one only a claim; run a switchboard that lies | take a name another key has listed (listing or announcing it is refused), or be believed: a name is shown as a claim, never decides anything, and the number and the safety phrase identify a caller. Names nobody listed can be shared |
| **Number squatter** | register numbers | take an existing number: numbers are bound to keys, first come. A number unheard for a year is released and held back for another before it can be issued again, so a re-issued number is rare, and shows up as a key mismatch, like SSH's changed-host-key warning |
| **Malicious peer on a call** | everything a host on your LAN can do: ARP spoofing, DHCP, scanning | read other peers' sessions *in transit* (it sees what is flooded to it, as any LAN host would) |
| **Malicious relay** | see sizes and timing of the traffic it relays; drop it | read or alter it: relayed packets are end-to-end sessions, and a relay is only a member of the line. It can also refuse or stall the pair: they notice, ask another member, and keep trying for a direct path |
| **Stolen device** | be that device: the seed *is* the number and the key | outlast an eviction: any member can evict it (`longwire evict`), which removes it from every member's list, rotates the line key, and bars it from ringing back in. Its own number and key stay its own, so the identity itself is replaced to be rid of it elsewhere |
| **Malicious line member** | everything a peer on a call can do, and also vouch for other nodes and evict any member: there is no owner and no quorum | read other members' sessions in transit. Any member can be evicted by any other, which is the trust a line implies |
| **Attacker on a site's own LAN segment** | do anything a LAN host can; flood the segment; replay a recorded probe to keep a dead box looking alive for up to about 30 seconds after it was recorded | forge a probe or a host list (both are authenticated with the site secret); join the line |

## Things to say plainly

- **A call is a cable into your LAN.** Accepting a number is like plugging a stranger's laptop into your switch. The
  first ring always asks, and the default answer policy is `ask`; `auto` removes that and is for headless boxes you
  have decided to trust.
- **The first call trusts the switchboard,** exactly as the first SSH connection trusts DNS. To remove even that,
  compare the six-word **safety phrase** both sides show. A number's key is pinned once a call is up, so from then on a
  lying switchboard cannot substitute another. Dialing a full address (`lw1…`) rather than a number *(planned: M3)*.
- **What the switchboard sees:** that you're online, your public address and port, which numbers ring which, and when.
  On the default public switchboard, that's its operator, the longwire project. If that metadata matters, run your
  own switchboard; it's one more script in the same install (`longwire-switchboard`). See
  [SWITCHBOARD](SWITCHBOARD.md#what-it-sees).
- **Out of scope for v1:** hiding *that* two sites talk (traffic analysis, padding, cover traffic), anonymity, and
  resisting a compromised endpoint OS.

## Handshake floods

Every handshake costs a KEM decapsulation, so each node keeps a budget (50 a second; 2000 on a switchboard). Past half
of it the node is *under load* and stops doing handshake work for anyone who hasn't proven they can receive replies at
their source address: it answers a short `COOKIE` instead, and a real peer retries with it at once. A flood from
spoofed or unreachable addresses therefore costs the victim one small reply per packet and no public-key work, and a
legitimate handshake still completes during it (tested: during a sustained flood a peer completes at least two rekeys and the session never lapses). What remains: an
attacker with a *real* address can fetch a cookie, and is then limited to 2 handshakes a second from that address
(burst 8); and someone who can see the cookie reply on the path can replay it from the same address, as in WireGuard.
Cookies are keyed by address and port, so two nodes behind one NAT address each get their own.

## Known gaps today

- **Two strict NATs connect only through a relay,** a member of the line with a public address. A line with no such
  member splits at that pair and says so. A node can see its own NAT type (`nat` in status); port spray *(planned: M3)* would connect some of these pairs directly.
- **A crossed rekey might stall a link** until the dead-peer timer fires (76 s at the default keepalive). It is rare
  by construction and has not been reproduced; a crossed first handshake is tested and heals.
- **The handshake composition has had no formal analysis or external review,** and the vendored crypto has not been
  checked against known-answer vectors *(planned: M8)*.
- **VLAN-tagged traffic through a splice box is neither handled nor tested** *(planned)*, the underlay is IPv4 only
  *(planned: M3)*, and the jack port is untested.

## Hardening in the code

- Parsers of untrusted input are few and small: handshake fragments (`engine/hs.c`), data packets (`engine/sess.c`),
  `HELLO` and `PUNCH` and the control messages (`engine/call.c`, `engine/board.c`), and the frames read from the LAN
  (`engine/sw.c`). Fuzzing them continuously *(planned: M8)*.
- No allocation on the data path. Tables have fixed sizes: peers (16, or 1024 on a switchboard), MAC entries (8,192),
  handshake slots (8, one per source), and four reassembly slots per peer, created on first use.
- Keys are handled in C only. The seed is mode 0600. `lw start` and `lw key` read it inside the module's process and
  wipe it before returning; the worker keeps its identity in memory that is `mlock`ed where the OS allows. It never
  crosses the control socket and never enters a hibr variable, so scripts handle numbers and decisions, never secrets. The control socket is mode 0600 and checks the peer's uid (`SO_PEERCRED`). The worker
  closes every descriptor it inherits.
- Nothing is setuid, and hibr never gains privilege (hibr ADR 0016). Splice gets `CAP_NET_RAW` from the service manager
  as an ambient capability. `lw start` opens the packet socket, clears every capability set and sets `no_new_privs`, and only
  then forks the worker. The unit that `longwire setup splice` prints names the module by absolute path
  (`LONGWIRE_MOD`), so the privileged moment never searches the environment for code.
