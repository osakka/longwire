# 0033 — A switchboard is built for strangers

Status: accepted. Amends [0032](0032-the-services-share-their-control-socket-with-a-group.md) (the restart policy) and extends
the threat model for a switchboard on the open internet.

## Context

A public switchboard accepts a handshake from anyone, and a completed handshake costs the stranger one key pair. Everything a
stranger can then say to it is the attack surface of a service the project will run for others. Before the first release that
puts it online we attacked that surface on purpose, three ways: a line-by-line read of every parser a stranger's bytes reach (three
independent readers, each with a region), a fuzzer that runs the real engine under TCC's bounds checker, and a test for each
defect found. This record is what that turned up, what was done, and what is still not known.

"Safe" is not something a test shows. What we can say is which attacks were tried, which worked, which no longer do, and which
were not tried.

## What was found, and what changed

Found by reading. Items 1 to 3 were then **reproduced** with a test that fails on the old code and passes on the new; the rest were fixed on the strength of the reading alone, and the fuzzer shows they do no harm, not that they were harmful:

1. **Strangers could fill a switchboard's places for good.** A throwaway identity completes a handshake and says nothing; its
   half-made session was never expired and never recycled. 24 of them filled a 24-place table and the next honest node was
   refused; on the default 1,024 places the cost is minutes from one address (the cookie limit allows two handshakes a second). *Now:* an unconfirmed session lapses after
   10 s; when the table is full a session that has never registered gives way (the longest silent first), and a registered one never
   does. The places are still all taken by strangers in the test, and the honest node gets one.
2. **A registration token could write into the event the policy script reads.** The token went into the `register` event
   unescaped, and `hibr`'s `json parse` takes the last of two equal keys: a token of `","ip":"9.9.9.9","id":7,"x":"` replaced the
   address the per-address limit counted and the id the script answered. *Now:* a token is visible ASCII (no spaces, no control
   characters) or the registration is refused; and it is escaped anyway. The event also carries `net`, the key the limits use.
3. **A switchboard could connect a call nobody answered.** `RING` then `GO` moved the call to the handshake whether or not the
   person (or the policy) had picked up, against what the threat model says. *Now:* a ringing call connects only after it was
   answered.
4. **Numbers and refusal text from a switchboard reached JSON and the pin file unchecked.** A number is now nine digits or the
   message is dropped; the refusal text is plain ASCII and escaped.
5. **The pending-decision table (32) and the call table (32) were global and one session could fill either.** Now one decision
   pending per asker and kind, one outstanding call per caller, 256 calls.
