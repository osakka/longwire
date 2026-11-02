# Operating longwire

For whoever runs a switchboard, a site, or a machine that should stay on. Commands assume Debian-style paths and `please`, hibr's `sudo`: it runs a command as root through polkit (and is `sudo` underneath on macOS), and Control Panel > Administrative Access sets who may and with whose password; the
rpm and pacman packages put the same files in the same places. For background on what each piece does, see
[ARCHITECTURE](ARCHITECTURE.md); for what protects what, [SECURITY](SECURITY.md).

**What has been tried.** The services are described by systemd units that `systemd-analyze` accepts and whose sandbox was checked
against a trace of the system calls the programs make. They have not been run under a live systemd by the author, and installing
from the repositories on a clean machine has not been tried. If a step here does not do what it says, `journalctl -u NAME` is the
first place to look, and [TROUBLESHOOTING](TROUBLESHOOTING.md) the second.

## Where things live

| What | Where |
|---|---|
| your identity (the seed; it *is* your number) | `~/.longwire/seed`, 32 bytes, mode 0600 |
| saved connections (profiles, with tokens) | `~/.config/longwire/profiles`, mode 0600 |
| a node's control socket, pins, answer policy, log | `~/.longwire/` (`ctl`, `known`, `answer`, `log`) |
| the root helpers (through polkit): the network device, and a site node | `/usr/lib/longwire/jack`, `/usr/lib/longwire/site` |
| the engine | `/usr/lib/hibr/longwire.so` |
| the scripts | hibr's catalogue: `catalogue install cli/longwire`, `cli/longwire-switchboard` |
| the services | `longwire-switchboard.service`, `longwire@NIC.service` |
| service arguments | `/etc/longwire/switchboard.conf`, `/etc/longwire/NIC.conf` |
| a service's state | `/var/lib/longwire-switchboard/` (its identity is `seed` there); `/var/lib/longwire/NIC/` (its identity is `.longwire/seed` inside). With `DynamicUser` these are links to `/var/lib/private/longwire-switchboard` and `/var/lib/private/longwire/NIC`, which only root can enter: use `please` |
| a service's control socket | `/run/longwire-switchboard/ctl`, `/run/longwire/NIC/ctl` |
| manual pages | `man longwire`, `longwire-switchboard`, `longwire-profiles`, `longwire-security` |

## Run a switchboard

The switchboard gives nodes their numbers and introduces two nodes that want to talk. It is not on the data path, so a small server
is enough, but it must be reachable from the internet on **UDP on its listen port and its echo port** (6741 and 9488 by default: the echo port is the listen port + 2747, and
answers the NAT-type check and a node's probe when the switchboard is silent to it). Nodes before 0.5.36 ask the listen port + 1 instead (6742), which the switchboard also answers
until `--echo-legacy off` ([ADR 0053](adr/0053-the-switchboard-has-ports-of-its-own.md)).

```
please apt install longwire
catalogue --system install cli/longwire-switchboard
please mkdir -p /etc/longwire
echo 'ARGS="--listen 6741 --registry none --tokens /var/lib/longwire-switchboard/tokens"' | please tee /etc/longwire/switchboard.conf
please systemctl enable --now longwire-switchboard
```

The service retries every couple of seconds until the token file exists, which is expected: the same start creates its state
directory. Then make the tokens (one per machine you want to let in, plus a spare):

```
please install -m 0600 /dev/null /var/lib/longwire-switchboard/tokens
please chown "$(stat -c %u /var/lib/private/longwire-switchboard)" /var/lib/longwire-switchboard/tokens
for i in 1 2 3; do openssl rand -hex 16; done | please tee /var/lib/longwire-switchboard/tokens
```

The file belongs to the service's own user and nobody else can read it. (A group-readable file would let anyone in the group `longwire` read, and
use, the tokens that have not been spent.) These steps have not been run under a live systemd by the author.

The switchboard script can make, list and remove tokens itself, with no switchboard running and no `openssl`: `please longwire-switchboard token new 3 --tokens /var/lib/longwire-switchboard/tokens`
prints three new tokens and adds them to the file (mode 0600); `token list` shows the unspent ones; `token remove TOKEN` takes one out. Run as root it keeps the file
owned by the folder's owner, the service. The **Switchboard** window does the same from the desktop (View > Tokens, [DESKTOP](DESKTOP.md#the-switchboard-window)).

