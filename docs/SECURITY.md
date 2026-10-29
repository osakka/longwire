# Security: what longwire protects, how far you can trust it, and what it does not do

This is the plain-language version. The engineering detail is in [CRYPTO](CRYPTO.md) (the primitives and the handshake),
[PROTOCOL](PROTOCOL.md) (the wire), [THREAT-MODEL](THREAT-MODEL.md) (adversaries and gaps) and the decision records
([0004](adr/0004-a-node-never-runs-as-root.md), [0005](adr/0005-standard-primitives-and-a-hybrid-handshake.md),
[0009](adr/0009-the-first-ring-asks-and-a-key-is-pinned-once-the-call-is-up.md),
[0033](adr/0033-a-switchboard-is-built-for-strangers.md)).

## The short answer

**longwire is designed to be hard to attack.** Its key exchange is built so that a quantum computer would have to break ML-KEM-768 as
well as X25519 to read traffic recorded today. It runs without root, lets in only the nodes you picked up, or that a member of your line
vouched for (or that the line's operator signed for), and has been attacked on purpose by its author, with the real flaws that turned up
fixed.

**It is not "ultra safe", and nobody should say that of it yet.** It has not had an independent security review. Its handshake
is a new *combination* of standard parts that has not been formally analysed. Some of the checking that was meant to be done has
not been done, and this page lists it. If your life or livelihood depends on a VPN, wait for a reviewed one, or run longwire
inside another layer you already trust.

## What it protects

| You want… | How longwire provides it |
|---|---|
| **Nobody on the internet can read your traffic** | Every frame is sealed with ChaCha20-Poly1305 (a standard authenticated cipher, 256-bit keys). The keys come from a handshake that no eavesdropper can reproduce. |
| **Nobody can change it or replay it** | Every packet carries an authentication tag; one that fails is dropped. A sliding window drops a replayed packet. |
| **A stolen key does not unlock yesterday's traffic** (forward secrecy) | The keys that seal traffic are made fresh by each handshake, renewed every two minutes, and erased. Learning your long-term key later does not recover them. |
| **Only the people you chose can join** | The first call from a number *asks you*, by default. A machine joins a line only if you picked it up, or a member of the line vouched for it (on a line with an operator, only the operator's signature admits). Every member is then trusted like a host on your LAN: see below. Tokens keep strangers off a switchboard. |
| **The other end is who it says it is** | Each side proves it holds a key that matches its number. After the first call that key is *pinned*: if the same number ever answers with a different key, the call is dropped and you are warned (`KEY CHANGED`), like SSH's changed-host-key warning. |
| **You can check it yourself** | Both ends show the same six-word *safety phrase*, derived from both keys. Compare it on another channel as soon as the call connects. A match is good evidence nobody is between you, with limits that matter: [see below](#the-safety-phrase-honestly). |
| **A hostile server cannot read your traffic** | The switchboard introduces two nodes and steps aside. It never has a frame, a seed or a private key, and it cannot make you accept a call. It *can* give a node the wrong key for a number on a call you have never made before; once a key is pinned, or the phrase compared, it cannot ([below](#the-safety-phrase-honestly)). |
| **Junk is cheap to drop** | A packet that does not prove it knows the receiver's public key id is dropped before any costly work, and under load only senders who can receive replies get work done for them. A flood from many real addresses still costs a key operation each, within a budget ([known residue](#known-residue)). |

## The safety phrase, honestly

The phrase is the one check a person can make that does not depend on trusting the software or the switchboard. It has limits, and they
are worth knowing:

- **It is short.** Six words from a list of 256: 48 bits, taken from a hash of the two key ids. There is no commitment step.
- **It is seen late by one side.** The person called sees it when the phone rings. The caller sees it only when the call connects,
  which is after the callee has picked up and the other machine is already on the line. So you compare *after* connecting, and **hang up
  at once if the words differ**.
- **A malicious switchboard has work to do, not an impossible job.** To sit between two nodes it must give each a substitute key of its
  own, and it chooses both. It only needs a pair that produces the same six words on both sides: a search of the order of 2^24 key pairs,
  not 2^48. That is within reach of a determined attacker with modest computing. So the phrase reliably catches interference by anyone
  else, and by a switchboard that does not go to that trouble; against a switchboard that is itself the attacker it raises the cost and
  does not make the attack impossible.
- **After the first call it is moot.** The key is pinned. If the same number later answers with another key, the call is dropped and you
  are warned.
- **If the switchboard is yours, none of this matters.** A longer phrase, committed to before it is shown, so that there is nothing to search offline, is designed ([ADR 0040](adr/0040-the-safety-phrase-is-committed-before-it-is-shown.md)) and planned *(planned: M8)*.

## Why the design is sound (as far as design can be)

- **Boring parts, no negotiation.** X25519, ML-KEM-768 (the NIST standard FIPS 203), ChaCha20-Poly1305, BLAKE2b, Ed25519. No TLS, no
  certificates, no cipher menu to downgrade. One suite, one version of the protocol.
- **Small.** The engine is plain C99 with nothing but libc. Less code is less to get wrong. It is built with one compiler (TCC)
  and its tests run under it ([ADR 0021](adr/0021-tcc-is-the-compiler-without-exception.md)).
- **Least privilege.** The node never runs as root. Bridging a whole site needs one capability, granted by the service manager and
  dropped as soon as the network port is open. A single machine uses a tap device it owns; the root helper that creates the device
  (and puts an IPv4 address on it, or starts a DHCP client that takes the address and route only) can do only that. A second helper, run through polkit from the Control Panel, starts a site node as a service; it reads the options from a file only the user can write and lets through five options with values of the shape each must have. The services run as a transient user with no home, a system-call filter and a memory limit; the switchboard holds no capabilities, and a site node only `CAP_NET_RAW`.
- **The secret never leaves the worker.** Your identity is one 32-byte seed. It is read inside the engine, never put in a shell
  variable, never sent over the control socket, locked in memory where the operating system allows it, and a crash writes no core file. Registration tokens are kept in a file only
  you can read, and the Control Panel and `longwire profile token` take one without putting it on a command line (which any user on the
  machine can read in the process list; the `token=` form of `profile set` and `up --token` do put it there, so prefer the other two).
- **Defence against floods.** Every handshake fragment must prove it knows the receiver's key before anything is stored; under load
  a cookie proves the sender can receive replies (WireGuard's idea, extended to every fragment). A public switchboard also limits
  what a stranger can hold, per session and per source.

## Quantum-safe: what that does and does not mean

A "quantum-safe" claim is only worth something if it says *which part*. This is the exact picture.

| Part | Safe against a future quantum computer? | Why |
|---|---|---|
| **Confidentiality of your traffic**, including traffic recorded today ("harvest now, decrypt later") | **Yes** | The handshake mixes X25519 with ML-KEM-768. An attacker has to break **both**, so a quantum computer that breaks X25519 still cannot recover the keys. |
| **Forward secrecy** | **Yes** | The ephemeral keys include an ML-KEM key, not only X25519. |
| **Hiding who is calling** | **Yes** | The initiator's identity is sealed under a key only the receiver can derive, protected by the ML-KEM part. |
| **Authenticating the two ends in the handshake** | **Yes** | Each end proves itself by decapsulating a message only it can read (ML-KEM), not by a signature. |
| **The cipher** (ChaCha20-Poly1305, 256-bit keys) | **Yes** | The best known quantum attack on a symmetric key only halves its strength; 256 bits leaves 128. |
| **The signatures that vouch for line membership** (admits, evictions, an operator's word) | **No** | These are Ed25519. A future quantum computer could forge them. A forged admit would get a stranger let in to a line without a prompt: it would then be a member, on your LAN, able to do what any host on a LAN can do ([below](#what-it-does-not-protect-against)), though it would still need a key of its own to complete a handshake and could not read sessions between other members. A forged eviction is only accepted from inside a handshake-authenticated member's session. A post-quantum signature (ML-DSA) is a later option. |

So: **the part that protects what you say is quantum-resistant; the part that vouches for who may join a line is not post-quantum.**
That is the honest sentence behind the word.

## What has been done to check it

- **A test suite that builds real networks.** It runs the actual scripts over unprivileged network namespaces with simulated
  NATs, carrying real ping, ARP, TCP and broadcasts between LANs (about 470 checks in a full run).
- **An attack on the switchboard,** the one component built to face the public. Before it was released, three readers went through every parser a stranger's bytes can
  reach. It found real flaws, three of them proven by tests that failed on the old code: strangers able to fill the switchboard's
  places for good, a registration token able to write into the policy script's event, and a switchboard able to connect a call nobody
  had answered. They are fixed ([ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md)).
- **A fuzzer under a bounds checker.** The real engine runs under TCC's own bounds checking against raw packets, a captured and
  corrupted handshake, and a stranger's encrypted session. It finds a planted bug in half a second, and found no memory errors in 600,000 rounds per stage before the fixes were made.
- **A cross-check of the crypto.** X25519, BLAKE2b, ChaCha20-Poly1305 and Ed25519, as TCC builds them, give the same answers as
  independent implementations on 2,100 random cases.

## What has not been done, and what that means for you

| Not done | Consequence |
|---|---|
| **No independent security review or audit** | Nobody outside has tried to break it. Treat every claim above as the author's. |
| **The handshake has no formal proof** (no Tamarin or ProVerif model) | The pieces are standard; the way they are combined is new, in the family of PQ-WireGuard and Rosenpass. A subtle flaw in the combination is possible. |
| **ML-KEM is not cross-checked** | There is no second implementation on the machine to compare against. It is the vendored reference code, and two nodes built from it agree with each other. |
| **Constant-time behaviour is unchecked** | The libraries are written to avoid timing leaks, but nobody has measured the code as TCC compiles it. |
| **The engine is written in C** | Memory-safety bugs are possible in any C code. The fuzzer and the bounds checker lower that risk; they do not remove it. The node runs unprivileged, which limits the damage. |
| **Real-world conditions are mostly untested** | The tests simulate NATs. Real routers, the wide-area internet, a real tap device, the services under a live systemd and a clean install from the packages have not been tried by the author. |

These gaps are tracked in [THREAT-MODEL](THREAT-MODEL.md) and the [ROADMAP](ROADMAP.md) (M8 is the verification milestone).

## Known residue

Smaller weaknesses that are known, with the position an attacker needs, and that are recorded in [ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md)
and [THREAT-MODEL](THREAT-MODEL.md):

- A counterparty who knows a call's token and can spoof addresses can send one forged packet that makes the caller send about 12 kB of handshake to a third party.
- A relay's routes are authenticated by address only; the traffic through them is still end-to-end encrypted, so this costs availability, not secrecy.
- An attacker who can spoof one initiator's address can poison that handshake attempt; it retries with a fresh identifier.
- A 10-byte request for a node's public bundle draws a 1,251-byte reply, capped at 20 a second per node (a reflection of about 25 KB/s).
- While a call is being set up, a node sends a burst of small packets at the addresses the other side offered, which can include a stranger's.
- Floods of handshakes from many *real* addresses cost a key operation each, within a budget; the budget limits, it does not defend.
- A pin is per switchboard: the same number on a different switchboard is a different pin.
- Anyone in the group `longwire` on a service machine can occupy half of its event watchers.

## What it does not protect against

- **Traffic analysis.** Someone watching the wire can see that two addresses exchange encrypted packets, and their sizes and
  timing. longwire does not hide *that* you talk, add padding or cover traffic, or give anonymity.
- **A compromised machine.** If an attacker controls an endpoint, they have your seed and your traffic. No VPN helps.
- **A line member you should not trust.** A call is a cable into your LAN. A member of your line can do what any host on your LAN
  can do: ARP spoofing, scanning, running a rogue DHCP server. longwire authenticates *who* joined; it does not make them
  well-behaved. Only vouch for machines you would plug into your switch.
- **What a switchboard learns.** Whoever runs it sees that you are online, your public address, who rings whom and when, and any
  name you chose to give it. If that matters, run your own ([SWITCHBOARD](SWITCHBOARD.md)).
- **Floods of plain traffic.** A switchboard or node behind a thin link can be filled with garbage by someone with enough
  bandwidth. That is a network problem.
- **Losing your seed.** The seed is your number. Anyone who copies it *is* you; anyone who loses the only copy loses the number.

## Using it safely

1. **Compare the safety phrase** on a first call, as soon as it connects, through another channel (a message, a phone call). If it differs, hang up at once ([why it matters](#the-safety-phrase-honestly)).
2. **Keep the answer policy at `ask` or `known`.** `auto` answers anyone and belongs on a headless machine you chose to trust.
3. **Back up your seed**, as 33 words (`longwire key export`), on paper or in a password manager. Do not share it.
4. **Run your own switchboard**, and give registration to people with tokens (`--tokens`). Open registration lets anyone take a number.
5. **Keep tokens private.** They live in `~/.config/longwire/profiles` (mode 0600). A token spent is useless; one unspent is a key to the door. Set one with `longwire profile token`, not on a command line.
6. **Vouch for few machines.** On a line with an operator, keep the operator's key offline on a machine that runs no node.
7. **Evict what you lose.** A stolen device can be evicted by any member of an ordinary line, or by the operator of one that has an operator; the line key changes.
8. **Add only trusted people to the group `longwire`.** A member can dial, hang up and evict through a service node's control socket (not stop it, and never read a key).
9. **Keep it updated.** `apt install --only-upgrade longwire`, and `catalogue update` for the scripts and the pane.
10. **Layer it where it matters.** Use end-to-end encryption (TLS, SSH) for what is truly sensitive. longwire protects the path between sites; it does not replace protecting the service.

## Reporting a problem

If you find a security flaw, please write to **osakka@gmail.com** with "longwire security" in the subject, and give the maintainer time to fix it before you
publish it. This is a personal address and the interim contact: a project address and a public home for reports come with the project's public home *(planned: M8)*.

What to expect, and what to send:

- **Send** what you saw and how to repeat it: the version (`longwire doctor` prints it), the command or packets that trigger it, and what happened. Please do not send tokens, seeds or the 33 words of a real identity.
- **Coordinated disclosure.** Please keep the details private for 90 days, or until a fixed release is published, whichever is first. A flaw being exploited now is the exception: say so, and it comes first.
- **A reply.** This is a small project with one maintainer, so a reply is best effort, not a promise of a time. If you hear nothing in two weeks, write again.
- **Credit.** A fix says what was found and, if you wish, by whom, in the [CHANGELOG](../CHANGELOG.md).
- **Not a flaw:** what this page already lists as not checked or not protected ([above](#what-has-not-been-done-and-what-that-means-for-you)). A better answer to those is welcome as an idea, in a normal message.