6. **A flood of introductions could push out an honest node's** (16 places for 10 s, so a few hundred packets a second). The table
   is 1,024 places found by the key id (a hash of the bundle, which nobody can aim at another's), two to try.
7. **Handshake fragments waited in 8 slots** that spoofed fragments from nine sources could churn. 512 slots, found by a keyed
   hash of the source; an answer (`HS_RESP`) is given a slot only if we have a handshake out with that id.
8. **The per-address limit for cookie holders** used an unkeyed 64-bucket hash an attacker with two addresses could collide. It is
   keyed per boot, 512 buckets, and a busy bucket is shared, not reset.
9. **A cookie reply never reached an IPv6 source** (it always used the IPv4 socket), so IPv6 initiators could not complete a handshake
   while the node was under load.
10. **A datagram's owner was found by walking every peer** (1,024 on a switchboard), so a stream of garbage cost the most where it
    matters. The session index now carries the peer's slot in its low bits and a lookup is one step.
11. **A stranger's session could send frames to a switchboard**, which has no LAN; and could flood the script with
    `dhcp-conflict` events. Frames are dropped on a switchboard, and the event is at most one in thirty seconds.
12. **No limit on what one session could say.** Now 10 control messages a second (burst 30) per session; one registration in five
    seconds (each is a write to disk); a dial every two seconds (burst 12) whatever came of it, so numbers are not found by trying
    them.
13. **The registry had no end and was walked for every message.** `--max-numbers` (default 100,000), a hash index by key and by
    number, and allocation failures that refuse instead of crashing.
14. **The rate limits of the script** counted an IPv6 address, not its /64, so a /64 owner had no limit; its counters were never
    emptied; an option with its value missing (`--tokens` last) silently meant "no tokens"; the token file was rewritten in place.
    *Now:* limits key on `net`; counters are emptied every hour and day and when large; a missing value or a bad number is an error;
    the file is written beside and moved into place; files are made private from the first byte (`umask 077`).
15. **Smaller:** the unknown-initiator path stopped before a Diffie-Hellman that the known one does (a timing difference that told
    whether a key was a peer), and now does it too; `HS_RESP` draws on the handshake budget; a directory query with a NUL could dodge the 3-character
    minimum; `FOUND` for a key query read bytes it was not sent; `name_ok` accepted overlong forms, surrogates and text that
    moves or hides other text; pins are written whole and moved into place; an event line is never cut short; the control
    socket waits 25 ms, not 200 ms, for a silent client; a signed shift in the address parser.

16. **A member of the group could do the owner's work.** `reply` (accepting or refusing a registration or a dial, choosing the
    number) and `stop` were open to anyone in the group, and the event stream the group watches carried each registration's token and
    address. Now both are the owner's alone, and `register` and `dial` events go only to the owner's watcher; a group member sees
    the call events, which hold numbers only. Tested with three real users (`tests/group.sh`).
17. **Line members' numbers** (from `LINE` and `MEMBER`, and the saved line file) went unchecked into events and into the file,
    where a number holding a newline could write a line of its own. Now nine digits or the member is dropped. The record of
    evictions was overwritten at its first place when full, so thirty-two evictions of no one let an evicted node pass again; it is
    now a queue. A managed line took any admit its founder signed for anyone, against ADR 0026; now only the operator's, and the
    founder's own. A node given an operator's admit joins only that operator's line.
18. **Smaller still:** a candidate address of port 0, "this host", multicast, the reserved block or the unspecified IPv6 address is
    skipped (no call has a reason to send there); a site key that is named and cannot be used stops the start (two boxes each
    believing they are alone forward every frame twice); a crash leaves no core file (`RLIMIT_CORE`, `LimitCORE=0`); `lw sb reply`
    with a very long argument wrote past its buffer (local only); the script keeps comments in the token file and never treats one
    as a token, quotes the number it passes on, limits rings per caller as well as per callee, and refuses an empty option value or
    one that is another option; `longwire up --answer` with nothing after it no longer loops forever.

**The restart policy** (the question left open in 0032). A group member can stop the worker over its socket and the units said
`Restart=on-failure`; on a service that faces the internet a local stop that stays stopped is a denial of service. Both units now
say `Restart=always`, so that is only a bounce, and `systemctl stop` still stops. This was decided for the owner on that reasoning and
is one word to reverse.

**The units** gained `ProtectKernelModules/Logs`, `ProtectClock`, `ProtectHostname`, `ProtectProc=invisible`, `ProcSubset=pid`,
`RestrictNamespaces/Realtime/SUIDSGID`, `LockPersonality`, `SystemCallArchitectures=native`, `UMask=0077`, `LimitNOFILE`,
`MemoryMax`, `TasksMax`, and `SystemCallFilter=@system-service` with `SystemCallErrorNumber=EPERM`. The switchboard dropped
`AF_NETLINK`: listing interface addresses is for a node's candidates. The filter was checked against a trace: 61 distinct system
calls from a node, a switchboard and `hibr` itself in a run of the names test are all in `@system-service` as this machine's
`systemd-analyze` expands it. `MemoryDenyWriteExecute` is not set, because nothing shows `hibr` runs without writable code pages.

## How it was checked

