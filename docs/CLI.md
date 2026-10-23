# CLI and UX

**Principle: you dial, it works.** There are no config files and no flags you need to know on day one.

`longwire` is the node script (component 2) and `longwire-switchboard` is the switchboard script (component 1).
Both are hibr scripts built on the module's `lw` builtin (component 3), which your own scripts can use too
([below](#scripting-it-yourself-the-lw-builtin)). Everything a node knows is available as JSON with `--json`, or as
a map in `$RET` from `lw`.

## First run

```
$ longwire
  new identity created            ~/.longwire/seed   (back it up: `longwire key export`)
  ☎  your number   447-120-933
     switchboard   203.0.113.7:1877   dial tone
     attach        splice ready on eth0 (cap_net_raw ok)
```

`longwire` with no arguments starts the daemon if it isn't running, registers, and prints status. Running it again
just prints status.

## What exists today (M1)

The table below is the design. This is what the scripts implement now:

| Command | Status |
|---|---|
| `longwire up [--switchboard HOST:PORT/KEY] [--answer ask\|known\|auto\|never] [--token T] [--jack TAP \| --sock-port L:P] [--listen [ADDR:]PORT] [--peer BUNDLE[@HOST:PORT]]...` | built: runs the node in the foreground, prints events as sentences, applies the answer policy |
| `longwire dial NUMBER`, `pickup`, `reject`, `hangup`, `status`, `down` | built |
| `longwire unpin NUMBER` | built: forget a number's key; the next call from it is a first call again. A pin is written only once a call is up, so a wrong safety phrase is always recoverable |
| `longwire id`, `address`, `bundle` | built: key id, full address (`lw1…`), public bundle (hex) |
| `longwire-switchboard [--listen ADDR:PORT] [--tokens FILE] [--state DIR] [--per-ip-hour N] [--rings-per-minute N]` | built ([SWITCHBOARD](SWITCHBOARD.md)) |
| `peers`, `who`, `watch`, `answer`, `evict`, `shell`, `setup`, `key export/import`, the default switchboard | not yet |

`--switchboard` is required for now: the default public switchboard needs its host and key from you ([DECISIONS
D14](DECISIONS.md#d14)). Answer policies as built: **ask** (the default) prints the ring and waits for `longwire
pickup` (the worker refuses after 60 s), **known** picks up numbers you have been connected to before and asks about
the rest, **auto** picks up anyone (headless boxes, tests), **never** refuses. The `line` policy arrives with party
lines (M3).

**Events.** Everything the worker does is an event, one JSON object per line on `/dev/longwire/events` (or `lw`'s
control socket, `watch`): `registered`, `register-refused`, `ring` (with `from`, `phrase`, `known`), `connected`
(with `number`, `path`, `ep`, `phrase`), `call-failed` (with a plain-language `reason`), `call-lost` (a call that was up lost its other end; the script decides whether to dial again), `hangup` (`by`: `us` or
`them`), and `key-changed`. The node script turns them into sentences; the desktop will turn them into dialogs.

## Commands

| Command | Does |
|---|---|
| `longwire` | start if needed, then show status |
| `longwire dial <number\|lw1…> [--splice IF \| --jack [TAP] \| --leaf]` | call. Joins the callee's line, or starts a new one |
| `longwire hangup [<number> \| all]` | leave the line, or drop one peer |
| `longwire status [--json]` | your number, the line, the peers, each one's path (`direct` / `relay via …` / `lan`), RTT, rates |
| `longwire peers` | one line per peer: number, role, path, RTT, last handshake |
| `longwire pickup` / `longwire reject` | answer the ringing call |
| `longwire answer <ask \| line \| known \| never>` | set the [answer policy](#answer-policy) |
| `longwire evict <number>` | remove a member from the line, permanently, and rotate the line key |
| `longwire who <number>` | look up a number: its full address, pin state and safety phrase |
| `longwire watch` | stream events: `ring`, `connected`, `path`, `key-changed`, `hangup` |
| `longwire shell <number>` | rootless: dial, then open `$SHELL` in a namespace whose only network is the line |
| `longwire up [--splice IF \| --jack [TAP]]` | run the node in the foreground: what a service unit or `hold` runs |
| `longwire setup splice <IF>` | print the one-time service unit with `AmbientCapabilities=CAP_NET_RAW` (and write it, with `sudo`) |
| `longwire setup jack` | print (or run) the one-time `ip tuntap add dev lw0 mode tap user $USER` |
| `longwire key export` / `key import` | the seed as 24 words |
| `longwire down` | hang up everything, deregister, stop |
| `longwire --switchboard host:port/lw1… …` | use a switchboard other than the default public one; it's remembered |
| `longwire-switchboard [--listen ADDR] [--tokens FILE]` | run a switchboard (component 1) |

**Choosing how to attach.** `dial` with no mode flag picks the first that works and says which it picked:

1. **splice**, if the running node was started with `CAP_NET_RAW` and there's exactly one wired NIC with a default
   route
2. **jack**, if a tap owned by this user exists
3. otherwise it explains the choices: `longwire setup splice eth0`, `longwire setup jack`, or `longwire shell`

## Answer policy

| Policy | A ring from… |
|---|---|
| `ask` (default) | anyone: ask the user. In a terminal, a prompt. Under hibr's desktop, a dialog. Headless, it waits for `longwire pickup` for 60 s |
| `known` | a pinned number: pick up. Anyone else: ask |
| `line` | a caller presenting a valid admit from this line: pick up (this is how a party line builds itself). Anyone else: ask |
| `never` | reject everything. Existing calls are unaffected |

`ask` and `known` both include `line` behaviour, so mesh introductions never prompt.

```
  ☎  447-120-933 is calling
     first call — safety phrase:  copper  orbit  lantern  fifteen  harbor  velvet
     pick up? [y]es / [n]o / [a]lways for this number
```

The **safety phrase** is 6 words from `H(key_id_caller ‖ key_id_callee)`. Both screens show the same words. Reading
them to each other over the phone defeats a lying switchboard on the first call.

## Status, the full view

```
$ longwire status
  ☎  447-120-933            line 7f3a9c   trunk   splice eth0
     switchboard  dial tone (203.0.113.7:1877, 18 ms)

  PEER          ROLE   PATH                      RTT    RX/TX            KEYS
  312-884-071   trunk  direct 198.51.100.4:41641  31 ms  48.2 / 12.9 Mb/s  pq 41 s ago
  580-016-224   trunk  relay via 312-884-071     88 ms   1.1 /  0.4 Mb/s  pq 77 s ago
  901-447-358   leaf   direct [2001:db8::7]       64 ms   0.2 /  0.1 Mb/s  pq 12 s ago

  MACs 214 (local 131, remote 83)   ARP answered locally 98%   frames split 0.4%
```

## Scripting it yourself: the `lw` builtin

The module adds one builtin, `lw`. The node and switchboard scripts are written entirely with it, and so can
yours. Every verb works from any hibr process. In the process that ran `lw start` it talks to the worker directly;
anywhere else it goes through the control socket.

| Verb | Does |
|---|---|
| `lw start [--splice IF \| --jack TAP \| --shell] [--seed PATH]` | open the port, fork the engine worker, clear every capability set in both processes (see the note below) |
| `lw stop` | hang up, deregister, end the worker |
| `lw dial NUM` · `lw hangup [NUM]` · `lw pickup` · `lw reject` · `lw evict NUM` | calls |
| `lw status` · `lw peers` · `lw who NUM` | results as a map in `$RET` |
| `lw key new\|export\|import` | the seed |
| `lw sb start --listen ADDR` · `lw sb reply …` | the switchboard worker, and the script's routing decisions |
| `/dev/longwire/events` | the event stream, one JSON object per line |

**`lw start` is one-way for the calling process.** Clearing capabilities also sets `no_new_privs` on the hibr process that
ran it. In a long-lived interactive shell that means `sudo` and other setuid programs stop working afterwards. A node
script run as a service never notices; in a shell you use for other things, run `longwire up` from a subshell
or under `hold`.

```sh
mod load "$LONGWIRE_MOD"
lw dial 312-884-071
lw status                                 # JSON text, also in $RET
json parse st "$RET"                      # now a map
n := json len st .peers
echo "${st[line]} has $n peers"
for p in "${!st[peers][@]}"; do
	echo "$p ${st[peers][$p][path]} ${st[peers][$p][rtt]}"
done

while read ev; do                          # rings, paths and hang-ups as JSON lines
	json parse e "$ev"
	[ "${e[event]}" = ring ] && echo "☎ ${e[from]} is calling"
done </dev/longwire/events
```

- **`/dev/longwire/events`** is a scheme: opening it subscribes to the worker's event stream. It isn't marked
  `HIBR_SCH_LOCAL`, so `source` won't run it.
- **The desktop** (part of component 2: `control-panel/lines.hibr`, which comes with the catalogue app) gets a **Lines** section in the Network pane: your number, a dial field, the line and its peers
  with path and RTT, and pick up / hang up / evict buttons. The ring is a desktop dialog showing the safety phrase,
  and the menu bar shows a ☎ that lights up while a line is up.