Open the firewall (`please ufw allow 6741,9488,6742/udp`; 6742 only while nodes before 0.5.36 remain), or your own tool,, read its address, and give each user a token:

```
please journalctl -u longwire-switchboard -n 10 --no-pager | grep switchboard
# switchboard  lw1q7m…k2a   (give this full address to your nodes)
please hibr -c 'mod load longwire; lw key address /var/lib/longwire-switchboard/seed'    # the same, any time, from its identity file
```

Nodes use `--switchboard YOUR.HOST:6741/lw1q7m…k2a`.

- **`--registry none`** makes it a private switchboard that keeps its own code. A shared public registry of switchboards is a later step
  (see [SWITCHBOARD](SWITCHBOARD.md)).
- **Tokens.** Open registration (no `--tokens`) lets anyone take a number. For a switchboard that matters, use tokens. A token is spent on first
  use; a machine that has registered once is recognised by its key afterwards. Add one with
  `openssl rand -hex 16 | please tee -a /var/lib/longwire-switchboard/tokens`. The file must be in the state directory: the service cannot
  write `/etc`.
- **Strikes and bans.** On by default (10 strikes in 10 minutes, a 15-minute first ban doubling to a day; `--strikes 0` turns it off). Add your own nodes with `--never-ban ADDR/PREFIX` or `--never-ban KEY` if you ever lock yourself out (`please longwire-switchboard bans clear all` lifts everything at once; a restart of the service does too, since bans are kept in memory, and is what makes a new `--never-ban` take effect).
  Details and why they cannot be turned against your users: [SWITCHBOARD](SWITCHBOARD.md).
- **Limits.** Defaults are 1,024 sessions (`--max-nodes`), 100,000 numbers (`--max-numbers`) and 20 registrations per source per hour; the unit's
  memory limit is 1 GB, which suits the defaults. Raise the limit with the numbers.
- **Back up the seed.** `/var/lib/longwire-switchboard/seed` is the switchboard's identity. Every node pins its address, so losing it
  means re-pointing every node. Copy it somewhere private (`please cp … ~/switchboard-seed.backup; chmod 600 …`) and never share it.
