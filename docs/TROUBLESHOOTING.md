# Troubleshooting

Find the message or the symptom, read the cause, do the fix. Messages are quoted as longwire prints them. When you ask for help, send
the output of `longwire show`, `longwire status` and the last lines of `~/.longwire/log` (a switchboard: `journalctl -u longwire-switchboard`).
Tokens are secrets: leave them out of what you send.

## Starting

**"☎ your number 447-120-933 — dial tone", and then nothing happens.**
Nothing is wrong. `longwire up` *is* the node: it stays in the foreground and prints events as they happen. Use another terminal for
commands, or start it in the background with `longwire on`. Ctrl-C stops the node.

**`lw start: need --splice NIC, --jack TAPNAME (or --sock-port LOCAL:PEER for tests)`**
No attach mode was given. Save a connection that has one (`longwire profile set home … attach=jack interface=lw0`) and use
`longwire on`, or fill in **Attach** and **Interface** in Control Panel → Longwire.

**`longwire: no profile yet. Make one:` …**
There is no saved connection. Fill in the Control Panel form, or `longwire profile set home switchboard=HOST:PORT/KEY …`.

**`longwire: the jack helper is not installed (the longwire package has it)` / `there is no pkexec to ask for the password with`**
A single machine needs a network device (the *jack*). `longwire jack` makes it after one password, through the helper
`/usr/lib/longwire/jack` and polkit's `pkexec`. If the package is missing the helper, upgrade it; if `pkexec` is missing, install polkit;
or follow the two commands the message prints, as root, once.

**Control Panel says "Could not start pkexec" or "Could not make the jack".**
Same causes. Run `longwire jack` in a terminal to see the full instructions. If you cancelled the password dialog, press **Turn on** again.

**`longwire: it did not start:` and some lines**
Those are the end of `~/.longwire/log`. Common causes are a wrong switchboard address, a token the switchboard refuses (below), or a
port already in use (`--listen`).

**`unknown option --max-numbers` (or any option) from a script**
The script is newer than the engine. Upgrade the package first: `sudo apt install --only-upgrade longwire`, then restart.

## Registering

**`the switchboard refused to register you: a valid token is needed to register here`**
The switchboard wants a token and you gave none, or a wrong or already-used one. Ask its operator for a fresh token and put it in the
profile (`longwire profile token home`, then type it). A machine that has registered once does not need one again.

**`… a token has no spaces, quotes or backslashes, and does not start with #`**
The token has a character a token may not. Tokens made by `openssl rand -hex 10` are fine.

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
`longwire show` shows the NAT type.

**`⚠ KEY CHANGED for 447-120-933: refused the call` / `call dropped`**
A number you have called before answered with a *different* key. Either the machine was reinstalled, or someone else is answering as it.
Check with the person over another channel. If it is legitimate, `longwire unpin 447-120-933`; the next call is then a first call. If you
cannot confirm, leave it.

**The safety phrases differ.**
Reject. Either it is the wrong number or someone is in the middle. Do not pick up.

## It is connected, but nothing works

**`ping` does not reply.**
longwire carries Ethernet frames; it does not give addresses. Both ends need an address in one subnet: `ip addr show lw0` on each, then
`sudo ip addr add 10.77.0.1/24 dev lw0` and `10.77.0.2/24` on the other, or `sudo dhclient lw0` if a DHCP server is on the line. Check a
firewall on the machine is not dropping traffic on `lw0`. Control Panel can set the address for you *(planned: M4)*.

**Big transfers stall or are slow.**
The jack's MTU is 1400 so a frame fits one datagram. If you made the device by hand, set `mtu 1400` (`longwire setup jack` prints it).
The cipher alone runs near 290 Mbit/s per core under the project's compiler; end to end it has been measured only in the test suite.

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
are in the group `longwire`: `sudo usermod -aG longwire YOU`, then log in again.

**A call rings but I hear nothing.**
A ring is heard while the desktop runs. If it is not running, the node still applies its policy (`auto` and `known` answer by themselves; under `ask` the ring waits a minute and then times out); use `longwire watch` to see rings.

## Services and switchboards

**The service will not start.**
`journalctl -u longwire-switchboard -n 50` (or `longwire@eth0`). The usual causes: the arguments file is missing
(`/etc/longwire/switchboard.conf`, `/etc/longwire/eth0.conf`), the script is not installed for the machine
(`sudo catalogue --system install cli/longwire-switchboard`, or `cli/longwire`), a token file named in `--tokens` does not exist yet
(create it in the state directory), or an option has no value.

**Nodes cannot reach the switchboard.**
Open UDP 1877 and 1878. From a node: its status line says **dial tone** when it can.

**I lost the switchboard's address.**
`sudo hibr -c 'mod load longwire; lw key address /var/lib/longwire-switchboard/seed'`.

**I lost my seed / moved to a new machine.**
`longwire key import` and type the 33 words you exported. You get the same number back when you register. With no words, the number is gone.

## Still stuck

Send the three outputs above to whoever runs your switchboard, or to the maintainer. The design is described in
[ARCHITECTURE](ARCHITECTURE.md) and [PROTOCOL](PROTOCOL.md), and what is known to be untested is listed in [SECURITY](SECURITY.md) and
the [ROADMAP](ROADMAP.md).
