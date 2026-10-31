# Roadmap

This page is status: what is built, what each coming milestone adds, and what is deliberately unscheduled. Everything
marked `*(planned: M3)*` anywhere in the documentation is listed here under that milestone, and `make check` fails if a
marker names a milestone that doesn't exist. What changed, and when, is in the [CHANGELOG](../CHANGELOG.md); why a thing
is the way it is, is in the [decisions](adr/README.md).

Each milestone ends with something you can *use*, and with tests that prove it.

| | Milestone | Status |
|---|---|---|
| **M0** | First frame: handshake, sessions, switch, module, node script | built |
| **M1** | Dial tone: switchboard, numbers, dial and ring, pins, NAT hole punching | built |
| **M2** | Splice: one subnet over two LANs, site groups, cookies | built |
| **M3** | Party line: lines of more than two nodes, relay, hard NATs, IPv6 | built, apart from PCP and relay candidates *(planned: M7)*; the independent doc review is done |
| **M4** | Desktop and distribution | planned |
| **M5** | No privilege, more Unix | planned |
| **M6** | Pocket: member mode, Android | planned |
| **M7** | Speed | planned |
| **M8** | 1.0: verification | planned |
| **M9** | Operator grade: replicas, federation, a switchboard that can run a service for others | proposed |

## Built

**M0, M1, M2 and M3** are built with TCC (and only TCC) and tested by `make check`: the hybrid handshake with
cookies, sessions, the learning switch, the hibr module and both scripts; numbers, rings, pins and hole punching through
a userspace NAT; splice carrying real ping, ARP, TCP and broadcasts between two LANs that are one subnet, and two boxes
on one segment with a failover, and a four-site line that builds its own mesh, evicts a member, and heals when one restarts. The test suites are described in [ADR 0019](adr/0019-the-tests-build-their-own-network.md).

What is *not* verified by those tests, plainly:
- **The jack port (a real tap) is untested.** The machine this was built on is an LXC container with no
  `/dev/net/tun`. First real-tap test, on a host that has one: `longwire setup jack`, then `longwire up --jack lw0`
  on two hosts behind different routers, and ping across.
- **Real consumer NAT routers and real wide-area links** have not been tried; the NAT is a model of their filtering
  (cone and symmetric).
- **End-to-end throughput** is measured only in the tests (about 50 Mbit/s over veth with a TCC build); the cipher alone
  is about 290 Mbit/s per core.
- **There is no default public switchboard** until its host and key are supplied: `--switchboard` is required.

## Coming

**M3: Party line** (in progress). Done when four sites, one behind carrier NAT on both ends, are all one L2 network, and a
relayed pair upgrades to direct once a path exists.
- *Built (stage 1):* lines of up to 16 nodes: joining by vouching, admits, the self-building mesh with no prompt, eviction
  with line-key rotation, persistence and rejoining, `longwire peers` and `evict`, a site secret derived from the line key,
  partial-mesh reporting ([ADR 0022](adr/0022-lines-form-by-vouching.md)). Tested with four spliced sites and no NAT.
