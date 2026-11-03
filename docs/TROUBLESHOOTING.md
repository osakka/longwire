# Troubleshooting

Find the message or the symptom, read the cause, do the fix. Messages are quoted as longwire prints them. When you ask for help, send
the output of `longwire show`, `longwire status` and the last lines of `~/.longwire/log` (a switchboard: `journalctl -u longwire-switchboard`).
Tokens are secrets: leave them out of what you send.

## Starting

**Did the update take effect?** The scripts come from the catalogue (`catalogue refresh && catalogue install longwire`), the engine from the package
(`please apt update && please apt install --only-upgrade longwire`), and a running node keeps the engine it started with (`longwire off`, then `longwire on`). `longwire doctor` says whether the scripts, the installed engine and the running node agree, and what to do if not; `longwire show`, `longwire on` and the Control Panel say it too (**Out of step** rows).

**"☎ your number 447-120-933 — dial tone", and then nothing happens.**
Nothing is wrong. `longwire up` *is* the node: it stays in the foreground and prints events as they happen. Use another terminal for
commands, or start it in the background with `longwire on`. Ctrl-C stops the node.

**The password is not accepted ("the password was not accepted (polkit: not authorized)", or pkexec's "This incident has been reported").**
Making or changing the jack (an address, routes, everything through the line, sharing) is done by a root helper through polkit, which asks for a password
first; nothing changes when it is refused. Whose password is asked, or none, is set in Control Panel > Administrative Access: *Set up the network device longwire uses
on this computer (its address and routes)* (`org.longwire.jack`: the device, its address and routes, connect and disconnect), *Send all of this computer's traffic through longwire, or share its
internet* (`org.longwire.everything`), and *Make this computer a longwire site node for a network card* (`org.longwire.site`). Set the first to no password to connect and disconnect,
routes or not, without being asked ([ADR 0058](adr/0058-the-device-and-the-whole-machine-are-two-permissions.md)). It works without one once the jack is as the profile says,
which is why a profile with no routes may turn on while one with routes asks. When your own password is right and still refused, polkit cannot check it:

- polkit 126 and later keeps its PAM file in `/usr/lib/pam.d/polkit-1` (not `/etc/pam.d`) and checks passwords in a sandboxed service,
  `polkit-agent-helper@.service`. An account from LDAP or sssd may be refused there when `please` (sudo) accepts it. The journal says why, right after a refusal:
  `journalctl -b --since "-5min" | grep -iE "polkit|pam_|sss|ldap"`.
- `pkexec true` is no test of this: it asks for an *administrator*, and polkit counts as administrators the group `sudo` only, unless told otherwise
  (`polkit.addAdminRule` in `/etc/polkit-1/rules.d/`). An account that is an administrator through another group is asked for root's password there.
- Until it is fixed, make the jack as root with the helper and your user id, then turn on as usual (nothing more is asked while the jack is as the profile says;
  `keep_jack = yes` keeps it after `longwire off`): `please /usr/lib/longwire/jack --address 192.168.152.51/24 --via 192.168.152.254 --routes 192.168.15.0/24 lw0 $(id -u)`.
  With everything through the line or sharing, the helper is `/usr/lib/longwire/jack-everything` (the same options, with `--everything` or `--share`).
  (`please longwire jack …` does not work: as root, the script installed for your user is not used.)

**"cannot resolve HOST" with Send everything on, and it starts with it off.**
Before 0.5.41 the node looked up the switchboard's name after attaching the jack, and a jack that takes everything takes name lookups too, with no
way out yet. 0.5.41 looks it up first. Update the package (the engine). If it still says so, the name really does not resolve on this machine:
`getent hosts HOST` with longwire off, or use the switchboard's address in the profile.

**`lw start: need --splice NIC, --jack TAPNAME (or --sock-port LOCAL:PEER for tests)`**
No attach mode was given. Save a connection that has one (`longwire profile set home … attach=jack interface=lw0`) and use
`longwire on`, or fill in **Attach** and **Interface** in Control Panel → Longwire.

**`longwire: no profile yet. Make one:` …**
There is no saved connection. Fill in the Control Panel form, or `longwire profile set home switchboard=HOST:PORT/KEY …`.

**`longwire: the jack helper is not installed (the longwire package has it)` / `there is no pkexec to ask for the password with`**
A single machine needs a network device (the *jack*). `longwire jack` makes it after one password, through the helper
`/usr/lib/longwire/jack` and polkit's `pkexec`. If the package is missing the helper, upgrade it; if `pkexec` is missing, install it (`please apt install pkexec`, or `sudo apt install pkexec` on a machine that has no `please` yet);
or follow the two commands the message prints, as root, once.

**`longwire: /usr/lib/longwire/jack-everything is not installed (the longwire package has it): update the package`** / **`longwire-jack: sending everything through the line, or sharing this machine's internet, is its own permission`**
Since 0.5.45 everything through the line and sharing are a second polkit action, `org.longwire.everything`, run through a second copy of the helper ([ADR 0058](adr/0058-the-device-and-the-whole-machine-are-two-permissions.md)).
The scripts are newer than the package: update the package (`please apt install --only-upgrade longwire`). The helper itself says the second when it is run as `jack` with `--everything` or `--share`.

**Control Panel says "Could not start pkexec" or "Could not make the jack".**
Same causes. Run `longwire jack` in a terminal to see the full instructions. If you cancelled the password dialog, press **Turn on** again.

**`longwire: on (profile …), waiting for the switchboard to give a number`**
The node is running but the switchboard has not answered yet (slow, unreachable, or a firewall on UDP 6741). `longwire show` says **dial tone** when it has. If it never does, after 15 s the log
and `longwire show` say why as far as the node can tell (below, `⚠ no answer from the switchboard`).

**Control Panel says "Could not make the site node" (splice), or `longwire: the site helper is not installed`**
A splice profile makes this machine a site node: a service, `longwire@CARD`, written and started by `/usr/lib/longwire/site` after your administrator password. The helper says why it refused:
`there is no network card called …` (check **Interface**: a real card, not `lo` or a jack), `option '…' is not one a site node takes` or `that token has a character a service file cannot carry` (the token or name has a
mark a service file cannot hold: a name is letters, digits and `. _ -`), or `its control socket did not appear` (`systemctl status longwire@CARD` and `journalctl -u longwire@CARD`). Without `pkexec` or the helper, do it by
hand: `longwire setup splice CARD`. Not having ACL tools means the pane cannot reach the service until you join the group `longwire` and log in again.

**`longwire: it did not start:` and some lines**
Those are the end of `~/.longwire/log`. Common causes are a wrong switchboard address, a token the switchboard refuses (below), or a
port already in use (`--listen`).

**`unknown option --max-numbers` (or any option) from a script**
The script is newer than the engine. Upgrade the package first: `please apt update && please apt install --only-upgrade longwire`, then `longwire off` and `longwire on`.

## Registering

**`the switchboard refused to register you: a valid token is needed to register here`**
The switchboard wants a token and you gave none, or a wrong or already-used one. Ask its operator for a fresh token and put it in the
profile (`longwire profile token home`, then type it). A machine that has registered once does not need one again.

**`… a token has no spaces, quotes or backslashes, and does not start with #`**
The token has a character a token may not. Tokens made by `openssl rand -hex 16` are fine.

**`… too many registrations from this address`**
This switchboard allows a limited number of new registrations per address per hour (20 by default). Wait, or ask the operator.

**`… this switchboard has no numbers left to give (--max-numbers)`**
The switchboard is full. Ask its operator to raise `--max-numbers`.

**`no dial tone: not registered with a switchboard`** (when dialing)
The node has no number yet. Check `longwire show`: it should say **dial tone**. If it says it is waiting, the switchboard cannot be reached
(address, port, firewall) or has refused you.

## Calling

**`call failed: unknown number` / `not online` / `declined` / `busy` / `refused by the switchboard` / `no answer` / `that is your own number`**
The number does not exist on this switchboard; it is registered but not on at the moment; the other end said no; it is already on a call
or you are ringing too fast; the switchboard's policy refused the ring (it limits dials per minute); or nobody picked up within a minute.
Check the digits and ask the other side to run `longwire show`.

**`✗ lost the call to 447-120-933: no answer from the other end`**
The other machine went away (off, asleep, network gone). The call comes back by itself if it returns.

**`✓ connected … through a relay: no direct path yet, still trying`**
Both machines are behind strict NATs, so the traffic goes through another machine of your line that has a public address, still encrypted
end to end. It moves to a direct path by itself when one exists. With no such machine on the line, the pair cannot connect;
`longwire show` shows the NAT type. `✗ lost the call to …: the relay went away` means the member relaying for you left; the call comes back when a path does.

**`⚠ KEY CHANGED for 447-120-933: refused the call` / `call dropped`**
A number you have called before answered with a *different* key. Either the machine was reinstalled, or someone else is answering as it.
Check with the person over another channel. If it is legitimate, `longwire unpin 447-120-933`; the next call is then a first call. If you
cannot confirm, leave it.

**The safety phrases differ** (or `longwire verify` says *the words differ*).
Hang up at once (`longwire hangup NUMBER`, or the call's row in the pane). The phrase appears only after the call connects, so the call is already on your line; either it was the wrong
number or someone is in the middle. Do not unpin or call back until you have checked with the person another way.

**`✗ the safety phrase could not be agreed with 447-120-933: …. The call was ended`**
*the other end did not take part in the safety phrase exchange*: it kept talking for 10 s and gave no part of the exchange. It is older than 0.5.41, whose ends agree who commits by their key ids
(every node updates together: there is no way to let it through), or something on the way stripped the exchange. *the other end's reveal does not match what it committed to*, or *the other end did not
reveal its half of the safety phrase*: something altered or dropped the exchange; treat it as the phrases differing.

## It is connected, but nothing works

**`ping` does not reply.**
longwire carries Ethernet frames; it does not give addresses. Both ends need an address in one subnet: `ip addr show lw0` on each. Set it in the
pane (**Address**, like `10.77.0.2/24`) or with `longwire profile set home address=10.77.0.2/24`, and turn on again (a password is asked if the device
does not have it yet); every machine needs its own. Or by hand, `please ip addr add 10.77.0.1/24 dev lw0`, or `address=dhcp` in the profile if a DHCP server is on the line (it needs `dhclient`, `dhcpcd` or `udhcpc` installed: `longwire-jack: no DHCP client found` says so; the lease takes the address and route only, never a default route or name servers).
Check a firewall on the machine is not dropping traffic on `lw0`.

**Big transfers stall or are slow.**
The jack's MTU is 1400 so a frame fits one datagram. If you made the device by hand, set `mtu 1400` (`longwire setup jack` prints it). A path narrower than about 1490 bytes on either
side splits longwire's IPv4 datagrams on the way (since 0.5.33; [ADR 0051](adr/0051-longwires-datagrams-may-be-split-on-the-way.md)), which costs a little speed; a middlebox that drops fragments
loses them: `ping -M do -s 1460 OTHEREND` tells whether the path carries 1488 bytes whole.
The cipher alone runs near 290 Mbit/s per core under the project's compiler; end to end it has been measured only in the test suite.

**A call keeps starting and never connects, after you gave your jack an address in the same network as the other end.** (The other side logs the same number joining the line again
and again.) `ip route get ADDRESS` on your machine, with ADDRESS the other end's address in that network, says `dev lw0`: your own packets for it go into the tunnel. longwire sends them
out through a socket tied to your real network device instead (ADR 0037), so this should not happen. If it does: `longwire watch` says `⚠ not using ADDRESS:PORT to reach the other end: it is inside your jack's own network …` when it could not (a kernel
before 5.7, or no default route: name the device, `longwire off`, then `longwire up --profile home --underlay eth0`; a profile cannot carry it); or the replies are dropped by strict reverse-path filtering (`sysctl net.ipv4.conf.all.rp_filter` and the
one for the real device are `1`: set them to `2`). The way out that always works is an address on the other end that is outside the bridged network ([OPERATING](OPERATING.md), "Run a site").

**I reach the line (192.168.152.x) but not the network behind it (192.168.150.x).**
Your machine has no route there. Set **Gateway** (the router on the line) and **Routes** (the networks behind it) in the pane, or `longwire profile set home gateway=… routes=…`, and turn on again; `ip route get 192.168.150.1`
should then say `via` the gateway `dev lw0`. If it does and there is still no answer, the far network must have a way back to your Address: its router has to know the line's network (it does when it is the line's router).

**Everything through the line is on, but nothing on the internet answers.**
`ip route get 1.1.1.1` should say `dev lw0 table 77…` (with no Gateway, `via 169.254.77.1`: that is right, it is the way out to the member that shares its internet, and no machine has that address).
With no Gateway, look at `longwire show`'s `way out` line first (below). If it names a member, that member is not passing the traffic on: on it, `longwire show` should say `sharing`, and its own
firewall may drop what it forwards (below). With a Gateway, the gateway is not passing the traffic on: it must forward it, and, at the edge of its network, translate it to its own address
(a home router does for its own network's machines; a server is easier made an exit with `longwire exit on`). Try `ping` to the gateway itself first. If `longwire show` says *asked, but … not going
through the line*, turn off and on. To get out at once: turn longwire off; nothing else is needed.

**`⚠ way out: no member of the line shares its internet now, so nothing goes out through the line`** (the pane: **Way out** *none*)
Everything goes through the line with no Gateway, and no member of the line says it shares its internet: so nothing for the internet goes anywhere (it does not leak to the network you sit on).
On the machine that should share: `longwire exit on` (or **Share this machine's internet** in its pane), and it must be on the same line and connected (`longwire peers`). A member stops counting
35 seconds after it last said so, so a member that just went away clears in that time. To use this machine's own internet meanwhile: turn longwire off, or switch off **Send everything through the line**.

**`⚠ way out: 447-120-933 is not sharing its internet now, …`**
The profile names that member in `exit_through` (the pane: **Through**), and it does not share now; another member that does is not used instead. Turn sharing on there, or empty
**Through** (`longwire profile set home exit_through=`) to use whichever shares.

**`longwire-jack: sharing this machine's internet needs nft (the nftables package)`**
The machine that shares needs `nft`: `please apt install nftables` (or the distribution's package of that name), then turn on again. Nothing was changed.

**`longwire-jack: this machine has no default route of its own to share`**
The machine that shares has no way to the internet but the line. Share from a machine whose own default route leaves by a real device.

**`longwire: sharing this machine's internet needs a fixed address on the jack`** / **`longwire-jack: --share needs the jack's own address (--address)`**
The line's network is what is let out, so the machine that shares needs a fixed **Address** (not `dhcp`): `longwire profile set home address=192.168.152.1/24`.

**`… a machine that shares its internet does not send its own through the line`**
A profile has both `exit = yes` and `everything = yes`. Pick one: `longwire exit off`, or `longwire profile set home everything=no`.

**It shares (`longwire show` says `sharing`), and the others see it as their way out, but nothing gets out.**
The exit's own firewall comes first. A machine with Docker, or any firewall whose forward policy is drop, may drop the line's forwarded traffic before longwire's table lets it out:
`please nft list ruleset` shows the chains on the `forward` hook. Let traffic from the jack (`lw0`) out to the machine's own device there, or share from another machine. Also check that the
provider allows the machine to forward at all (some containers do not).

**Everything goes through the line, but names do not resolve.**
`resolvectl dns lw0` names the server asked. It must answer on the line's side: try `resolvectl query example.org`. Empty the **Name servers** to go back to your own. `longwire show` has a `names` line.

**`⚠ no answer from the switchboard … its host answers`**
UDP of every size gets to the switchboard's machine and back, but no handshake completes. In this order: the key after `/` in your switchboard address is not the switchboard's (compare it with a node
that works); this node's key is banned there for a while (on the switchboard, `please longwire-switchboard bans list` shows it, its log says why, and `please longwire-switchboard bans clear KEY`
lifts it; a ban ends by itself, 15 minutes, doubling at each repeat, at most a day; a restart of the switchboard forgets every ban); or the switchboard's own port is closed while its echo port is open ([OPERATING](OPERATING.md#when-a-node-gets-no-number)).

**`⚠ no answer from the switchboard … small packets come back from its host, but one as large as a handshake's (1276 bytes) does not`**
Something between you and the switchboard loses large or fragmented UDP, and a handshake needs it. Look at this machine first: `ip link` for an interface with an MTU under 1280 (a tunnel, a
container's network, another VPN), and a firewall that drops IP fragments. Fix that and the node registers by itself.

**`⚠ no answer from the switchboard … nothing comes back from its host`**
UDP does not get there and back: a firewall on either side (the switchboard's port and its echo port, the port + 2747), or the switchboard is not running.

**`longwire-jack: routes need the jack's own address (--address); with DHCP …`** / **`the gateway … is not on the jack's network`**
Gateway and Routes need a static **Address** (not `dhcp`), and the Gateway must be in that address's network: a machine the line reaches directly. Send everything through the line with no Gateway
needs only an Address, static or `dhcp`.

**`longwire-jack: something else already takes everything (…); turn it off there first`** / **`the place in the rules longwire uses (7700) is taken`**
Another jack, or another program, already sends this machine's traffic somewhere with the same rules. Turn that off, then turn longwire on.

**`longwire-jack: the jack lw0 has carrier with no node on it, or a node is on it now: turn longwire off first`**
Everything through the line is put on only while no node holds the jack, so that a node that stops always gives the machine its own network back. `longwire off`, then `longwire on`.

**`longwire-jack: lw0 is not this user's jack`**
A device of that name belongs to another user of this machine; longwire changes only your own. Choose another **Interface** (`lw1` …).

**`longwire-jack: names still go to this machine's own name server: … needs systemd-resolved`**
Without systemd-resolved, **Name servers** is not used; everything else goes through the line. `longwire show` says so on its `names` line.

**`· the road changed (wlan0 to none): …`**, then **`· waiting for a network`**
With everything through the line, the machine changed network (or lost it). longwire let go at once, so the machine is on its own network, and starts again by itself when there is one
([ADR 0046](adr/0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md)). Nothing to do; if it stays waiting, check the machine's own network.

**`⚠ two DHCP servers answer on this line`**
Two machines on the line are both handing out leases, and clients may get clashing addresses. Turn one off.

**`⚠ the line is not a full mesh: 2 of 3 members reachable`**
Two of your line's members cannot reach each other directly and no member can relay between them. Give the line a member with a public address.

## The desktop

**There is no Longwire in Control Panel.**
`catalogue refresh && catalogue install longwire`, then Refresh Desktop. `catalogue installed` shows what you have.

**There is no telephone on the menu bar.**
It needs hibr 0.99.351 or newer. Then: the switch **Longwire on the Menu Bar** in the pane (or Control Panel → Menu Bar Items), and Refresh
Desktop. With the Blit glyph set and an older hibr you see plain characters instead of Blit's telephone; that needs hibr 0.99.364 and Blit 0.2.44 (the classic rotary phone: hibr 0.99.366 and Blit 0.2.45).

**The pane says "Longwire is off" but a node runs.**
A node started by another user (or a service) has another control socket. The pane finds a service node under `/run/longwire` if you
are in the group `longwire`: `please usermod -aG longwire YOU`, then log in again.

**The Switchboard window shows no traffic, or says "No switchboard answers at …".**
Traffic needs the longwire package 0.5.29 or newer on the switchboard's machine. "No switchboard answers" means it found no control socket: start one, join the group `longwire` for a
service (then log in again), or press `s` (File > Switchboard Socket…) to say where its socket is.

**A call rings but I hear nothing.**
A ring is heard while the desktop runs. If it is not running, the node still applies its policy (`auto` and `known` answer by themselves; under `ask` the ring waits a minute and then times out); use `longwire watch` to see rings.

## Services and switchboards

**The service will not start.**
`journalctl -u longwire-switchboard -n 50` (or `longwire@eth0`). The usual causes: the arguments file is missing
(`/etc/longwire/switchboard.conf`, `/etc/longwire/eth0.conf`), the script is not installed for the machine
(`catalogue --system install cli/longwire-switchboard`, or `cli/longwire`), a token file named in `--tokens` does not exist yet
(create it in the state directory), or an option has no value.

**Nodes cannot reach the switchboard.**
Open UDP on its listen port and its echo port, the listen port + 2747 (6741 and 9488 by default). From a node: its status line says **dial tone** when it can, and after 15 s without it the node says why
([OPERATING](OPERATING.md#when-a-node-gets-no-number)).

**I cannot lift a ban: "only the user this runs as may do that".**
The service runs as a transient user; lift it as root: `please longwire-switchboard bans clear KEY` (it finds the service's socket by itself).

**I lost the switchboard's address.**
`please hibr -c 'mod load longwire; lw key address /var/lib/longwire-switchboard/seed'`.

**I lost my seed / moved to a new machine.**
`longwire key import` and type the 33 words you exported. You get the same number back when you register. With no words, the number is gone.

## Still stuck

Send the three outputs above to whoever runs your switchboard, or to the maintainer. The design is described in
[ARCHITECTURE](ARCHITECTURE.md) and [PROTOCOL](PROTOCOL.md), and what is known to be untested is listed in [SECURITY](SECURITY.md) and
the [ROADMAP](ROADMAP.md).
