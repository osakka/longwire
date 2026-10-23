# Threat model

## What longwire is protecting

1. **Confidentiality and integrity of every frame** on the line, against anyone on the path, both today and for a
   future quantum attacker who recorded it.
2. **Admission.** Only nodes that a line member has picked up, or admitted, ever reach your LAN.
3. **Identity binding.** Once you've talked to a number, nobody can impersonate it to you. That includes the
   switchboard.
4. **Availability of the data path,** independent of the switchboard. Established calls survive the switchboard
   going down.

## Adversaries

| Adversary | Can | Cannot |
|---|---|---|
| **Passive network observer** (ISP, Wi-Fi, state), quantum-capable later | see that two IPs exchange UDP, plus sizes and timing | read frames, learn the initiator's identity, or decrypt recordings later (hybrid KEM) |
| **Active network attacker** | drop, delay, replay or inject packets; flood handshakes | forge or replay data (AEAD + replay window), complete a handshake without the keys, or make a responder do public-key work without `mac1` |
| **Malicious or compromised switchboard** | see presence, IPs and who dials whom; refuse service; on a *first* call, hand out the wrong key for a number | read or alter traffic; impersonate a number you've already called (the key is pinned); join a line (a member must pick up) |
| **Number squatter** | register numbers | take an existing number (numbers are bound to keys, first come). A *released* and re-assigned number shows up as a key mismatch, like SSH's changed-host-key warning |
| **Malicious line member** | everything a host on your LAN can do: ARP spoofing, DHCP, scanning | read other members' pairwise sessions *in transit* (it can see broadcast/flood traffic that reaches it, as any LAN host would) |
| **Malicious relay** (a line member) | see sizes and timing of relayed traffic; drop it | read or alter it: relayed packets are end-to-end A↔B sessions |
| **Stolen device** | everything that device's key could do | survive eviction: `longwire evict <number>` cuts it off and rotates the line key |

## Things to say plainly

- **A line is a cable into your LAN.** Admitting a number is like plugging a stranger's laptop into your switch. The
  first ring always asks, and the default answer policy is `ask`.
- **The first call trusts the switchboard,** exactly as the first SSH connection trusts DNS. To remove even that,
  dial the full address (`longwire dial lw1q…`) or compare the 6-word **safety phrase** both sides show on first
  pickup.
- **What the switchboard sees:** that you're online, your public IP and port, which numbers dial which, and when.
  On the default public switchboard, that's its operator, the longwire project. If that metadata matters, run your
  own switchboard; it's one more script in the same install (`longwire-switchboard`). See
  [SWITCHBOARD](SWITCHBOARD.md#what-it-sees).
- **Out of scope for v1:** hiding *that* two sites talk (traffic analysis, padding, cover traffic), anonymity, and
  resisting a compromised endpoint OS.

## Known gaps today

- **No DoS cookies yet.** A flood of well-formed handshakes is *shed* by a budget (50/s on a node, 2000/s on a
  switchboard), so CPU stays bounded and live sessions are unharmed, but an attacker who knows a node's key ID can use
  the same budget to starve legitimate new handshakes. Cookies fix that and are required before the public switchboard
  goes live ([ROADMAP](ROADMAP.md#m1-what-exists)).
- **Two strict NATs cannot connect yet** (hard-NAT traversal and relay are M3); the call fails with a plain message.
- **HELLO answers are limited globally,** not per source.

## Hardening built into the design

- One parser for untrusted input (`engine/wire.c`), fuzzed continuously (libFuzzer/AFL++ with the gcc/clang build).
- No allocation on the data path. Fixed-size tables with explicit limits: peers, MAC entries (default 8,192),
  reassembly slots (64) and pending handshakes per source (1).
- Keys live in one place: the C worker. The seed is mode 0600 and loaded into worker memory that's `mlock`ed where
  the OS allows. It never crosses the control socket and never enters a hibr variable, so scripts handle numbers
  and decisions, never secrets. The control socket is mode 0600 and checks the peer's uid
  (`SO_PEERCRED` / `getpeereid`).
- Nothing is setuid, and hibr never gains privilege (hibr ADR 0016). Splice gets `CAP_NET_RAW` from the service
  manager as an ambient capability. `lw start` is the first thing the node script does, and it clears every
  capability set in both processes once the packet socket is open. The scripts load the module by explicit path, so
  the privileged moment never searches the environment for code.