- **Fuzzing, under TCC's own bounds checker** (`tests/fuzz.c`, `tests/fuzz.sh`; `tcc -b -g`, no other compiler). The engine runs
  in one process as a switchboard and a node, and the fuzzer feeds the receive paths: raw datagrams of every type and length from
  many sources; a real handshake captured off the wire and bent (flipped bytes, cuts, repeats); and a stranger's *encrypted*
  session (the fuzzer holds both ends of the keys) carrying control messages, split frames and Ethernet frames. A bounds error
  stops the run and prints the input. To know a clean run means something, a planted off-by-one in the candidate count was found in
  374 iterations (half a second). Before the fixes above: 4 seeds of 150,000 iterations per stage were clean. After: `make check`
  runs two seeds of 12,000 and the three regressions.
- **The suite caught one of our own fixes.** A limit of one outstanding call per node stopped a node from building a full line (it
  dials its members together); `tests/m3.sh` failed on it, repeatably, and the limit is now the size of a line. Fixes to a
  service are tested like the service.
- **The crypto, against other implementations** (`tests/kat.sh`), as above.
- **Three regressions** that fail on the old code and pass on the new: places held by strangers, a token writing into the event, a
  call connected before an answer. Each was run against a copy with the fix taken out.

## What is not known

Said plainly, because "100 %" is not on offer:

- **The handshake has had no formal analysis and no outside review** *(planned: M8)*. The vendored crypto built by TCC was
  checked against independent implementations (`tests/kat.sh`: 2,100 outputs of X25519, keyed BLAKE2b, ChaCha20-Poly1305 and Ed25519
  against Python's `hashlib` and `cryptography`, no differences), so a miscompilation of those four would show. **ML-KEM was not**: no
  second implementation or published vector set is on the machine, so the only evidence it is the standard algorithm is that two
  nodes built from it agree with each other. And nothing checks the constant-time behaviour of any of it as TCC compiles it.
- **The fuzzer covers what it generates.** It does not reach a valid line operator's signatures, a real relay's second hop, or the
  state files' parsers; and it runs the engine in one process, not under the kernel's UDP stack.
- **Open registration cannot be made abuse-proof.** A botnet can register every number up to `--max-numbers`. `--tokens` is the
  lever, and a public switchboard should use it (SWITCHBOARD). A flood of valid-looking handshakes from many real addresses costs a
  KEM decapsulation each, within the budget; the budget is a limit, not a defence.
- **Plain bandwidth floods** are for the network in front of it.
- **Not run:** the units under a live systemd, a clean install of each package, the services under a firewall, real routers and
  wide-area links, a long soak.
- **Residue noted, not changed** (all need a position the model already gives the attacker, or a spoofing-friendly network): a
  counterparty who knows a call's token can send one forged `PUNCH` that makes the caller send its handshake (about 12 kB over the
  retries) to a third party; a member of a line can send relay requests that fill a relay's 64 routes for a minute, or an
  unsolicited relay offer that delays a direct path by a minute; a counterparty's candidate addresses (the global ones) make a node
  send a few hundred small datagrams at someone for the length of a call; up to eight watchers share a worker's event socket, and a
  group member can hold them; the unit `longwire setup splice` prints for a person is not sandboxed. An off-path attacker who can spoof one initiator's address can poison that handshake's second
  fragment (it retries with a new id); relay routes are authenticated by their address only; the introduction (`HELLO`) request
  of 10 bytes draws 1,251, capped at 20 replies a second in all; a member of a line can send a relay offer nobody asked for.

## Consequences

- A switchboard's capacity is stated: 1,024 sessions (`--max-nodes`), 100,000 numbers (`--max-numbers`), and 256 calls; past them it
  refuses, it does not fall over.
- Older nodes (0.3.x) work against a 0.4.0 switchboard; nothing on the wire changed. The index a peer is given for its session
  is opaque to it.
- Every future parser gets a stage in the fuzzer.
