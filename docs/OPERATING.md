# Operating longwire

For whoever runs a switchboard, a site, or a machine that should stay on. Commands assume Debian-style paths and `sudo`; the
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
| the network device helper (runs as root, through polkit) | `/usr/lib/longwire/jack` |
| the engine | `/usr/lib/hibr/longwire.so` |
| the scripts | hibr's catalogue: `catalogue install cli/longwire`, `cli/longwire-switchboard` |
| the services | `longwire-switchboard.service`, `longwire@NIC.service` |
| service arguments | `/etc/longwire/switchboard.conf`, `/etc/longwire/NIC.conf` |
| a service's state | `/var/lib/longwire-switchboard/` (its identity is `seed` there); `/var/lib/longwire/NIC/` (its identity is `.longwire/seed` inside) |
| a service's control socket | `/run/longwire-switchboard/ctl`, `/run/longwire/NIC/ctl` |
| manual pages | `man longwire`, `longwire-switchboard`, `longwire-profiles`, `longwire-security` |

## Run a switchboard

The switchboard gives nodes their numbers and introduces two nodes that want to talk. It is not on the data path, so a small server
is enough, but it must be reachable from the internet on **UDP 1877 and 1878** (1878 answers the NAT-type check).

```
sudo apt install longwire
sudo catalogue --system install cli/longwire-switchboard
sudo mkdir -p /etc/longwire
echo 'ARGS="--listen 1877 --registry none --tokens /var/lib/longwire-switchboard/tokens"' | sudo tee /etc/longwire/switchboard.conf
sudo systemctl enable --now longwire-switchboard
```

The service retries every couple of seconds until the token file exists, which is expected: the same start creates its state
directory. Then make the tokens (one per machine you want to let in, plus a spare):

```
sudo install -m 0600 /dev/null /var/lib/longwire-switchboard/tokens
sudo chown "$(stat -c %u /var/lib/private/longwire-switchboard)" /var/lib/longwire-switchboard/tokens
for i in 1 2 3; do openssl rand -hex 10; done | sudo tee /var/lib/longwire-switchboard/tokens
```

The file belongs to the service's own user and nobody else can read it. (A group-readable file would let anyone in the group `longwire` read, and
use, the tokens that have not been spent.) These steps have not been run under a live systemd by the author.

Open the firewall (`sudo ufw allow 1877:1878/udp`, or your own tool), read its address, and give each user a token:

```
sudo journalctl -u longwire-switchboard -n 10 --no-pager | grep switchboard
# switchboard  lw1q7m…k2a   (give this full address to your nodes)
sudo hibr -c 'mod load longwire; lw key address /var/lib/longwire-switchboard/seed'    # the same, any time, from its identity file
```

Nodes use `--switchboard YOUR.HOST:1877/lw1q7m…k2a`.

- **`--registry none`** makes it a private switchboard that keeps its own code. A shared public registry of switchboards is a later step
  (see [SWITCHBOARD](SWITCHBOARD.md)).
- **Tokens.** Open registration (no `--tokens`) lets anyone take a number. For a switchboard that matters, use tokens. A token is spent on first
  use; a machine that has registered once is recognised by its key afterwards. Add one with
  `openssl rand -hex 10 | sudo tee -a /var/lib/longwire-switchboard/tokens`. The file must be in the state directory: the service cannot
  write `/etc`.
- **Limits.** Defaults are 1,024 sessions (`--max-nodes`), 100,000 numbers (`--max-numbers`) and 20 registrations per source per hour; the unit's
  memory limit is 1 GB, which suits the defaults. Raise the limit with the numbers.
- **Back up the seed.** `/var/lib/longwire-switchboard/seed` is the switchboard's identity. Every node pins its address, so losing it
  means re-pointing every node. Copy it somewhere private (`sudo cp … ~/switchboard-seed.backup; chmod 600 …`) and never share it.
- **Watch it.** `sudo journalctl -u longwire-switchboard -f` for the log. As a member of the group `longwire`, `LW_SOCK=/run/longwire-switchboard/ctl longwire status`
  prints its figures, and the **Switchboard** window on the desktop shows it live. Only the user the service runs as can stop it or decide its registrations.
- **What it sees.** That a node is online, its public address, who rings whom and when, and the names nodes chose to give it. It keeps
  numbers and listed names on disk (`numbers.log`, `names.log`) and no call history. See [SWITCHBOARD](SWITCHBOARD.md).

## Run a site (splice a whole LAN)

A site box sits on a LAN and makes that LAN part of the line. It needs one capability, `CAP_NET_RAW`, which the service manager grants
and the node drops as soon as the port is open.

```
sudo apt install longwire
sudo catalogue --system install cli/longwire
echo 'ARGS="--switchboard HOST:1877/lw1… --token YOURTOKEN --answer known"' | sudo tee /etc/longwire/eth0.conf
sudo chmod 600 /etc/longwire/eth0.conf
sudo systemctl enable --now longwire@eth0
```

A token on the service's command line is visible to other users on the machine in the process list while it is there, so use it for the
first start only: once the node has registered, the switchboard knows its key, and you can delete `--token …` from the file and restart.

Replace `eth0` with the NIC on the LAN (the instance name after the `@` is the NIC). Each instance has its own identity and state
(`/var/lib/longwire/eth0/`), so two NICs are two nodes. Use the node from a terminal or the desktop with
`LW_SOCK=/run/longwire/eth0/ctl longwire show` if you are in the group `longwire` (`sudo usermod -aG longwire YOU`, then log in again).
Members of that group can dial, hang up and evict through the socket; they cannot stop the service or read a key.

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
longwire profile set home switchboard=HOST:1877/lw1… token=… attach=jack interface=lw0
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

The module and the scripts come from two places and must stay in step:

```
sudo apt update && sudo apt install --only-upgrade longwire     # the engine, the launchers, the helper, the man pages
catalogue refresh && catalogue update                            # the scripts, the pane, the menu bar telephone
sudo catalogue --system update                                   # the same, for scripts installed for the whole machine
sudo systemctl restart longwire-switchboard                      # or longwire@NIC, for a service
```

A script newer than the module can fail with `unknown option` (for example, `--max-numbers` against an older module): upgrade the package
first.

## Remove

```
sudo systemctl disable --now longwire-switchboard longwire@eth0   # whichever you ran
catalogue remove longwire                                         # the bundle: the command, the pane, the telephone and the strip module
sudo catalogue --system remove cli/longwire cli/longwire-switchboard   # the scripts installed for the whole machine
sudo apt remove longwire
```

This keeps your identity and profiles (`~/.longwire`, `~/.config/longwire`, `/var/lib/longwire*`). Delete them only if you mean to give
up the number.

## Ports, firewalls and NATs

- A **switchboard** needs UDP 1877 and 1878 open inbound.
- A **node** needs no inbound port: it opens one outbound to the switchboard and punches through its NAT. It listens on a port of its
  own choosing (`--listen` fixes it); a firewall that allows outbound UDP and its replies is enough.
- Two machines behind ordinary home routers connect directly. Two behind *strict* (symmetric) NATs need a relay: another machine of
  your own line with a public address. A line with none says it is not a full mesh in `longwire watch` and `longwire up`, and `longwire show` shows the NAT type.
