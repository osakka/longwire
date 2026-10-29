# Threat model

The plain-language version, with what has and has not been checked, is [SECURITY](SECURITY.md).

What longwire defends against, what it does not, and what is still open. Designed-but-unbuilt defences carry
`*(planned: M3)*`.

## What longwire is protecting

1. **Confidentiality and integrity of every frame** on the line, against anyone on the path, both today and for a
   future quantum attacker who recorded it ([CRYPTO](CRYPTO.md)).
2. **Admission.** Only a node that you picked up (or, with the `auto` policy, chose to pick up for anyone), or that you
   configured with `--peer`, or that a member of your line has vouched for (or, on a line with an operator, that the operator has
   admitted), ever reaches your LAN.
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
| **Anonymous node on a switchboard** (anyone who can send a UDP packet and complete a handshake) | hold a session, register a number up to the limits, ring registered numbers up to the limits, send control messages up to a budget | fill a switchboard's places so that an honest node is refused (a session that never registered gives way), stuff a field of the event the policy script reads, make a node connect a call nobody answered, take another's number or name, or tell which numbers exist faster than a dial every two seconds ([ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md)) |
| **Number squatter** | register numbers | take an existing number: numbers are bound to keys, first come. A number unheard for a year is released and held back for another before it can be issued again, so a re-issued number is rare, and shows up as a key mismatch, like SSH's changed-host-key warning |
| **Malicious peer on a call** | everything a host on your LAN can do: ARP spoofing, DHCP, scanning | read other peers' sessions *in transit* (it sees what is flooded to it, as any LAN host would) |
| **Malicious relay** | see sizes and timing of the traffic it relays; drop it | read or alter it: relayed packets are end-to-end sessions, and a relay is only a member of the line. It can also refuse or stall the pair: they notice, ask another member, and keep trying for a direct path |
| **Stolen device** | be that device: the seed *is* the number and the key | outlast an eviction: any member can evict it (`longwire evict`), which removes it from every member's list, rotates the line key, and bars it from ringing back in. Its own number and key stay its own, so the identity itself is replaced to be rid of it elsewhere |
| **Malicious line member** | everything a peer on a call can do, and also vouch for other nodes and evict any member: there is no owner and no quorum | read other members' sessions in transit. Any member can be evicted by any other, which is the trust a line implies. A line may instead name an operator ([ADR 0026](adr/0026-a-line-may-have-an-operator.md)): then members can neither admit nor evict, and the power sits in one key |
| **Line operator's key** (stolen or lost) | stolen: admit any node and evict any member of that line. Lost: no one new can join and no one can be evicted, while the line goes on | keep the operator's seed offline, on a machine that runs no node, with a backup. Rotating it means founding a new line |
| **Attacker on a site's own LAN segment** | do anything a LAN host can; flood the segment; replay a recorded probe to keep a dead box looking alive for up to about 30 seconds after it was recorded | forge a probe or a host list (both are authenticated with the site secret); join the line |

## Things to say plainly

- **A call is a cable into your LAN.** Accepting a number is like plugging a stranger's laptop into your switch. The
  first ring always asks, and the default answer policy is `ask`; `auto` removes that and is for headless boxes you
  have decided to trust.