- *Built (stage 2):* relaying through a line member and path upgrade ([PROTOCOL §5](PROTOCOL.md#5-relay-built-m3-stage-2),
  [ADR 0008](adr/0008-hard-nats-are-relayed-by-line-members.md)). Tested with two members behind symmetric NATs and a
  public third: frames cross the relay, then the NATs are loosened and the pair finds its own path.
- *Built:* NAT-PMP, offered as one more candidate (`mapped` in status, `--nat-pmp off`).
- *Built:* the `ECHO` exchange and NAT-type detection (`nat` in status, a plainer message when a call fails).
- *Built:* port spray: a strict NAT against an easy one connects directly (a strict end opens 256 sockets and the easy end probes; the socket that is hit becomes that peer's). Two strict NATs still need a relay.
- **The number format, before anything public exists** ([ADR 0024](adr/0024-a-full-number-is-a-switchboard-code-and-a-local-number.md)):
  a full number is a switchboard code and a local number (`+0731042 312-884-076`). *Built:* the derived code (a `0` and six digits), full
  numbers shown and typed everywhere, pins and line members that name their switchboard by key. *Registry built:* a switchboard registers with
  a registry (`longwire-switchboard --registry HOST:PORT/KEY`, or `none` for a private one; `--issue-codes` makes a switchboard
  the registry) and is given its short code in the prefix-free plan: `+1` for the named public switchboard, then `20`, `21`, and so on.
  *Waiting on the domain:* the project's own registry, which the default public switchboard will use. Once the default
  public switchboard issues a number the format is frozen, so this is a gate on M4. The registry is a second thing hosted on
  the project's domain, with the default public switchboard.
- *Built:* operator-managed lines ([ADR 0026](adr/0026-a-line-may-have-an-operator.md), amends [0022](adr/0022-lines-form-by-vouching.md)): a line may name an authority key whose signed admits and evictions every member accepts. Member vouching stays the default; status says `managed`.
- *Built:* candidates that carry their own length ([ADR 0025](adr/0025-every-candidate-carries-its-own-length.md)), so IPv6 can be added without a format change.
- *Built:* IPv6: the underlay, v6 candidates, and neighbour-discovery suppression ([ADR 0027](adr/0027-ipv6-is-a-second-socket.md))
- *Built:* a longer rekey for switchboard sessions (`--sb-rekey`, 600 s).
- *Built:* the warning when two DHCP servers share a line (`dhcp-conflict`).
- *Built:* a switchboard for strangers: limits per session and per source, a registry with an end, a fuzzer under TCC's bounds checker, hardened units ([ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md))
- *Built:* number release after a year unheard, then held back for a year (`--release-after`). *Built:* `longwire key export` and `import`

**M4: Desktop and distribution.** Done when, on a clean machine, you install the package, `catalogue install cli/longwire`,
and then dial and pick up entirely from the hibr desktop.
- *Built:* the control panel pane for the client (`control-panel/longwire.hibr`): status, dial, answer a ring with the safety phrase, hang up, the line and its members, evict, the answer policy ([ADR 0030](adr/0030-three-surfaces-one-engine.md)); drawing it is untested
- *Built:* saved connections ([ADR 0034](adr/0034-a-connection-is-a-profile.md)): profiles (switchboard, token, attach, interface, name), `longwire on` and `off` in the background, `up` with no arguments using the default profile, the jack made by a root helper through polkit, and the pane's profile form with a token field and Turn on / Turn off. Untested: the form drawn on a real desktop, `pkexec` behind the desktop's pty job, a real tap device
- *Built:* the services give their control sockets to a group, `longwire`, so the pane and the switchboard application work with a service node and a service switchboard ([ADR 0032](adr/0032-the-services-share-their-control-socket-with-a-group.md)); choosing among several service nodes *(planned: M4)*
- *Built:* bytes and bit rates, sent and received, next to each connection in the pane ([ADR 0031](adr/0031-a-connection-counts-the-frames-it-carries.md)); the switchboard shows none, by design
- *Built:* the application for the switchboard (`apps/switchboard.hibr`): fill and handshake gauges, the nodes with the names they gave, and the calls being set up as they happen; drawing it on a real desktop is untested
- *Built:* longwire on the menu bar and the Control Strip ([ADR 0035](adr/0035-longwire-on-the-menu-bar.md)): a telephone with the state, a ring that interrupts with the safety phrase, a menu (Turn On/Off, Pick up, Dial, who is connected and how fast, Settings), in one catalogue bundle with the pane. Untested: how it is drawn. The Blit telephone icons are waiting on Blit 0.2.44.
- *Built:* choosing among several nodes in the pane (the **Node** row); tokens made, listed and removed with `longwire-switchboard token`. *Built:* splice from the pane (`longwire site`, [ADR 0039](adr/0039-a-site-node-from-the-pane.md)). Tokens in the switchboard application on the desktop *(planned: M4)*
- *Built:* the module as a deb, an rpm and a pacman package (`make packages`, `tests/pkg.sh`), the launchers, and the two
  systemd services ([ADR 0028](adr/0028-one-deb-and-the-others-from-it.md)); the scripts declare their catalogue headers.
- *Built and published:* the packages in the signed hibr repositories (apt, dnf, pacman), and in hibr's catalogue the two commands under the `cli` kind
  ([ADR 0029](adr/0029-commands-are-their-own-catalogue-kind.md)), the pane and the switchboard app. Not yet tried: installing from those repositories on a clean machine.
- Homebrew *(planned: M5)*; the default public switchboard and the project's registry live *(planned: M4)*
- *Built:* `longwire` with no arguments, `longwire show` and `longwire watch` (no JSON); automatic choice of attach mode *(planned: M4)*
- *Built:* an address for the jack from the pane or the profile, put on by the same root helper that makes the device ([ADR 0036](adr/0036-the-jack-gets-its-address-from-the-profile.md)); routes to networks beyond the line through a gateway on it ([ADR 0044](adr/0044-routes-beyond-the-line-go-through-a-gateway-on-it.md)); a route for everything ("full tunnel") *(planned: M4)*; an address from a DHCP server on the line (`address = dhcp`: a DHCP client started by the same helper, without a default route or name servers, [ADR 0038](adr/0038-the-jack-may-ask-a-dhcp-server.md))

**M5: No privilege, more Unix.** Done when a user with no please anywhere joins a line with `longwire shell`.
- A Homebrew formula, once the module builds on macOS; `longwire shell` (user namespaces); FreeBSD and OpenBSD tap and BPF splice; macOS BPF splice; documented invocations
  for init systems other than systemd

**M6: Pocket.** Done when a phone on mobile data gets a lease from the home router and prints to the home printer.
- The member-mode adapter (a virtual MAC, a DHCPv4 client, ARP and NDP responders); the leaf role; the Android app
  (`VpnService` around `engine/` through JNI, the one piece outside hibr). First, confirm that TCC can build the engine
  as an Android library: [ADR 0021](adr/0021-tcc-is-the-compiler-without-exception.md) leaves no other compiler to use

**M7: Speed.** Done when the TCC build reaches 600 Mbit/s on x86-64.
- PCP as a further address, and public members of a line offered as relay candidates to callers outside it
- A `TPACKET_V3` receive ring, `recvmmsg` and `sendmmsg`, one worker per core, x86-64 assembly for ChaCha20 (arm64 once
  TCC's assembler is verified), an optional AEGIS-256 suite, real path-MTU probing, and a proper end-to-end benchmark

**M8: 1.0.** Done when an independent report has no open high-severity findings.
- A security contact at the project's own address and a public home for reports (today: a personal address and a written disclosure policy in [SECURITY](SECURITY.md#reporting-a-problem)); a longer safety phrase committed to before it is shown (today 48 bits, no commitment); a formal model of the handshake (Tamarin or ProVerif); known-answer tests for the vendored crypto (X25519, BLAKE2b, ChaCha20-Poly1305 and Ed25519 are cross-checked against other implementations now; ML-KEM is not) and a script that
  fetches and verifies the vendored sources; fuzzing of the parsers the harness does not reach yet (the line operator's signed
  messages, a relay's second hop, the state files; what it does reach is in [ADR 0033](adr/0033-a-switchboard-is-built-for-strangers.md));
  timing tests; reproducible builds; an external review

**M9: Operator grade** *(proposed, decisions below taken)*. Done when a failover test passes: a standby takes over
from a killed primary under a load of 10,000 simulated nodes, every node is back within 30 seconds, no number or name is
lost or issued twice, and the operator can see all of it on a dashboard without reading a log. M8 makes the product
trustworthy; M9 makes the *switchboard* something a third party can run as a service. What is true today is the baseline:

- *Today:* one process, one registry file, about a hundred million possible local numbers, released after a year unheard; 1,024 sessions by
  default; a status call and a script log for visibility; no failover; the switchboard's address is pinned by every node,
  so its key cannot be changed without re-pointing them.

Two relationships are kept apart on purpose. **Replicas** are several processes that are *one* switchboard: one registry, one
code, one writer at a time. **Federation peers** are separate switchboards, each with its own registry and its own code, that
agree to connect calls between them. Redundancy comes first; federation builds on it.

*Redundancy (first)*
- A fixed, operator-configured set of two or three replicas, not general consensus. The registry and name bindings are
  append-only logs, so replication is shipping them; one replica is the writer, elected among the set, and the others
  serve existing numbers, lookups and rings from their copy.
- The rule is stated once: **uniqueness is never traded for availability.** A replica that cannot reach a majority serves
  what it has and refuses new registrations and name bindings until it can. Two replicas cannot form a majority alone, so
  a set of two needs a tie-breaker the operator names; a set of three needs none.
- A node takes a list of switchboard addresses (the replicas of one switchboard) and registers with the first that answers.
- Rotating the switchboard's own key with an overlap period, announced to nodes inside the old session, so a pinned address
  can change without a flag day.

*Federation (second)*
- A full number's code ([ADR 0024](adr/0024-a-full-number-is-a-switchboard-code-and-a-local-number.md)) names a switchboard.
  A registry of switchboards, joined by proving control of the switchboard's key, hands out short aliases for the derived
  codes (the area and country codes of the comparison), and `+555` stays set aside for tests. Dialing a number on another
  switchboard introduces the two nodes through the two switchboards; the data path is still direct, as it is today.

*Number space and lifecycle* (builds on number release, M3)
- A warning when the space is half used. Names (ADR 0023) get the same life cycle.

*Scale*
- More than a thousand sessions per worker (tens of thousands): a smaller peer slot for a node that only registers, one
  worker per core, the longer rekey for switchboard sessions, and a load generator that is part of `make check`.

*Operation*
- Metrics as text a standard collector reads, structured logs, an audit trail of administrative decisions (registrations,
  refusals, evictions of names) that records *no* calls, a token and name admin command, a configuration reload without
  dropping sessions, backup and restore of the registry, and the abuse limits as operator settings.
- The switchboard's key in a hardware module or the platform's key store, not a file.

*Tenancy*
- Several lines per node, so one gateway can serve more than one customer; quotas per token (nodes, lines, lookups). (The
  operator-signed admit is in M3.)

*Assurance* (with M8)
- Signed releases and an SBOM, a published data-retention statement, a penetration test of the switchboard, and a runbook.

Still open: how the registry of switchboard codes is governed and by whom, and who runs the tie-breaker for a set of two.

## Unscheduled

ML-DSA signatures, more than one line per node, VLAN-tagged traffic through a
splice box, IGMP and MLD snooping, Windows and iOS members, arm64, and an OpenWrt package.
