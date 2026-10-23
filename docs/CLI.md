# Commands

**Principle: you dial, it works.** There are no config files, and the only flags you need are the ones that say where
your switchboard is and how this machine attaches to its LAN.

`longwire` is the node script (component 2) and `longwire-switchboard` is the switchboard script (component 1). Both
are hibr scripts built on the module's `lw` builtin (component 3), which your own scripts can use too
([below](#scripting-it-yourself-the-lw-builtin)). Anything designed but not built carries `*(planned: M3)*`.

## A first run

```
$ longwire up --switchboard 203.0.113.7:1877/lw1q7m…k2a --splice eth0
☎  your number 447-120-933 — dial tone
```

(The examples write `longwire`; until the launcher is packaged *(planned: M4)* that is `hibr scripts/longwire.hibr`.)

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
| `longwire up [options]` | run the node in the foreground ([options](#options-of-up)) |
| `longwire dial NUMBER` | call a number (`447-120-933`, dashes optional; a wrong check digit is refused before anything is sent) |
| `longwire pickup [ID]` / `longwire reject [ID]` | answer the ringing call; `ID` is the call id from the ring, needed only when several rings are waiting |
| `longwire hangup [NUMBER]` | with no number: end every call and leave the line (or decline a ringing call); with a number: end just that call |
| `longwire peers` | the line you are on: its members and which of them are connected (JSON) |
| `longwire evict NUMBER` | remove a member from the line for good, and change the line key |
| `longwire unpin NUMBER` | forget a number's key: the next call from it is a first call again |
| `longwire status` | what the node is doing, as JSON ([fields](#status)) |
| `longwire down` | stop the node |
| `longwire id`, `longwire address`, `longwire bundle` | your key id (hex), your full address (`lw1…`), your public bundle (hex, for `--peer`) |
| `longwire setup splice NIC`, `longwire setup jack [TAP]` | print the one-time privileged step for that attach mode |
| `longwire-switchboard [options]` | run a switchboard ([SWITCHBOARD](SWITCHBOARD.md)) |
| `longwire` with no arguments starts the node if it isn't running, then shows status | *(planned: M4)* |
| `longwire who NUMBER` | a number's full address, pin and safety phrase *(planned: M3)* |
| `longwire answer POLICY` | change the answer policy while running *(planned: M3)* |
| `longwire watch` | stream events as sentences *(planned: M4)* |
| `longwire shell NUMBER` | dial, then a shell whose only network is the line *(planned: M5)* |
| `longwire key export`, `longwire key import` | the seed as 24 words *(planned: M3)* |

### Options of `up`

`up` handles `--answer` itself and passes the rest to the engine (`lw start`):

| Option | Does |
|---|---|
| `--switchboard HOST:PORT/KEY` | the switchboard to register with; `KEY` is its full address (`lw1…`) or 64 hex digits. Required until a default public switchboard is in the script ([ADR 0014](adr/0014-a-default-public-switchboard.md)) |
| `--token T` | the registration token, for a switchboard that requires one |
| `--answer ask\|known\|auto\|never` | the [answer policy](#answer-policy); default `ask` |
| `--splice NIC` | attach to a LAN: frames go straight on that NIC. Needs `CAP_NET_RAW` once (`longwire setup splice NIC`) |
| `--site-key FILE` | the secret shared by every splice box on this LAN segment, 64 hex digits. On a line the boxes derive it themselves from the line key; this is for two boxes that are not on one, and overrides the derived key ([ARCHITECTURE §4](ARCHITECTURE.md#4-two-boxes-on-one-segment-site-groups)) |
| `--jack TAP` | attach a tap device you own (`longwire setup jack`) |
| `--sock-port LOCAL:PEER` | frames as datagrams on a Unix socket: for tests |
| `--listen [ADDR:]PORT` | the UDP address to listen on (default: any port) |
| `--peer BUNDLE[@HOST:PORT]` | a peer known by its bundle file; with an address the node dials it, without it waits. Used without a switchboard |
| `--relay yes\|no\|auto` | relay for members of your line: `auto` (default) only when this node has a public address |
| `--direct-retry S` | how often a relayed peer tries for a direct path (60, doubling to 600) |
| `--keepalive S`, `--rekey S` | keepalive interval (25) and rekey interval (120), in seconds |
| `--cand-loopback` | offer 127.x as a call candidate: tests, and two nodes on one host |
| `--seed FILE`, `--sock PATH`, `--state DIR` | where the identity, control socket and pins live (default `~/.longwire/`) |
| `--maxpeers N` | how many peers to hold (default 16) |

**Attaching.** Today the attach mode is always explicit (`--splice` or `--jack`). Choosing one automatically, from what
the machine can do, *(planned: M4)*.

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

The **safety phrase** is six words from the hash of the two key ids, and appears on the ring and again when the call
connects, the same on both screens. Reading it to each other over another channel defeats a lying switchboard on the
first call. A number is pinned to its key once the call is up, so a wrong phrase never leaves a pin to undo
([ADR 0009](adr/0009-the-first-ring-asks-and-a-key-is-pinned-once-the-call-is-up.md)).

## Status

`longwire status` prints one JSON object. On a node:

| Field | Meaning |
|---|---|
| `key_id`, `number`, `switchboard`, `reflexive` | your key id and number; the switchboard's state (`none`, `no switchboard`, `registering`, `dial tone`); the public address it sees you at |
| `call` | one word for all your calls: `dialing` or `ringing` (a person's call is waiting), else `connecting`, `up` or `idle` |
| `calls` | each call: `call` (its id), `number`, `state` (`dialing`, `ringing`, `punching`, `connecting`, `up`), `we_called`, `mesh` (a call a line's mesh made by itself) |
| `line` | `null`, or the line you are on: `id`, `founder` (you founded it), `members` (each with `number`, `key_id` and `state`: `you`, `up` or `down`), `up`, `partial` |
| `uptime`, `port` | seconds since start; the attach mode and its argument |
| `macs`, `local_rx`, `local_tx`, `flood` | the switch: addresses known, frames read from and written to the LAN, frames flooded |
| `drop_garbage`, `drop_noroute`, `drop_flood`, `drop_standby`, `drop_oversize` | frames dropped: unparseable; split horizon; over the flood budget; while standby; oversize and not TCP |
| `frames_split`, `frames_joined`, `resegmented`, `arp_answered` | frames sent as two datagrams and rejoined; coalesced TCP frames cut down; ARP requests answered locally |
| `site`, `site_members`, `site_synced` | `alone`, `standby` or `active`; other live boxes heard; remote hosts shared by the active box |
| `hs_shed`, `hs_nocookie`, `hs_badmac2`, `cookies_sent`, `cookies_got` | handshake load: shed by the per-source limit; refused for a missing or wrong cookie; cookies handed out and received |
| `registrations`, `sb_rekeys` | `NUMBER` replies received; rekeys of the switchboard session |
| `peers` | per peer: `key_id`, `number`, `state`, `path`, `ep`, `age`, `rx_frames`, `tx_frames`, `rekeys`, `drop_auth`, `drop_replay` |

On a switchboard: `key_id`, `address`, `uptime`, `numbers`, `online`, `calls_ringing`, `policy`, `drop_garbage`. A
tabular view *(planned: M4)*.

## Events

**Events.** Everything the worker does is an event, one JSON object per line on `/dev/longwire/events`: `registered`,
`register-refused` (with `reason`), `ring` (with `from`, `phrase`, `known`, `admit`), `connected` (with `number`, `path`, `ep`,
`phrase`; a relayed one says so in its path field and names the relay in a via field), `path` (a relayed peer found a direct path: `number`, `path`, `ep`), `call-failed` (with a plain-language `reason`), `call-lost` (a call that was up lost its other end; the
script decides whether to dial again), `hangup` (with `by`, `us` or `them`), `key-changed` (a number answered with a
different key than before), `site` (a splice box became `active` or `standby`), and for lines `line-joined` (with `line`,
`members`), `member-joined`, `member-left` and `member-evicted` (each with `number`), `evicted` (you were put off the
line), `line-partial` (with `members` and `up`: the mesh is still incomplete 30 seconds after joining) and
`line-complete`. The node script turns them into the sentences above; the desktop will turn them into dialogs
*(planned: M4)*.

## Scripting it yourself: the `lw` builtin

The module adds one builtin, `lw`. The node and switchboard scripts are written entirely with it, and so can yours. Every
verb works from any hibr process, through the worker's control socket (`~/.longwire/ctl`, or `$LW_SOCK`).

| Verb | Does |
|---|---|
| `lw start [options]` | open the port, clear every capability set, then fork the engine worker, which inherits the empty sets (see the note below) |
| `lw stop` | end the worker |
| `lw dial NUM`, `lw pickup [ID]`, `lw reject [ID]`, `lw hangup [NUM]`, `lw unpin NUM` | calls and pins |
| `lw evict NUM`, `lw peers` | the line: remove a member (and change the key); list the members |
| `lw status` | the JSON above, also in `$RET` |
| `lw key new\|id\|address\|bundle SEEDFILE` | create or read an identity |
| `lw sb start [options]`, `lw sb reply ID accept\|refuse [ARG...]` | the switchboard worker, and the script's decisions |
| `lw who NUM` | a number's details *(planned: M3)* |

**`lw start` is one-way for the calling process.** Clearing capabilities also sets `no_new_privs` on the hibr process that
ran it. In a long-lived interactive shell that means `sudo` and other setuid programs stop working afterwards. A node
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

`/dev/longwire/events` is a scheme: opening it subscribes to the worker's event stream. It is not marked
`HIBR_SCH_LOCAL`, so `source` won't run it.

## The desktop *(planned: M4)*

The Network pane of hibr's desktop gains a **Lines** section: your number, a dial field, the call and its peers with
path and round-trip time, and pick up, hang up and unpin buttons; the ring becomes a dialog showing the safety phrase,
and the menu bar shows a ☎ that lights while a call is up. It ships as a part of the node app in hibr's catalogue
(`control-panel/lines.hibr` *(planned: M4)*).