- **The first call trusts the switchboard,** exactly as the first SSH connection trusts DNS. To remove even that,
  compare the six-word **safety phrase** both sides show. A number's key is pinned once a call is up, so from then on a
  lying switchboard cannot substitute another. The phrase is 48 bits with no commitment, and the caller sees it only once the call is connected,
  so a switchboard that is itself the attacker can search for substitute keys that give matching words (of the order of 2^24 pairs): the phrase
  raises the cost of that attack and does not make it impossible ([SECURITY](SECURITY.md#the-safety-phrase-honestly)). Dialing a full address (`lw1…`) rather than a number is supported.
- **What the switchboard sees:** that you're online, your public address and port, which numbers ring which, and when.
  On a public switchboard, that's whoever runs it. If that metadata matters, run your
  own switchboard; it's one more script in the same install (`longwire-switchboard`). See
  [SWITCHBOARD](SWITCHBOARD.md#what-it-sees).
- **Out of scope for v1:** hiding *that* two sites talk (traffic analysis, padding, cover traffic), anonymity, and
  resisting a compromised endpoint OS.

## Saved connections and the jack helper

[ADR 0034](adr/0034-a-connection-is-a-profile.md). The profiles file (`~/.config/longwire/profiles`) holds registration **tokens**, so it is
mode 0600 in a 0700 directory, the Control Panel and `longwire profile token` take a token without putting it on a command line (standard input; the `token=` form of
`profile set` and `up --token` do put it there), and `show` and the pane print only its length. A token is an invitation, not an identity: it lets one machine register once, and the seed is stored elsewhere and never enters
the file. Anyone who can read your home directory can read your tokens, as they could read your SSH config.

There are two privileged pieces, both run through `pkexec` and both polkit actions that ask for an administrator's password. The second, `/usr/lib/longwire/site` ([ADR 0039](adr/0039-a-site-node-from-the-pane.md)), writes `/etc/longwire/NIC.conf` and enables `longwire@NIC`: it reads one line of options from a file the user owns, accepts only `--switchboard`, `--token`, `--name`, `--answer` and `--listed`, each checked (no spaces or marks that could carry a second option), refuses `lo`, jacks and cards that do not exist, and gives the user access to the service's control socket with an ACL. A person who can run it can start a site node on any real card, which is what the action is for; the node it starts is the unit's own, with only `CAP_NET_RAW`. The first is `/usr/lib/longwire/jack`, run through `pkexec` (polkit asks for an administrator's password once per
session). It can only create a tap device called `lw0` to `lw99`, owned by the user polkit says asked, and bring it up. The node
itself still never runs as root and still holds no capability. Not tested: `pkexec` behind the desktop's pseudo-terminal job with a real
polkit agent, and the helper on a machine with `/dev/net/tun`.

## Port mapping

A node asks its default gateway for a port mapping (NAT-PMP). Someone on the same LAN can answer in the gateway's
place and make the node offer a wrong address; the cost is a failed attempt, because every path still ends in the
authenticated handshake. A mapping also makes the node reachable by anyone who learns the address, which is what the
handshake's costs and cookies are for. `--nat-pmp off` turns it off.

## Handshake floods

Every handshake costs a KEM decapsulation, so each node keeps a budget (50 a second; 2000 on a switchboard). Past half
of it the node is *under load* and stops doing handshake work for anyone who hasn't proven they can receive replies at
their source address: it answers a short `COOKIE` instead, and a real peer retries with it at once. A flood from
spoofed or unreachable addresses therefore costs the victim one small reply per packet and no public-key work, and a
legitimate handshake still completes during it (tested: during a sustained flood a peer completes at least two rekeys and the session never lapses). What remains: an
attacker with a *real* address can fetch a cookie, and is then limited to 2 handshakes a second from that address
(burst 8); and someone who can see the cookie reply on the path can replay it from the same address, as in WireGuard.
Cookies are keyed by address and port, so two nodes behind one NAT address each get their own.

A node with a global IPv6 address has no NAT in front of it: its UDP port is reachable from anywhere, so a stranger can send
it handshake attempts as they could a public IPv4 address. The protections are the same (mac1 and cookies under load, a
per-source budget by /64), and a firewall may close the port ([ADR 0027](adr/0027-ipv6-is-a-second-socket.md)).

## A switchboard in public

The switchboard is the one component that must serve strangers. [ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md) lists what
was tried against it, what worked and was fixed, and what has not been tried. In short: every parser a stranger's bytes reach was
read; the engine runs under TCC's bounds checker against raw datagrams, a bent real handshake, and a stranger's encrypted session
(`tests/fuzz.sh`); and the defects found (strangers holding every place, a token writing into the script's event, a call connected
before an answer, a registry with no end and a walk for every message) each have a test that failed before the fix.

What limits remain are limits, not defences. A flood from many real addresses costs a key-encapsulation each, within the budget
(2,000 a second on a switchboard). Open registration can be used up to `--max-numbers` by anyone with enough addresses: a switchboard
that matters should hand out tokens. Raw bandwidth is for the network in front of it.

## Known gaps today

- **Two strict NATs connect only through a relay,** a member of the line with a public address. A line with no such
  member splits at that pair and says so. A node can see its own NAT type (`nat` in status); a strict NAT against an easy one is connected directly by port spray (the strict end opens 256 sockets and the other end probes random ports at 100 a second for about a quarter of a minute: a burst of traffic at a stranger's public address, only while a call between the two is being set up and only toward the address the switchboard gave).
- **A crossed rekey might stall a link** until the dead-peer timer fires (76 s at the default keepalive). It is rare
  by construction and has not been reproduced; a crossed first handshake is settled by a key-id tie-break and tested (`tests/start.sh`, `tests/crossed.sh`).
- **The handshake composition has had no formal analysis or external review,** and the vendored crypto has not been
  checked against published known-answer vectors *(planned: M8)*; X25519, BLAKE2b, ChaCha20-Poly1305 and Ed25519 as TCC builds them agree with
  independent implementations on 2,100 random cases (`tests/kat.sh`), ML-KEM was not cross-checked, and constant-time behaviour as TCC compiles it is unchecked. The fuzzer
  ([ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md)) finds memory errors in what it generates; it does not prove their absence.
- **Not run:** the services under a live systemd, a clean install of each package, a long soak, real routers and wide-area links.
- **VLAN-tagged traffic through a splice box is neither handled nor tested** *(planned)*, and the jack port is untested.
  IPv6 has been tested only on loopback and between namespaces, not on a real network.

## Hardening in the code

- Parsers of untrusted input are few and small: handshake fragments (`engine/hs.c`), data packets (`engine/sess.c`),
  `HELLO` and `PUNCH` and the control messages (`engine/call.c`, `engine/board.c`), and the frames read from the LAN
  (`engine/sw.c`). Fuzzing them continuously *(planned: M8)*.
- No allocation on the data path. Tables have fixed sizes: peers (32, or 1024 on a switchboard), MAC entries (8,192),
  handshake slots (8, one per source), and four reassembly slots per peer, created on first use.
- Keys are handled in C only. The seed is mode 0600. `lw start` and `lw key` read it inside the module's process and
  wipe it before returning; the worker keeps its identity in memory that is `mlock`ed where the OS allows. It never
  crosses the control socket and never enters a hibr variable, so scripts handle numbers and decisions, never secrets. The control socket is mode 0600 and checks the peer's uid (`SO_PEERCRED`), unless it is given a group: the services give theirs to the group `longwire`, mode 0660, and then the file mode is the check, so everyone in that group may dial, hang up and evict on that node, and no one may read its key (stopping a service, and deciding a switchboard's registrations, stay with the user it runs as, and the event stream the group watches does not carry a registration's token or address) ([ADR 0032](adr/0032-the-services-share-their-control-socket-with-a-group.md)). The worker
  closes every descriptor it inherits.
- Nothing is setuid, and hibr never gains privilege (hibr ADR 0016). Splice gets `CAP_NET_RAW` from the service manager
  as an ambient capability. `lw start` opens the packet socket, clears every capability set and sets `no_new_privs`, and only
  then forks the worker. The unit that `longwire setup splice` prints names the module by absolute path
  (`LONGWIRE_MOD`), so the privileged moment never searches the environment for code.
