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
| **Malicious line member** | everything a peer on a call can do, and also vouch for other nodes and evict any member: there is no owner and no quorum; offer itself as the way out to the internet, and then see the outside traffic of whoever goes out through it | read other members' sessions in transit; become another member's way out by claiming an address (the way out is a member's key: [ADR 0056](adr/0056-a-member-can-share-its-internet.md)). Any member can be evicted by any other, which is the trust a line implies. A line may instead name an operator ([ADR 0026](adr/0026-a-line-may-have-an-operator.md)): then members can neither admit nor evict, and the power sits in one key |
| **Line operator's key** (stolen or lost) | stolen: admit any node and evict any member of that line. Lost: no one new can join and no one can be evicted, while the line goes on | keep the operator's seed offline, on a machine that runs no node, with a backup. Rotating it means founding a new line. A lost or stolen *member* key: its admit ends by itself (at most 400 days, 90 by default; [ADR 0065](adr/0065-an-admit-has-an-end-and-a-revocation-does-not-outlive-it.md)), at once if the operator evicts it, and the operator's roll voids every admit made before it |
| **Attacker on a site's own LAN segment** | do anything a LAN host can; flood the segment; replay a recorded probe to keep a dead box looking alive for up to about 30 seconds after it was recorded | forge a probe or a host list (both are authenticated with the site secret); join the line. A *member* holds the line key and so the site secret: before 0.5.49 it could send a valid probe in over the line and put the site's other box in standby; the line's frames of that type are now dropped ([ADR 0063](adr/0063-inputs-are-bounded-by-the-way-they-are-written.md)) |

## Things to say plainly

- **A call is a cable into your LAN.** Accepting a number is like plugging a stranger's laptop into your switch. The
  first ring always asks, and the default answer policy is `ask`; `auto` removes that and is for headless boxes you
  have decided to trust.
- **The first call trusts the switchboard,** exactly as the first SSH connection trusts DNS. To remove even that,
  compare the eight-word **safety phrase** both sides show. A number's key is pinned once a call is up, so from then on a
  lying switchboard cannot substitute another. The phrase is committed to before it is known (the end with the lower key id commits, the other adds a number, the first reveals) and tied to the session, so a switchboard that is itself the attacker has no offline search and one
  online try per call at about 2^-64; dropping the exchange ends the call (there is no weaker phrase to fall back to), and the call is on the line while you compare ([SECURITY](SECURITY.md#the-safety-phrase-honestly)). Dialing a full address (`lw1…`) rather than a number is supported.
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

There are two privileged pieces, run through `pkexec` as three polkit actions that ask for an administrator's password by default: the jack helper is installed twice, as `/usr/lib/longwire/jack` (`org.longwire.jack`: the person's own device, its address and routes) and as `/usr/lib/longwire/jack-everything` (`org.longwire.everything`: also everything through the line and sharing the internet), and refuses the wider changes when run as `jack`, so allowing the device with no password does not allow rerouting the machine ([ADR 0058](adr/0058-the-device-and-the-whole-machine-are-two-permissions.md)). The second, `/usr/lib/longwire/site` ([ADR 0039](adr/0039-a-site-node-from-the-pane.md)), writes `/etc/longwire/NIC.conf` and enables `longwire@NIC`: it reads one line of options from a file the user owns, accepts only `--switchboard`, `--token`, `--name`, `--answer` and `--listed`, each checked (no spaces or marks that could carry a second option), refuses `lo`, jacks and cards that do not exist, and gives the user access to the service's control socket with an ACL. A person who can run it can start a site node on any real card, which is what the action is for; the node it starts is the unit's own, with only `CAP_NET_RAW`. The first, `/usr/lib/longwire/jack`, makes a tap called `lw0` to `lw99` owned by the user polkit names (never root) and brings it up at MTU 1400. It acts only on a jack that user owns (a
device of that name owned by anyone else is refused), and in the same step it may:
- put on an IPv4 host address with a /8 to /30 prefix;
- start `dhclient`, `dhcpcd` or `udhcpc` with a hook that drops the default route, the name servers and the rest;
- add 1 to 16 routes (/8 to /32; not loopback, link-local, multicast, or overlapping the jack's network) through a gateway on the jack's own network;
- with `--everything` (as `jack-everything` only), add the jack's own table and the rules 7700 and 7701, refusing if something else holds them or if the jack has carrier ([ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md));
- with `--everything`, set up to three name servers on the jack in systemd-resolved ([ADR 0047](adr/0047-names-through-the-line-are-set-on-the-jack.md));
- with `--share` (as `jack-everything` only), which needs a fixed address and `nft`, share the machine's internet with the line ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)): turn forwarding on for the jack and for the device the machine's own
  default route leaves by (that device's earlier setting is written in `/run/longwire/share-lwN`), and load an nftables table of the jack's own, `longwire-lwN`, that lets the jack's own network out
  through that device, masqueraded, lets only established and related traffic back, and drops anything else from or to the jack that is forwarded. Made again without `--share`, or with `--remove`, it deletes the table and
  puts the forwarding back as it was written.

`--remove` deletes only a jack the asking user owns, and stops a DHCP client only if its process is one of those three. polkit keeps an approval for a few minutes (`auth_admin_keep`), and in that time a
program running as the same user can run the helper of the action approved again without a prompt, so the checks above are the boundary. The routes, rules, forwarding and nftables table it adds affect every user of the machine. The node
itself still never runs as root and still holds no capability. Not tested: `pkexec` behind the desktop's pseudo-terminal job with a real polkit agent, and the helper on a machine with `/dev/net/tun`.

## Port mapping

A node asks its default gateway for a port mapping (NAT-PMP). Someone on the same LAN can answer in the gateway's
place and make the node offer a wrong address; the cost is a failed attempt, because every path still ends in the
authenticated handshake. A mapping also makes the node reachable by anyone who learns the address, which is what the
handshake's costs and cookies are for. `--nat-pmp off` turns it off.

## Everything through the line, roaming and names

[ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md), [0046](adr/0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md), [0047](adr/0047-names-through-the-line-are-set-on-the-jack.md). The safety net is the kernel
passing over the routes of a jack with no carrier, so the design fails open: when the node is off, crashed, or has let go after a change of network, everything (traffic for the line's own and routed
networks included) goes out on whatever network the machine is on, unencrypted. Between a change of road and the engine noticing it (up to a second), packets go into a line that cannot carry them. A
switch that blocks instead *(planned: M4)*. The network the machine sits on sees every name it looks up unless `dns` is set and systemd-resolved is present. The engine's own datagrams never enter the jack,
because every socket is tied to the real device; a program on the machine that ties its own socket to that device also bypasses the line, as it would with any tunnel.

**The exit** ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)). With everything through the line and no gateway, the jack's default route goes to 169.254.77.1, whose MAC the helper fixes
(`nud permanent`), so no ARP answer is ever asked or believed; the engine hands every frame for that MAC to the member chosen as the exit, and to nobody else, or drops it. A member is an exit only
while it says so (`EXIT`, every 10 s) inside its own session and is a current member of the line, so the way out is a member's key: a malicious member can offer itself as an exit, and is then
used only if it is the only one that offers or is the one named with `exit_through` (whose number must be pinned to its key here); with several offering and none named, none is used. It cannot take over another member's way out by claiming an address or answering ARP, nor win the choice with a forged low number: the choice no longer looks at numbers ([ADR 0064](adr/0064-a-members-record-is-a-claim-and-the-way-out-is-never-the-lowest-number.md)). Whoever is the exit sees all the outside
traffic of those who use it, as an internet provider does, and the internet sees its address: share only from a machine the line trusts that much, and name it with `exit_through` when more than one
member may offer. While no member offers, internet traffic stops rather than leaking to the local network; when the node stops, the fail-open above is unchanged. On the exit, the nftables table
keeps the line off the machine's other networks, but a firewall of its own whose forward policy drops (Docker's) comes first and may drop the line's traffic.

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
(2,000 a second on a switchboard). A session that has passed the handshake and keeps failing earns **strikes**: a refused registration counts 2, a malformed registration 1, a dial of a number that does not exist 1, a dial over the limit 1, a control message over the session's budget 0.25; they leak away over `--strike-window`. A registration repeated within 5 s on one session is ignored, not struck (since 0.5.28), so a node whose answer was slow does not strike itself, and a refusal because the policy script gave no verdict within 10 s is the switchboard's own doing and earns none (since 0.5.41). At `--strikes` (default 10 in 600 s) the **key is banned** for `--ban-for` (900 s, doubling at each repeat up to `--ban-max`, 86400 s), at most `--ban-ceiling` (1000) bans an hour. Its live session is ended once with "temporarily refused: too many failures"; after that its handshakes are dropped after the same work as an unknown key's, with no reply, and it is never told how long the ban lasts. The echo port still answers it, so a node that knows it has the right key can tell it is being ignored (which is what `switchboard-silent` reports, [ADR 0048](adr/0048-a-silent-switchboard-is-said-so.md)). `--net-ban-keys` (5) banned keys from one /24 or /64 refuse new identities from that network for `--net-ban-for` (900 s); registered nodes behind it are unaffected. Nothing before the handshake counts, because a spoofed source could then get others banned ([ADR 0043](adr/0043-strikes-and-bans-count-only-what-cannot-be-faked.md)). Bans live in memory; the switchboard's log records each banned key's first 16 hex digits and no address. Open registration can be used up to `--max-numbers` by anyone with enough addresses: a switchboard
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
- **Not run:** the services under a live systemd, a clean install of each package, a long soak, real routers and wide-area links, everything through the line on a real tap with real traffic, a real exit on the internet (sharing ran on the engine, `tests/exit.sh`, and in network namespaces on the real kernel, `tests/exitnet.sh`), a laptop really moving, a real systemd-resolved, a real DHCP lease, the helpers under a real `pkexec`, a long real attack on bans.
- **VLAN-tagged traffic through a splice box is neither handled nor tested** *(planned)*, and the jack port is untested.
  IPv6 has been tested only on loopback and between namespaces, not on a real network.

## Hardening in the code

- Parsers of untrusted input are few and small: handshake fragments (`engine/hs.c`), data packets (`engine/sess.c`),
  `HELLO` and `PUNCH` and the control messages including the safety phrase exchange (`engine/call.c`, `engine/board.c`), the switchboard's echo port (`engine/call.c`), and the frames read from the LAN
  (`engine/sw.c`). Fuzzing them continuously *(planned: M8)*.
- No allocation on the data path. Tables have fixed sizes: peers (32, or 1024 on a switchboard), MAC entries (8,192),
  handshake slots (512, one partial message per source address, found by a hash keyed with a salt drawn when the node starts), and four reassembly slots per peer, created on first use.
- Keys are handled in C only. The seed is mode 0600. `lw start` and `lw key` read it inside the module's process and
  wipe it before returning; the worker keeps its identity in memory that is `mlock`ed where the OS allows. It never
  crosses the control socket and never enters a hibr variable, so scripts handle numbers and decisions, never secrets. The control socket is mode 0600 and checks the peer's uid (`SO_PEERCRED`), unless it is given a group: the services give theirs to the group `longwire`, mode 0660, and then the file mode is the check, so everyone in that group may dial, hang up and evict on that node, and no one may read its key (stopping a service, and deciding a switchboard's registrations, stay with the user it runs as, and the event stream the group watches does not carry a registration's token or address) ([ADR 0032](adr/0032-the-services-share-their-control-socket-with-a-group.md)). The worker
  closes every descriptor it inherits.
- Nothing is setuid, and hibr never gains privilege (hibr ADR 0016). Splice gets `CAP_NET_RAW` from the service manager
  as an ambient capability. `lw start` opens the packet socket, clears every capability set and sets `no_new_privs`, and only
  then forks the worker. The unit that `longwire setup splice` prints names the module by absolute path
  (`LONGWIRE_MOD`), so the privileged moment never searches the environment for code.
