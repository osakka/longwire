# Commands

**Principle: you dial, it works.** There is nothing to write by hand: the only things you ever say are where your switchboard is and how this
machine attaches to its LAN, and you say them once, in the Control Panel or in a saved profile ([below](#profiles)).

`longwire` is the node script (component 2) and `longwire-switchboard` is the switchboard script (component 1). Both
are hibr scripts built on the module's `lw` builtin (component 3), which your own scripts can use too
([below](#scripting-it-yourself-the-lw-builtin)). Anything designed but not built carries `*(planned: M3)*`.

## A first run

```
$ longwire up --switchboard 203.0.113.7:1877/lw1q7m…k2a --splice eth0
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
   safety phrase: gopher daisy castle cactus island flint
   first call from this number
   run: longwire pickup | longwire reject (60 s)
```

## Commands

| Command | Does |
|---|---|
| `longwire on [PROFILE]` | start the node **in the background** from a saved [profile](#profiles) (the default one if none is named), make the jack if it is missing, and say the number it was given. `on` again says it is already on |
| `longwire off` | stop the node (the same as `down`) |
| `longwire up [options]` | run the node in the foreground ([options](#options-of-up)). With no switchboard named it uses the default profile; `up --profile NAME` a named one; anything on the command line overrides the profile |
| `longwire dial NUMBER` | call a number: `447-120-933` (dashes optional) on your own switchboard, or its full number `+0731042 447-120-933`, whose code must be your switchboard's today. A wrong check digit is refused before anything is sent |
| `longwire pickup [ID]` / `longwire reject [ID]` | answer the ringing call; `ID` is the call id from the ring, needed only when several rings are waiting |
| `longwire hangup [NUMBER]` | with no number: end every call and leave the line (or decline a ringing call); with a number: end just that call |
| `longwire peers` | the line you are on: its members and which of them are connected (JSON) |
| `longwire evict NUMBER` | remove a member from the line for good, and change the line key. On a line with an operator only the operator evicts: use `revoke` |
| `longwire revoke EVICTION` | carry the operator's signed eviction (hex, or a file holding it) to the line; every member applies it and the line key changes |
| `longwire operator new FILE`, `operator authority FILE` | make an operator's key (the file is its seed: keep it offline), or show its public key |
| `longwire operator admit FILE LINEID NODE`, `operator evict FILE LINEID NODE` | sign an admit or an eviction for `NODE` (its `lw1…` address or key id) on the line `LINEID` (the 16 hex digits of `id_full` in the founder's status). Print it; it needs no running node |
| `longwire unpin NUMBER` | forget a number's key: the next call from it is a first call again |
| `longwire status` | what the node is doing, as JSON ([fields](#status)) |
| `longwire down` | stop the node |
| `longwire id`, `longwire address`, `longwire bundle` | your key id (hex), your full address (`lw1…`), your public bundle (hex, for `--peer`) |
| `longwire jack --remove [TAP]` | take the jack away (stopping a DHCP client of ours on it first); only a jack that is yours. A profile with `keep_jack = no` does this at every `off` |
| `longwire jack [--address A.B.C.D/N] [--via GATEWAY [--routes NET/PREFIX,...] [--everything]] [TAP]` | make the tap device a single machine attaches to (`lw0` to `lw99`, `lw0` by default) and give it to you, with an IPv4 address if one is given ([ADR 0036](adr/0036-the-jack-gets-its-address-from-the-profile.md)). A small root helper does it ([ADR 0034](adr/0034-a-connection-is-a-profile.md)): polkit asks for a password once, in a terminal or in the desktop. `on` does this by itself when the jack, or its address, is missing |
| `longwire site NIC [PROFILE]`, `longwire site --stop NIC` | make this machine a site node for the card NIC from a profile: a service (`longwire@NIC`) written and started by a root helper after one password, with the profile's switchboard, token, name, answer policy and `listed`; a name with a space is left out. `--stop` stops and disables it ([ADR 0039](adr/0039-a-site-node-from-the-pane.md)) |
| `longwire setup splice NIC`, `longwire setup jack [TAP]` | print the one-time privileged step for that attach mode, to run by hand if you would rather |
| `longwire profile list`, `show [NAME]`, `set NAME KEY=VALUE…`, `token NAME`, `default NAME`, `rename OLD NEW`, `remove NAME` | the saved connections ([profiles](#profiles)) |
| `longwire-switchboard [options]` | run a switchboard ([SWITCHBOARD](SWITCHBOARD.md)) |
| `longwire` with no arguments | turn the node on from the default [profile](#profiles) if it is off, then say how it is (the same as `longwire on` and then `longwire show`). With no profile yet it says how to make one, and that Control Panel > Longwire does the same |
| `longwire show` | how the node is, in a few lines and no JSON: your number, the switchboard, who is connected and what they have carried, the line, and any ring waiting (with the safety phrase) |
| `longwire who NAME` or `who lw1…` | ask the switchboard's directory for listed nodes whose name contains the text (3 characters or more), or that hold that full address: up to 8 results, each with number, key id and name (JSON). A name is a claim, so check the safety phrase on the first call |
| `longwire dial lw1…` | call a full address, through the directory. Fails plainly if the switchboard has none or the node is not listed |
| `longwire who NUMBER` | what this node knows of a number (JSON): its key id and full address, whether it is pinned, on your line, connected (and by what path), and the safety phrase of a live call. It asks nobody, so a number never called, pinned or listed is "not known here" |
| `longwire answer [POLICY]` | show, or change, the [answer policy](#answer-policy) of the running node. It takes effect on the next ring; a call already ringing is judged by the policy it arrived under. `up` sets it again from `--answer` (default `ask`) |
| `longwire doctor` | says whether the scripts (from the catalogue), the installed engine (from the package) and the running node (which keeps the engine it started with) are the same version, and what to do if not; `show`, `on` and the pane say the same when they find a mismatch |
| `longwire watch` | the node's events as sentences, as they happen (a ring shows the safety phrase and what to run). It only listens: Ctrl-C leaves and the node goes on |
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
  switchboard = vps.example.org:1877/lw1…
  token = 5fe1c0ab…
  attach = jack                      jack | splice
  interface = lw0                    the tap, or the NIC to splice
  gateway = 192.168.152.254          a router on the line that reaches other networks (jack only)
  routes = 192.168.150.0/24          networks to reach through it, separated by commas: never the default route ([ADR 0044](adr/0044-routes-beyond-the-line-go-through-a-gateway-on-it.md))
  everything = yes                   send all of this machine's traffic through the gateway; needs gateway and a static address ([ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md))
  keep_jack = yes                    yes (the default): Turn off leaves the jack, so the next Turn on needs no password; no: Turn off removes it
  address = 10.77.0.2/24             this machine's address on the line (jack only; every machine its own, one subnet), or `dhcp`
  name = osakka
  listed = no
  answer = known
```

```
longwire profile set home switchboard=HOST:1877/lw1… token=… attach=jack interface=lw0 answer=known
longwire profile token home            # the token on standard input, so it is never on a command line
longwire on                            # the default profile, in the background
longwire off
```

The first profile you make is the default. `show` prints the token only as its length. A token is needed the first time a machine
registers at a switchboard that wants one; after that the switchboard knows the machine's key and the token can be taken out
(`longwire profile set home token=`).

### Options of `up`

`up` handles `--answer` itself and passes the rest to the engine (`lw start`):

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
| `--require-sas` | refuse a call (other than a mesh call to a fellow member) whose other end does not do the committed safety phrase within five seconds, instead of showing the old, weaker phrase ([ADR 0040](adr/0040-the-safety-phrase-is-committed-before-it-is-shown.md)) |
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

The **safety phrase** is eight words, committed to by the caller before the callee speaks and derived from both nonces, both key ids and the session
([ADR 0040](adr/0040-the-safety-phrase-is-committed-before-it-is-shown.md)). It appears a moment after the call connects (the `sas` event), the same on both screens; the ring shows none. Reading it to each other over another channel defeats a lying switchboard on the
first call. An older node gets the old six-word phrase, marked as the weaker kind (`legacy`); `--require-sas` refuses such a call. A number is pinned to its key once the call is up, so a wrong phrase never leaves a pin to undo
([ADR 0009](adr/0009-the-first-ring-asks-and-a-key-is-pinned-once-the-call-is-up.md)).

## Status

`longwire status` prints one JSON object. On a node:

| Field | Meaning |
|---|---|
| `key_id`, `number`, `name`, `listed`, `switchboard`, `reflexive` | your key id and number; the name you announce (empty if none) and whether you asked to be listed; the switchboard's state (`none`, `no switchboard`, `registering`, `dial tone`); the public address it sees you at |
| `mapped` | the address and port the router gave by NAT-PMP, or empty |
| `nat` | what the switchboard's second port says of your NAT: `easy` (one mapping for every destination, so a direct path is likely), `strict` (a new mapping per destination: only a relay is certain) or `unknown` (not measured yet, or the second port is blocked) |
| `call` | one word for all your calls: `dialing` or `ringing` (a person's call is waiting), else `connecting`, `up` or `idle` |
| `calls` | each call: `call` (its id), `number`, `state` (`dialing`, `ringing`, `punching`, `connecting`, `up`), `we_called`, `mesh` (a call a line's mesh made by itself), `name` (what a ringing caller announced: a claim) and `phrase` (the safety phrase of a ring awaiting an answer, else empty) |
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
| `peers` | per peer: `key_id`, `number`, `state`, `path`, `ep`, `age`, `rx_frames`, `tx_frames`, `rx_bytes`, `tx_bytes` (the Ethernet frames carried each way, since the session started: [ADR 0031](adr/0031-a-connection-counts-the-frames-it-carries.md)), `rekeys`, `drop_auth`, `drop_replay` |

On a switchboard: `key_id`, `address`, `uptime`, `numbers`, `online`, `calls_ringing`, `policy`, `drop_garbage`, `code` (its short code), `code_refused` (why the registry did not give it one), `codes_issued` (a registry's count), `max_nodes` and the handshake gauges `hs_shed`, `cookies_sent` and `cookies_got`. `lw sb nodes [FROM]` lists the registry a page at a time (24 entries, then `next`): each entry has `number`, `name`, `listed`, `online` and `age`, and no address or key. A
readable view of a node's status is `longwire show`; a switchboard's is the switchboard application.

## Events

**Events.** Everything the worker does is an event, one JSON object per line on `/dev/longwire/events`: `registered`,
`register-refused` (with `reason`), `code-assigned` (a switchboard was given its short code by the registry), `ring` (with `from`, a name that is empty if none was announced, `phrase`, `known`, `admit`), `found` (the directory's answer: the query, whether there is a directory, whether you were told to slow down, and the results), `connected` (with `number`, `path`, `ep`,
`phrase`; a relayed one says so in its path field and names the relay in a via field), `path` (a relayed peer found a direct path: `number`, `path`, `ep`), `call-failed` (with a plain-language `reason`), `call-lost` (a call that was up lost its other end; the
script decides whether to dial again), `hangup` (with `by`, `us` or `them`), `dhcp-conflict` (two DHCP servers offered on the line, with both addresses), `key-changed` (a number answered with a
different key than before), `site` (a splice box became `active` or `standby`), and for lines `line-joined` (with `line`,
`members`), `member-joined`, `member-left` and `member-evicted` (each with `number`), `evicted` (you were put off the
line), `line-partial` (with `members` and `up`: the mesh is still incomplete 30 seconds after joining) and
`line-complete`, `sas` (the safety phrase, once the exchange is done; it says whether it is the old kind) and `sas-failed` (the exchange failed and the call was ended), and `address-skipped` (with the address and the port: an address of the other end lies inside the jack's own network and no other road to it could be had, so it is not used; [ADR 0037](adr/0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md)). The node script turns them into the sentences above; the desktop will turn them into dialogs
*(planned: M4)*.

## Scripting it yourself: the `lw` builtin

The module adds one builtin, `lw`. The node and switchboard scripts are written entirely with it, and so can yours. Every
verb works from any hibr process, through the worker's control socket (`~/.longwire/ctl`, or `$LW_SOCK`).

| Verb | Does |
|---|---|
| `lw sb nodes [FROM]` | on a switchboard: a page of the registry (JSON) |
| `lw start [options]` | open the port, clear every capability set, then fork the engine worker, which inherits the empty sets (see the note below) |
| `lw stop` | end the worker |
| `lw dial NUM`, `lw pickup [ID]`, `lw reject [ID]`, `lw hangup [NUM]`, `lw unpin NUM` | calls and pins |
| `lw evict NUM`, `lw revoke HEX`, `lw peers` | the line: remove a member (and change the key); carry the operator's eviction; list the members |
| `lw status` | the JSON above, also in `$RET` |
| `lw key new\|id\|address\|bundle\|export\|import SEEDFILE` | create or read an identity |
| `lw key authority SEEDFILE`, `lw key admit\|evict SEEDFILE LINEID KEYID` | an operator's public key; a signed admit or eviction (hex) |
| `lw version` | the version of the installed engine (the scripts compare it with themselves and with the running node) |
| `lw plan N` | the N-th assigned switchboard code of the numbering plan, counting from 0 (for operators and tests) |
| `lw sb start [options]`, `lw sb reply ID accept\|refuse [ARG...]` | the switchboard worker, and the script's decisions |
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
panel pane** for the client (number and name, switchboard, attach mode, answer policy, dial, the ring dialog with the safety
phrase, lines and their members) and an **application** for the switchboard (nodes, numbers, a call feed that forgets at
`GO`, limits, tokens), each a catalogue entry of its own ([ADR 0030](adr/0030-three-surfaces-one-engine.md)). Both are built (`control-panel/longwire.hibr`, `apps/switchboard.hibr`).