- **Watch it.** `please journalctl -u longwire-switchboard -f` for the log; `please journalctl -u longwire-switchboard -o cat | grep -E '^(ban|net-ban) '` for the bans alone. Each ban line gives
  the key's first 16 hex digits, how long, which ban in a row, why, and the command that lifts it ([SWITCHBOARD](SWITCHBOARD.md#its-log)). As a member of the group `longwire`,
  `LW_SOCK=/run/longwire-switchboard/ctl longwire status` prints its figures ([fields](CLI.md#status)), `longwire-switchboard bans list` its bans (it finds the service's socket by itself), and
  the **Switchboard** window on the desktop shows it live. Stopping it is `systemctl`'s. Deciding its registrations and lifting a ban are for the user it runs as, or root:
  `please longwire-switchboard bans clear KEY`.
- **What it sees.** That a node is online, its public address, who rings whom and when, and the names nodes chose to give it. It keeps
  numbers and listed names on disk (`numbers.log`, `names.log`) and no call history. See [SWITCHBOARD](SWITCHBOARD.md).

## Run a site (splice a whole LAN)

A site box sits on a LAN and makes that LAN part of the line. It needs one capability, `CAP_NET_RAW`, which the service manager grants
and the node drops as soon as the port is open.

```
please apt install longwire
catalogue --system install cli/longwire
echo 'ARGS="--switchboard HOST:6741/lw1… --token YOURTOKEN --answer known"' | please tee /etc/longwire/eth0.conf
please chmod 600 /etc/longwire/eth0.conf
please systemctl enable --now longwire@eth0
```

A token on the service's command line is visible to other users on the machine in the process list while it is there, so use it for the
first start only: once the node has registered, the switchboard knows its key, and you can delete `--token …` from the file and restart.

Replace `eth0` with the NIC on the LAN (the instance name after the `@` is the NIC). Each instance has its own identity and state
(`/var/lib/longwire/eth0/`), so two NICs are two nodes. Use the node from a terminal or the desktop with
`LW_SOCK=/run/longwire/eth0/ctl longwire show` if you are in the group `longwire` (`please usermod -aG longwire YOU`, then log in again).
Members of that group can dial, hang up and evict through the socket; they cannot stop the service or read a key. `longwire answer` does not reach a service (it writes
your own `~/.longwire/answer`): change `--answer` in `/etc/longwire/eth0.conf` and restart. Its log, including why it has no number, is `please journalctl -u longwire@eth0`.

**A machine that joins the bridged network.** A machine with a jack that takes an address in the network you bridge would send its own packets for the site box
into the tunnel. longwire sends them out through a socket of their own, tied to the machine's real network device (the default route's, or `--underlay DEV`), so
the call comes up with no route added by hand ([ADR 0037](adr/0037-traffic-to-the-other-end-never-goes-into-our-own-jack.md)). Two things can still stop it: a kernel before 5.7
(only root may tie a socket to a device; `longwire watch` then says the address is not used) and strict reverse-path filtering on the joining machine
(`rp_filter=1` drops the site box's replies because it believes the site box is behind the jack; `2` or `0` is fine). Where neither can be changed, give the site box
an address outside the bridged network (a second, not-bridged leg with `--listen ADDR:PORT`) or a forwarded public port.

If two boxes sit on one LAN segment, they elect one to forward and the other stands by, so frames are not carried twice. Give them the same
site key if they are not on one line (`--site-key FILE`, [ARCHITECTURE](ARCHITECTURE.md)).

## Run one machine in the background

Without a desktop, save a connection and turn it on:

```
longwire profile set home switchboard=HOST:6741/lw1… attach=jack interface=lw0
longwire profile token home          # the token on standard input, out of the process list and the shell history
longwire on
```

`longwire` with nothing after it does the same from the default profile and then shows how it is. To bring it up at login, run `longwire on`
from your session start-up. A node started this way keeps running after you log out of the terminal; `longwire off` stops it.

## Lines and operators

A line is the set of machines that are connected as one network. By default members **vouch** for new members. For a line you control,
an **operator** key admits and evicts instead:

```
longwire operator new operator.seed                         # make the key; keep this file offline, on a machine that runs no node
longwire operator authority operator.seed                   # its public key
longwire up --authority OPERATORPUBKEY …                    # the first node founds the line
longwire operator admit operator.seed LINEID NODE > admit    # sign an admit for a node (its lw1… address)
longwire up --admit admit …                                  # that node presents it when it calls a member
longwire operator evict operator.seed LINEID NODE            # sign an eviction; then carry it: longwire revoke EVICTION
```

See [ADR 0026](adr/0026-a-line-may-have-an-operator.md). A lost operator key means nobody new can join and nobody can be evicted
until a new line is founded; a stolen one lets its holder admit and evict at will. Treat it like a signing key.

## Back up and restore the identity

Your number is your 32-byte seed. Print it once as 33 words (32 for the seed, one of checksum):

```
longwire key export
```

Write the words on paper or keep them in a password manager. To restore on a new machine:

```
longwire key import          # then type or paste the words on standard input
```

The number comes back the next time the machine registers (a switchboard gives a key the same number it had, unless it was released
after a year unheard). Anyone who has the words *is* you; do not email them.

## Upgrade

longwire comes from two places, and whatever is running keeps the engine it started with:

| Part | From | Update |
|---|---|---|
| the engine (`longwire.so`), the launchers, the root helpers, the units, the man pages | the package | `please apt update && please apt install --only-upgrade longwire` (dnf, pacman alike) |
| the scripts, the pane, the menu bar telephone, the Switchboard window | hibr's catalogue | `catalogue refresh && catalogue update`; for the services `catalogue --system update` |

Then restart what runs:

```
longwire off && longwire on                       # a node you turned on (or Turn off, Turn on in the pane)
please systemctl restart longwire@eth0            # a site node
please systemctl restart longwire-switchboard     # a switchboard: this also forgets its bans
longwire doctor                                   # do the scripts, the installed engine and the running node agree?
LW_SOCK=/run/longwire/eth0/ctl longwire show      # the engine line of a site node
```

A release note that says it is a switchboard fix needs the package on the switchboard's machine and that restart; a switchboard's status does not name its
engine. If `doctor` still says the package is older after the upgrade, `apt policy longwire` shows what is installed and what the repository offers (apt
never moves to a lower version on its own: a machine that took the withdrawn 0.6.0 needs `please apt install --allow-downgrades longwire=VERSION-1`). A script
newer than the module can fail with `unknown option` (for example, `--max-numbers` against an older module): upgrade the package first.

## Remove

```
please systemctl disable --now longwire-switchboard longwire@eth0   # whichever you ran
catalogue remove longwire                                         # the bundle: the command, the pane, the telephone and the strip module
catalogue --system remove cli/longwire cli/longwire-switchboard   # the scripts installed for the whole machine
please apt remove longwire
```

This keeps your identity and profiles (`~/.longwire`, `~/.config/longwire`, `/var/lib/longwire*`). Delete them only if you mean to give
up the number.

## Ports, firewalls and NATs

- A **switchboard** needs UDP inbound on its listen port and its echo port (the listen port + 2747), and, while nodes before 0.5.36 remain, the listen port + 1:
  `please ufw allow 6741,9488,6742/udp`, or in an nftables `inet filter input` chain `please nft add rule inet filter input udp dport '{ 6741, 9488, 6742 }' accept`. A provider's firewall in
  front of the machine needs the same. A switchboard that keeps `--listen 1877` from before keeps its port; its echo port becomes 4624, and 1878 still answers.
- A **node** needs no inbound port: it opens one outbound to the switchboard and punches through its NAT. It listens on a port of its
  own choosing (`--listen` fixes it); a firewall that allows outbound UDP and its replies is enough.
- Two machines behind ordinary home routers connect directly. Two behind *strict* (symmetric) NATs need a relay: another machine of
  your own line with a public address. A line with none says it is not a full mesh in `longwire watch` and `longwire up`, and `longwire show` shows the NAT type.

## When a node gets no number

A node with no session with its switchboard after 15 seconds says so in its log (`please journalctl -u longwire@NIC`, or `~/.longwire/log` after `longwire on`), in `longwire watch`, and in
`longwire show` (`⚠ no answer from it for N s: …`). At 12 seconds it sent one 40-byte packet to the echo ports of the switchboard's machine (its port + 2747, and its port + 1, which an older switchboard uses), which answer anyone, banned or not, and when that came back, one as large as a
handshake datagram (1276 bytes on the wire).
What came back decides the sentence ([ADR 0048](adr/0048-a-silent-switchboard-is-said-so.md)):

- **small packets come back, … but one as large as a handshake's does not**: something between this node and the switchboard loses large or fragmented UDP. Look at the node's side first: an
  interface with an MTU under 1280 (a tunnel, a container's network, another VPN: `ip link`), or a firewall or middlebox that drops IP fragments. Once large packets get through, the node registers.
- **its host answers**: UDP of every size goes there and back, but no handshake completes. Check, in this order:
  1. the key after `/` in the switchboard address is the switchboard's own: compare with `please journalctl -u longwire-switchboard -o cat | grep '^switchboard '` on it;
  2. this node's key is not banned: `please longwire-switchboard bans list` on the switchboard, or a `ban` line in its log starting with the first 16 hex digits of the node's `longwire id`.
     Lift it with `please longwire-switchboard bans clear KEY`, or wait (15 minutes, doubling, at most a day);
  3. the switchboard's own port is open, not only the next one;
  4. the switchboard's answers get back: the large probe tests the way there; a handshake's answer is as large on the way back ([PROTOCOL](PROTOCOL.md)), which the probe does not test.
- **nothing comes back from its host**: UDP to the switchboard's echo port is dropped on the way (either side's firewall, or the provider's), or the switchboard is not running
  (`systemctl status longwire-switchboard`; `please ss -ulpn` shows both ports bound). A switchboard whose firewall opens only its own port, or whose echo port was taken by something
  else, also lands here, whatever the real cause.
- **no reason given** (the sentence lists every cause): the switchboard's address is IPv6, which the probe does not cover.
