# Commands

**Principle: you dial, it works.** There is nothing to write by hand: the only things you ever say are where your switchboard is and how this
machine attaches to its LAN, and you say them once, in the Control Panel or in a saved profile ([below](#profiles)).

`longwire` is the node script (component 2) and `longwire-switchboard` is the switchboard script (component 1). Both
are hibr scripts built on the module's `lw` builtin (component 3), which your own scripts can use too
([below](#scripting-it-yourself-the-lw-builtin)). Anything designed but not built carries `*(planned: M3)*`.

## A first run

```
$ longwire up --switchboard 203.0.113.7:6741/lw1q7m…k2a --splice eth0
☎  your number 447-120-933 — dial tone
```

(The examples write `longwire`, the launcher the package installs; from a checkout that is `hibr scripts/longwire.hibr`.)

`longwire up` runs the node in the foreground, which is what a service unit or `hold` runs. It creates your identity
(`~/.longwire/seed`, 32 bytes, mode 0600: back it up, it *is* your number), starts the engine worker, registers with the
switchboard, and prints what happens as sentences. In another terminal:

```
$ longwire dial 312-884-076
{"call":"dialing","number":"312-884-076"}
```

and on the other machine's `up` terminal:

```
☎  447-120-933 is calling
   the safety phrase comes once you are connected: compare it with theirs
   picking up adds 447-120-933 to your network (your line): it stays a member, and reconnects by itself, until you evict it
   first call from this number
   run: longwire pickup | longwire reject (60 s)
```

## Commands

| Command | Does |
|---|---|
| `longwire on [PROFILE]` | start the node **in the background** from a saved [profile](#profiles) (the default one if none is named), make the jack if it is missing, and say the number it was given. `on` again says it is already on |
| `longwire off` | stop the node (the same as `down`) |
| `longwire up [options]` | run the node in the foreground ([options](#options-of-up)). With no switchboard named it uses the default profile; `up --profile NAME` a named one; anything on the command line overrides the profile |
| `longwire dial NUMBER` | call a number: `447-120-933` (dashes optional) on your own switchboard, or its full number `'+0731042 447-120-933'` (quoted: one argument), whose code must be your switchboard's today. A wrong check digit is refused before anything is sent |
| `longwire pickup [ID]` / `longwire reject [ID]` | answer the ringing call; `ID` is the call id from the ring, needed only when several rings are waiting |
| `longwire hangup [NUMBER]` | with no number: end every call and leave the line (or decline a ringing call); with a number: end just that call |
| `longwire peers` | the line you are on: its members and which of them are connected (JSON) |
| `longwire evict NUMBER` | remove a member from the line for good, and change the line key. On a line with an operator only the operator evicts: use `revoke` |
| `longwire revoke EVICTION` | carry the operator's signed eviction (hex, or a file holding it) to the line; every member applies it and the line key changes |
| `longwire operator new FILE`, `operator authority FILE` | make an operator's key (the file is its seed: keep it offline), or show its public key |
| `longwire operator admit FILE LINEID NODE`, `operator evict FILE LINEID NODE` | sign an admit or an eviction for `NODE` (its `lw1…` address or key id) on the line `LINEID` (the 16 hex digits of `id_full` in the founder's status). Print it; it needs no running node |
| `longwire unpin NUMBER` | forget a number's key: the next call from it is a first call again |
| `longwire verify [NUMBER WORDS…]` | with nothing: each call, its safety phrase, and whether it is verified. With a number and the words the other end shows: compare them with this end's (any case, any spacing); a match is recorded (`show` and the pane say *verified*), a difference ends the call and forgets the key ([ADR 0054](adr/0054-a-phrase-is-checked-by-typing-the-other-ends-words.md)) |
| `longwire status` | what the node is doing, as JSON ([fields](#status)) |
| `longwire down` | stop the node |
| `longwire id`, `longwire address`, `longwire bundle` | your key id (hex), your full address (`lw1…`), your public bundle (hex, for `--peer`) |
| `longwire jack --remove [TAP]` | take the jack away (stopping a DHCP client of ours on it first); only a jack that is yours. `longwire off` does this when the profile the node was turned on with says `keep_jack = no` |
| `longwire jack [--address A.B.C.D/N\|dhcp] [--via GATEWAY [--routes NET/PREFIX,...]] [--everything \| --share] [--dns NAMESERVER,...] [TAP]` | make the tap device a single machine attaches to (`lw0` to `lw99`, `lw0` by default) and give it to you, with an IPv4 address if one is given ([ADR 0036](adr/0036-the-jack-gets-its-address-from-the-profile.md)), routes through a gateway ([ADR 0044](adr/0044-routes-beyond-the-line-go-through-a-gateway-on-it.md)), everything through the line ([ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md)) and up to three name servers ([ADR 0047](adr/0047-names-through-the-line-are-set-on-the-jack.md)). `--routes` needs `--via` and a static `--address`. `--everything` with `--via` goes to that gateway; without it, to the member that shares its internet ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)), and then needs only the jack's own address, fixed or `dhcp`. `--share` shares this machine's internet with the line: forwarding on for the jack and for the device the machine's own default route leaves by, and an nftables table of the jack's own (`longwire-lwN`) that lets the line's network out through that device, masqueraded, lets only answers back and lets nothing from the line reach the machine's other networks; it needs a fixed `--address` and `nft`, is undone exactly when the jack is made again without it or removed, and does not go with `--everything`. `--via` alone and `--dns` without `--everything` are dropped. A device of that name owned by another user is refused. A small root helper does it ([ADR 0034](adr/0034-a-connection-is-a-profile.md)): polkit asks for a password once, in a terminal or in the desktop. The device, its address and routes go through `/usr/lib/longwire/jack` (polkit's `org.longwire.jack`); with `--everything` or `--share` the script runs the same helper as `/usr/lib/longwire/jack-everything` (`org.longwire.everything`), so allowing the first with no password does not allow rerouting the machine ([ADR 0058](adr/0058-the-device-and-the-whole-machine-are-two-permissions.md)). `on` does this by itself when the jack, or its address, is missing |
| `longwire exit [on\|off] [PROFILE]` | share this machine's internet with the line, or stop: sets the profile's `exit` key (the running one, else the default) and, if the node is on with that profile, turns it off and on again, so the jack helper sets the sharing up (one password, for `org.longwire.everything`). With neither, says whether the profile shares. Needs a jack with a fixed `address` and no `everything`; the machine needs nftables. The others then use it with `everything = yes` and no `gateway`. An exit sees everything its users send outside the line, and the internet sees its address ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)) |
| `longwire site NIC [PROFILE]`, `longwire site --stop NIC` | make this machine a site node for the card NIC from a profile: a service (`longwire@NIC`) written and started by a root helper after one password, with the profile's switchboard, token, name, answer policy and `listed`; a name with a space is left out. `--stop` stops and disables it ([ADR 0039](adr/0039-a-site-node-from-the-pane.md)) |
| `longwire setup splice NIC`, `longwire setup jack [TAP [ADDR]]` | print the one-time privileged step for that attach mode, to run by hand if you would rather; ADDR is an address with its prefix, or `dhcp` |
| `longwire profile list`, `show [NAME]`, `set NAME KEY=VALUE…`, `token NAME`, `default NAME`, `rename OLD NEW`, `remove NAME` | the saved connections ([profiles](#profiles)) |
| `longwire-switchboard [options]` | run a switchboard ([SWITCHBOARD](SWITCHBOARD.md)) |
| `longwire` with no arguments | turn the node on from the default [profile](#profiles) if it is off, then say how it is (the same as `longwire on` and then `longwire show`). With no profile yet it says how to make one, and that Control Panel > Longwire does the same |
| `longwire show` | how the node is, in a few lines and no JSON: your number, the switchboard, who is connected and what they have carried, the line, any ring waiting, and with everything through the line its `way out` (the member it goes out through, or a warning that none shares, so nothing goes out); on an exit, `sharing` |
| `longwire who NAME` or `who lw1…` | ask the switchboard's directory for listed nodes whose name contains the text (3 characters or more), or that hold that full address: up to 8 results, each with number, key id and name (JSON). A name is a claim, so check the safety phrase on the first call |
| `longwire dial lw1…` | call a full address, through the directory. Fails plainly if the switchboard has none or the node is not listed |
| `longwire who NUMBER` | what this node knows of a number (JSON): its key id and full address, whether it is pinned, on your line, connected (and by what path), and the safety phrase of a live call. It asks nobody, so a number never called, pinned or listed is "not known here" |
| `longwire answer [POLICY]` | show, or change, the [answer policy](#answer-policy) of the running node. It takes effect on the next ring; a call already ringing is judged by the policy it arrived under. `up` sets it again from `--answer` (default `ask`). It is kept in `~/.longwire/answer` of whoever runs it, and read at each ring by the `up` running from the same directory: for a service node, change `--answer` in `/etc/longwire/NIC.conf` and restart it |
| `longwire doctor` | says whether the scripts (from the catalogue), the installed engine (from the package) and the running node (which keeps the engine it started with) are the same version, and what to do if not; `show`, `on` and the pane say the same when they find a mismatch |
| `longwire watch` | the node's events as sentences, as they happen (a ring shows who calls, what picking up means and what to run; the safety phrase follows once connected). It only listens: Ctrl-C leaves and the node goes on |
| `longwire shell NUMBER` | dial, then a shell whose only network is the line *(planned: M5)* |
| `longwire key export`, `longwire key import` | the identity as 33 words from the safety-phrase list (32 for the seed, one of checksum), and back. `export` prints to the terminal only; `import` reads the words on standard input, checks the checksum, and refuses to overwrite an identity. The same words give the same key and so the same number. They *are* the identity: keep them like the seed file |

### Profiles

A profile is a saved connection: which switchboard, the token it asked for, how to attach, and so on. They live in one file,
`~/.config/longwire/profiles` (`$XDG_CONFIG_HOME/longwire/profiles`), readable by you only because it holds tokens, and the
command, the Control Panel pane and the menu bar item all read it. You can edit it by hand; `longwire profile` rewrites it whole, so a
comment you put in it is not kept.

```
default home

profile home
  switchboard = vps.example.org:6741/lw1…
  token = 5fe1c0ab…
  attach = jack                      jack | splice
  interface = lw0                    the tap, or the NIC to splice
  gateway = 192.168.152.254          a router on the line that reaches other networks (jack only)
  routes = 192.168.150.0/24          networks to reach through it, up to 16, typed with commas, spaces or semicolons between them and kept with commas; each its first address and a prefix of 8 to 32, never the default route; they need a gateway and a fixed address ([ADR 0044](adr/0044-routes-beyond-the-line-go-through-a-gateway-on-it.md))
  everything = yes                   send all of this machine's traffic through the line: to the gateway if there is one, else out through the member that shares its internet; needs an address ([ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md), [ADR 0056](adr/0056-a-member-can-share-its-internet.md))
  exit_through = 447-120-933         with everything and no gateway: go out through this member, when more than one shares; empty: the only one, or the lowest number
  dns = 1.1.1.1,9.9.9.9              ask these name servers (up to 3, typed like routes), through the line, for every name; without everything, each must be on the line's network or a route ([ADR 0059](adr/0059-name-servers-without-everything.md)); with it, like a router on the line or, through an exit, any resolver such as 1.1.1.1 (systemd-resolved; [ADR 0047](adr/0047-names-through-the-line-are-set-on-the-jack.md))
  exit = no                          yes: share this machine's internet with the line (jack only; needs a fixed address and nftables; not with everything)
  keep_jack = yes                    yes (the default): Turn off leaves the jack, so the next Turn on needs no password; no: Turn off removes it (with `org.longwire.jack` set to no password, neither way asks)
  address = 10.77.0.2/24             this machine's address on the line (jack only; every machine its own, one subnet), or `dhcp`
  name = osakka
  listed = no
  answer = known
```

```
longwire profile set home switchboard=HOST:6741/lw1… token=… attach=jack interface=lw0 answer=known
longwire profile token home            # the token on standard input, so it is never on a command line
longwire on                            # the default profile, in the background
longwire off
```

The first profile you make is the default. `show` prints the token only as its length. A token is needed the first time a machine
registers at a switchboard that wants one; after that the switchboard knows the machine's key and the token can be taken out
(`longwire profile set home token=`).

### Options of `up`

`up` handles `--answer` and `--profile` itself, reads `--admit` from a file when it names one, and passes the rest to the engine (`lw start`):

| Option | Does |
|---|---|
| `--switchboard HOST:PORT/KEY` | the switchboard to register with; `KEY` is its full address (`lw1…`) or 64 hex digits. Required until a default public switchboard is in the script ([ADR 0014](adr/0014-a-default-public-switchboard.md)) |
| `--token T` | the registration token, for a switchboard that requires one |
| `--name TEXT` | announce this name (32 bytes of text) to the nodes you call, via a switchboard that carries names. Default: none ([ADR 0023](adr/0023-names-are-opt-in-claims.md)) |
| `--listed` | list that name and your number in the switchboard's directory so others can find you by name or full address; the name is yours alone from then on. Default: not listed |
| `--answer ask\|known\|auto\|never` | the [answer policy](#answer-policy); default `ask` |
| `--splice NIC` | attach to a LAN: frames go straight on that NIC. Needs `CAP_NET_RAW` once (`longwire setup splice NIC`) |
| `--site-key FILE` | the secret shared by every splice box on this LAN segment, 64 hex digits. On a line the boxes derive it themselves from the line key; this is for two boxes that are not on one, and overrides the derived key ([ARCHITECTURE §4](ARCHITECTURE.md#4-two-boxes-on-one-segment-site-groups)) |
| `--jack TAP` | attach a tap device you own (`longwire setup jack`) |
| `--sock-port LOCAL:PEER` | frames as datagrams on a Unix socket: for tests |
| `--listen [ADDR:]PORT` | the UDP address to listen on (default: any port, IPv4 and IPv6). `[ADDR]:PORT` is IPv6, a comma separates several: `127.0.0.1:4001,[::1]:4001`. A bare port, or none, is both families on any address ([ADR 0027](adr/0027-ipv6-is-a-second-socket.md)) |
| `--peer BUNDLE[@HOST:PORT]` | a peer known by its bundle file; with an address the node dials it, without it waits. Used without a switchboard |
| `--authority KEY` | found a line run by the operator whose public key this is: the line exists at once and only the operator's admits and evictions count ([ADR 0026](adr/0026-a-line-may-have-an-operator.md)) |
| `--admit ADMIT` | the operator's admit for this node (hex, or a file), presented when it calls a member of an operator's line |
| `--relay yes\|no\|auto` | relay for members of your line: `auto` (default) only when this node has a public address |
| `--nat-pmp on\|off` | ask the home router (NAT-PMP) to keep a port open for this node and offer it as an address (default on) |
| `--direct-retry S` | how often a relayed peer tries for a direct path (60, doubling to 600) |
| `--keepalive S`, `--rekey S` | keepalive interval (25) and rekey interval (120), in seconds |
| `--sb-rekey S` | the rekey interval of a switchboard session (600), the same on the node and the switchboard: a switchboard holds thousands of sessions, and they carry only control messages |
| `--underlay DEV` | the network device to reach peers by when your jack has an address in their network (default: the default route's); [ADR 0037](adr/0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md) |
| `--everything` | send all of this machine's traffic through the jack's own routing table (made by `longwire jack --everything`). Every socket of the engine is tied to the real network device (the default route's, or `--underlay`), so the line's own packets never go into the line; when that device changes the node lets go of the jack, says `roamed`, and `up` starts it again on the new one ([ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md), [ADR 0046](adr/0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md)). Needs `--jack`. `up` adds it by itself when the jack's table already takes everything |
| `--use-exit` | send what goes to the exit gateway (169.254.77.1, MAC 02:6c:77:65:78:74, set by `longwire jack --everything` without `--via`) to the member that shares its internet, and to nobody else; with none, it is dropped. `up` adds it from a profile with `everything = yes` and no `gateway`, and by itself when the jack's table goes to that gateway ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)) |
| `--exit-through NUMBER` | the member to go out through when more than one shares (implies `--use-exit`); it must offer, or nothing goes out |
| `--exit` | share this machine's internet: tell every member, inside the session, every 10 s, the MAC of the jack (`longwire jack --share` does the kernel's side). Not with `--use-exit`. Needs `--jack`, or `--exit-mac` |
| `--exit-mac MAC` | a splice box: the way out is this machine on the LAN (its router), not a jack; implies `--exit` |
| `--profile NAME` | take the connection from a saved [profile](#profiles); handled by the script |
| `--cand-loopback` | offer 127.x as a call candidate: tests, and two nodes on one host |
| `--seed FILE`, `--sock PATH`, `--state DIR` | where the identity, control socket and pins live (default `~/.longwire/`) |
| `--group NAME\|GID` | give the control socket to that group at mode 0660, so its members can use it ([ADR 0032](adr/0032-the-services-share-their-control-socket-with-a-group.md)); the services use `longwire`. An unknown group stops the start |
| `--maxpeers N` | how many peers to hold (default 32; a switchboard 1024) |

**Attaching.** The attach mode is explicit (`--splice` or `--jack`), or the one in the profile. `jack` is the one for a single machine, and
the jack is made for you (`longwire jack`, or `on`). Choosing the mode automatically, from what the machine can do, *(planned: M4)*.

## Answer policy

| Policy | A ring from… |
|---|---|
| `ask` (default) | anyone: print it and wait for `longwire pickup` or `reject`; the worker refuses after 60 s |
| `known` | a number you have been connected to before: pick up. Anyone else: ask |
| `auto` | anyone: pick up (headless boxes, tests) |
| `never` | reject everything, vouched-for or not. A call already up is unaffected |

Under `ask`, `known` and `auto` alike, a caller **vouched for by a member of your line** (the ring says `admit`) is
picked up without a prompt: that is how a line's mesh builds itself. A node you have not met, or that was evicted from
the line, is never admitted that way: an evicted node is refused outright.

The **safety phrase** is eight words, committed to by one end before the other speaks and derived from both nonces, both key ids and the session
([ADR 0040](adr/0040-the-safety-phrase-is-committed-before-it-is-shown.md)); the end with the lower key id commits, so both ends agree on it whoever dialled. It appears a moment after the call connects (the `sas` event), the same on both screens; the ring shows none. Reading it to each other over another channel defeats a lying switchboard on the
first call. Every session reached through a switchboard has one, a fellow member's and a relayed one included, and a call whose other end does not take part is ended ([ADR 0055](adr/0055-no-legacy.md), [ADR 0057](adr/0057-the-safety-phrase-belongs-to-the-session.md)); `watch` says it quietly, and the desktop does not interrupt, for a member the line brought (`mesh`). A number is pinned to its key once the call is up, so a wrong phrase never leaves a pin to undo
([ADR 0009](adr/0009-the-first-ring-asks-and-a-key-is-pinned-once-the-call-is-up.md)).

## Status

`longwire status` prints one JSON object. On a node:

| Field | Meaning |
|---|---|
| `node` | `up` (a switchboard says `switchboard`) |
| `version` | the engine this node started with; the installed one may be newer (`longwire doctor`) |
| `key_id`, `number`, `name`, `listed`, `switchboard`, `reflexive` | your key id and number; the name you announce (empty if none) and whether you asked to be listed; the switchboard's state (`none`, `no switchboard`, `registering`, `dial tone`); the public address it sees you at |
| `mapped` | the address and port the router gave by NAT-PMP, or empty |
| `nat` | what the switchboard's second port says of your NAT: `easy` (one mapping for every destination, so a direct path is likely), `strict` (a new mapping per destination: only a relay is certain) or `unknown` (not measured yet, or the second port is blocked) |
| `call` | one word for all your calls: `dialing` or `ringing` (a person's call is waiting), else `connecting`, `up` or `idle` |
| `calls` | each call: `call` (its id), `number`, `state` (`dialing`, `ringing`, `punching`, `connecting`, `up`), `we_called`, `mesh` (a call a line's mesh made by itself), `name` (what a ringing caller announced: a claim) and `phrase` (empty: the committed phrase arrives once the call is connected, in the `sas` event and the peer's `phrase`) |
| `line` | `null`, or the line you are on: `id` (six hex digits, for people), `id_full` (all sixteen: what the operator signs for), `managed` (it has an operator) and `authority` (the operator's public key, or empty), `founder` (you founded it), `members` (each with `number`, `key_id` and `state`: `you`, `up` or `down`), `up`, `partial` |
| `uptime`, `port` | seconds since start; the attach mode and its argument |
| `macs`, `local_rx`, `local_tx`, `flood` | the switch: addresses known, frames read from and written to the LAN, frames flooded |
| `drop_garbage`, `drop_noroute`, `drop_flood`, `drop_standby`, `drop_oversize` | frames dropped: unparseable; split horizon; over the flood budget; while standby; oversize and not TCP |
| `frames_split`, `frames_joined`, `resegmented`, `arp_answered`, `dhcp_conflicts` | frames sent as two datagrams and rejoined; coalesced TCP frames cut down; ARP requests and IPv6 neighbour solicitations answered locally; times a second DHCP server was seen |
| `site`, `site_members`, `site_synced` | `alone`, `standby` or `active`; other live boxes heard; remote hosts shared by the active box |
| `hs_shed`, `hs_nocookie`, `hs_badmac2`, `cookies_sent`, `cookies_got` | handshake load: shed by the per-source limit; refused for a missing or wrong cookie; cookies handed out and received |
| `full`, `switchboard_offers`, `switchboard_codes` | your full number (`+code number`); the switchboard keys and short codes this node has been told of |
| `relay` | `routes`, `forwarded`, `dropped` and `offer` (whether this node offers to relay) |
| `registrations`, `sb_rekeys` | `NUMBER` replies received; rekeys of the switchboard session |
| `peers` | per peer: `key_id`, `number`, `state`, `path`, `ep`, `age`, `rx_frames`, `tx_frames`, `rx_bytes`, `tx_bytes` (the Ethernet frames carried each way, since the session started: [ADR 0031](adr/0031-a-connection-counts-the-frames-it-carries.md)), `rekeys`, `drop_auth`, `drop_replay`, `phrase` (the safety phrase once agreed, else empty), `sas` (`agreed`, `pending`, or `none`: a peer named by its bundle, with no switchboard call), `mesh` (no call to it was made or answered by a person here: the desktop does not interrupt for its phrase; [ADR 0057](adr/0057-the-safety-phrase-belongs-to-the-session.md)), `exit` (it has said in the last 35 s that it shares its internet) |
| `underlay`, `underlay_state`, `jack_nets`, `cand_skipped` | the device the underlay socket is tied to; `open`, `failed` or `unused`; how many networks the jack has, connected or routed through it; addresses of the other end left out because they lie inside the jack's networks and no underlay could be had ([ADR 0037](adr/0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md)) |
| `exit` | the exit ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)): `share` (this node shares its internet), `use` (it sends everything through a member that does), `wanted` (the number asked for with `--exit-through`, or empty), `using` (the member it goes out through now, or empty: none, so nothing goes out), `offers` (how many members offer now), `sent` (frames handed to the exit) and `dropped` (frames for the exit gateway with no exit to hand them to) |
| `tied` | with `--everything`: the device every socket of the engine is tied to; empty otherwise ([ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md)) |
| `sb_quiet`, `sb_host` | seconds without a session with the switchboard (0 while there is one); what the probe of its host found at 15 s: `answers`, `large-lost`, `silent`, or empty (before 15 s, or a switchboard on IPv6, which the probe does not cover) ([ADR 0048](adr/0048-a-silent-switchboard-is-said-so.md)) |

On a switchboard (`node` is `switchboard`): `key_id`, `address`, `uptime`, `numbers`, `online`, `calls_ringing`, `policy`, `drop_garbage`, `code` (its short code), `code_refused` (why the registry did not give it one), `codes_issued` (a registry's count), `max_nodes`, `strikes_given`, `bans_issued`, `net_bans_issued`, `ban_refused` (handshakes refused because the key or its network is banned), `echo_shed` (echo requests over their budget), the handshake gauges `hs_shed`, `cookies_sent` and `cookies_got`, and since it started `rx_pkts`, `rx_bytes`, `tx_pkts`, `tx_bytes` (every UDP packet and byte in and out), `registrations`, `rings`, `put_through`, `ended` and `bans_active` ([ADR 0049](adr/0049-the-switchboard-window-shows-traffic-and-names-every-key.md)). `lw sb nodes [FROM]` lists the registry a page at a time (24 entries, then `next`): each entry has `number`, `name`, `listed`, `online` and `age`, and no address or key. A switchboard's status does not name its engine's version: restart it after every package upgrade. A
readable view of a node's status is `longwire show`; a switchboard's is the switchboard application.

## Events

**Events.** Everything the worker does is an event, one JSON object per line on `/dev/longwire/events`: `registered`,
`register-refused` (with `reason`), `code-assigned` (a switchboard was given its short code by the registry), `ring` (with `call`, `from`, a name that is empty if none was announced, `key_id`, `known`, `admit`; no phrase: it comes in `sas`), `found` (the directory's answer: the query, whether there is a directory, whether you were told to slow down, and the results), `connected` (with `number`, `path`, `ep`;
a relayed one says so in its path field and names the relay in a via field instead of `ep`), `path` (a relayed peer found a direct path: `number`, `path`, `ep`), `call-failed` (with a plain-language `reason`), `call-lost` (a call that was up lost its other end; the
script decides whether to dial again), `hangup` (with `by`, `us` or `them`), `dhcp-conflict` (two DHCP servers offered on the line, with both addresses), `key-changed` (a number answered with a
different key than before), `site` (a splice box became `active` or `standby`), and for lines `line-joined` (with `line`,
`members`), `member-joined`, `member-left` and `member-evicted` (each with `number`), `evicted` (you were put off the
line), `line-partial` (with `members` and `up`: the mesh is still incomplete 30 seconds after joining) and
`line-complete`, `sas` (with `number`, `phrase` and `mesh`: the safety phrase, once the exchange is done; `mesh` is true when no call to that peer was made or answered by a person here) and `sas-failed` (the exchange failed and the call was ended), `switchboard-silent` (with `ep` and `host`: no session with the switchboard after 15 s. At 12 s the node sent one small packet to the echo ports of the switchboard's host (its port + 2747): when that came back, it sent one as large as a handshake datagram. `answers` means both came back, so UDP of every size goes there and back and the switchboard is not completing a handshake with this node (its key banned there, a switchboard key that is not this switchboard's, or the switchboard's own port closed while its echo port is open); `large-lost` means only the small one came back: something on the way loses large or fragmented UDP; `silent` means nothing came back; "untested" means the switchboard's address is IPv6, which the probe does not cover; [ADR 0048](adr/0048-a-silent-switchboard-is-said-so.md)), `roamed` (with `from` and `to`: the road under a node that takes everything changed, so it let go of the jack and is started again on the new one; [ADR 0046](adr/0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md)), `exit` (with `number`, `wanted` and `offers`: on a node that sends everything through the member that shares its internet, which member it goes out through now, empty for none; the one asked for with `--exit-through`, or empty; how many members offer. Said when the node starts and whenever that changes; [ADR 0056](adr/0056-a-member-can-share-its-internet.md)), and `address-skipped` (with the address and the port: an address of the other end lies inside the jack's own network and no other road to it could be had, so it is not used; [ADR 0037](adr/0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md)). The node script turns them into the sentences above; the desktop turns a ring into a dialog and a call's safety phrase into a note (a switchboard's own events, ban and net-ban, are for its owner: [SWITCHBOARD](SWITCHBOARD.md#its-log)).

## Colour

In a terminal, the sign at the start of a sentence of `up`, `watch`, `on` and `show` takes its status colour: ✓ green, ✗ red, ⚠ amber, ☎ green for your number and amber for someone
calling ([BRAND](BRAND.md)). Never into a pipe or a file, never with `NO_COLOR` set or `TERM=dumb`; `LONGWIRE_COLOR=always` or `never` decides it outright.

## Scripting it yourself: the `lw` builtin

The module adds one builtin, `lw`. The node and switchboard scripts are written entirely with it, and so can yours. Every
verb works from any hibr process, through the worker's control socket (`~/.longwire/ctl`, or `$LW_SOCK`, or `--sock PATH` after the verb). A worker started without `--group` answers only the user it runs as, root included.

| Verb | Does |
|---|---|
| `lw sb nodes [FROM]` | on a switchboard: a page of the registry (JSON) |
| `lw sb bans`, `lw sb unban KEY\|all` | on a switchboard: its ban settings, counters, banned keys and networks (JSON); lift one key's ban (64 hex digits, or the first 8 or more) or every ban, networks included. Only the user the worker runs as, or root, may lift |
| `lw start [options]` | open the port, clear every capability set, then fork the engine worker, which inherits the empty sets (see the note below) |
| `lw stop` | end the worker |
| `lw dial NUM`, `lw pickup [ID]`, `lw reject [ID]`, `lw hangup [NUM]`, `lw unpin NUM` | calls and pins |
| `lw evict NUM`, `lw revoke HEX`, `lw peers` | the line: remove a member (and change the key); carry the operator's eviction; list the members |
| `lw status` | the JSON above, also in `$RET` |
| `lw key new\|id\|address\|bundle\|export\|import SEEDFILE` | create or read an identity |
| `lw key authority SEEDFILE`, `lw key admit\|evict SEEDFILE LINEID KEYID` | an operator's public key; a signed admit or eviction (hex) |
| `lw version` | the version of the installed engine (the scripts compare it with themselves and with the running node) |
| `lw plan N` | the N-th assigned switchboard code of the numbering plan, counting from 0 (for operators and tests) |
| `lw sb start [options]`, `lw sb reply ID accept\|refuse [ARG...]` | the switchboard worker, and the script's decisions. A registration's `accept` answers with what happened, `{"registered":"NUMBER"}` or `{"refused":"WHY"}`, so the script spends a token only on a registration that happened; the worker waits 10 s for a verdict ([SWITCHBOARD](SWITCHBOARD.md)) |
| `lw who NUM` | what this node knows of a number (JSON) |
| `lw lookup NAME\|lw1…` | ask the directory; the answer arrives as a `found` event |

**`lw start` is one-way for the calling process.** Clearing capabilities also sets `no_new_privs` on the hibr process that
ran it. In a long-lived interactive shell that means `please`, `pkexec` and other setuid programs stop working afterwards. A node
script run as a service never notices; in a shell you use for other things, run `longwire up` from a subshell or under
`hold`. A failed `lw` verb puts its reason in `$ERRMSG`.

```sh
mod load "$LONGWIRE_MOD"
lw status                                  # JSON text, also in $RET
json parse st "$RET"                       # now a map
n := json len st .peers
echo "${st[call]}: $n peers"
for p in "${!st[peers][@]}"; do
	echo "${st[peers][$p][number]:-static} ${st[peers][$p][state]} ${st[peers][$p][ep]}"
done

exec 5</dev/longwire/events                # rings, connections and hang-ups as JSON lines
while read -r -u 5 ev; do
	json parse e "$ev"
	[ "${e[event]}" = ring ] && echo "☎ ${e[from]} is calling"
done
```

`/dev/longwire/events` is a scheme: opening it subscribes to the worker's event stream, and `/dev/longwire/events@PATH` does the same for the worker whose control socket is `PATH` (a switchboard's, beside your node). It is not marked
`HIBR_SCH_LOCAL`, so `source` won't run it.

## The desktop

The command stays the supported way to run a service. Beside it, hibr's desktop (which runs on servers too) gets a **control
panel pane** for the client (number and name, switchboard, attach mode, answer policy, dial, the ring dialog, the safety
phrase once connected, lines and their members) and an **application** for the switchboard (nodes, numbers, a call feed that forgets at
`GO`, limits, tokens), each a catalogue entry of its own ([ADR 0030](adr/0030-three-surfaces-one-engine.md)). Both are built (`control-panel/longwire.hibr`, `apps/switchboard.hibr`).
