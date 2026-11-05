# Quick start: two machines on one line

About ten minutes. At the end, two machines in different places can reach each other as if they were plugged into the same
switch, and you will have seen how to turn it on and off, how a call is answered, and how to check nobody is listening in.

Every step is shown two ways: **in the desktop** (hibr's Control Panel and menu bar) and **on the command line**. Use whichever you like;
they do the same thing and share one saved connection, so you can mix them.

## What you need

- **Two Linux machines** with hibr installed (the package asks for 0.99.430 or newer, the first with module ABI 17; the menu bar
  telephone wants 0.99.351 or newer, and the Blit telephone icons 0.99.364). `please` (hibr's way to run one command as root) and
  **Control Panel > Administrative Access**, as this guide uses them, need hibr 0.99.390 or newer: on an older hibr write `sudo` where it says `please`. Call them **A** and **B**.
- **A switchboard** both can reach. There is no public one to borrow, so you run your own: see [OPERATING](OPERATING.md)
  ("Run a switchboard", about five minutes on any small server). You will need three things from it:
  its address `HOST:6741`, its **key** (it prints `lw1…` when it starts), and, if you gave it tokens, **one token for each machine**.
- **Your administrator password** on each machine, to make the network device. It is asked for through polkit (`pkexec`; you can tune it in Control Panel > Administrative Access) the first time, and again after a
  restart, because the device does not survive one. Set *Set up the network device longwire uses on this computer* (`org.longwire.jack`) to *No Password, Just Me* (hibr 0.99.444 or newer; before that, no password for your own group), and the pane shows the current setting there and turning on and off asks none;
  sending everything through the line and sharing the internet are a second action, `org.longwire.everything`, which still asks
  ([ADR 0058](adr/0058-the-device-and-the-whole-machine-are-two-permissions.md)).

## 1. Install

On each machine:

```
please apt install longwire             # or: please dnf install longwire, please pacman -S longwire
catalogue refresh
catalogue install longwire            # the command, the Control Panel pane, the menu bar telephone and the Control Strip module
```

`apt install longwire` brings the engine, the launchers, the helper that makes the network device, and the manual pages
(`man longwire`). The catalogue brings the parts you see. For a server with no desktop, `catalogue --system install cli/longwire`
is enough.

## 2. Connect machine A

### In the desktop

1. Open **Control Panel → Longwire**. It says **Longwire is off** and shows a form.
2. Fill in:
   - **Switchboard:** `HOST:6741/lw1…`, the address, a slash, and the key.
   - **Token:** the token for this machine, if your switchboard wants one.
   - **Attach:** `jack`. **Interface:** `lw0`.
   - **Name:** optional, a name callers will see. It is a claim, never proof.
3. Press **Turn on**. When the network device has to be made (the first time, and after a restart), a dialog asks for your administrator password.
4. A note says **on, your number is 447-120-933**. A telephone `☎ ●` appears on the menu bar.

### On the command line

```
longwire profile set home switchboard=HOST:6741/lw1… attach=jack interface=lw0
longwire profile token home           # type the token, then Enter (it stays out of your shell history)
longwire on
```

`on` makes the network device if it is missing (it asks for your password then), starts the node in the background, and prints
`longwire: on, your number is 447-120-933`.

Either way, A now has a **number**. Look at it any time with `longwire show` or by clicking the telephone.

## 3. Connect machine B

Do the same on B, with B's own token. B gets its own number, say `312-884-076`.

## 4. Give the devices an address

longwire carries Ethernet frames; it does not hand out IP addresses. Give each machine its own address in one subnet (any private range):
in the Control Panel pane press **Address** and type `10.77.0.1/24` on A and `10.77.0.2/24` on B, or from a terminal

```
longwire profile set home address=10.77.0.1/24        # on A
longwire profile set home address=10.77.0.2/24        # on B
```

**Turn on** (or `longwire on`) then puts it on the device, in the same password step that makes the device; after a restart, once more. By hand
instead, as root: `ip addr add 10.77.0.1/24 dev lw0`.

If a DHCP server sits on the other side (a router at a site you have bridged), ask it instead: type `dhcp` in the **Address** row, or `longwire profile set home address=dhcp`. The same password step starts a DHCP client on the jack (`dhclient`, `dhcpcd` or `udhcpc`, whichever is installed); it keeps asking until the line answers, and it takes the address and the network route only: never a default route and never name servers, so that a line cannot take over your machine's traffic.

### Beyond the bridged subnet

The line is one segment: everything on it is reached with no route. A network *behind* it (a site's other subnets) is reached through a router that sits on the segment. Put that router in **Gateway** and the networks in **Routes**
(or `longwire profile set home gateway=192.168.152.254 routes=192.168.150.0/24`), and turn on: the routes go on with the device. Ask whoever runs the far site which networks its router reaches. Routes are never the default route
([ADR 0044](adr/0044-routes-beyond-the-line-go-through-a-gateway-on-it.md)), and they need a static Address (not `dhcp`) in the same network as the Gateway.

### Everything through the line

To have all of this machine's traffic leave by another member's internet (a laptop on a café's Wi-Fi going out by a server you run, or by home), let one member share its internet and the others
send everything through the line ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)). No gateway address is typed anywhere, and nothing is set up by hand on either side:

- **On the machine that shares** (a server, like your VPS): give its jack a fixed Address, then switch on **Share this machine's internet** in the pane, or
  `longwire profile set home address=192.168.152.1/24` and `longwire exit on`. The same password step as the device turns on forwarding and adds a firewall table of the jack's own
  (it needs nftables: `nft`); `longwire show` then says `sharing  this machine's internet with the line`.
- **On each machine that goes out through it:** switch on **Send everything through the line** and leave **Gateway** empty (`longwire profile set home everything=yes`; it needs an Address,
  fixed or `dhcp`), then turn on. `longwire show` says `everything  through the line (lw0)` and `way out  447-120-933, which shares its internet`. Set **Name servers** to any resolver, like
  `1.1.1.1`, or up to three (`longwire profile set home dns=1.1.1.1,9.9.9.9`): it is asked through the line and goes out by the exit, so the network you sit on does not see what you look up (with systemd-resolved).

When more than one member shares, the lowest number is used, or the one in **Through** (`exit_through=NUMBER`). The machine that shares sees everything the others send outside the line, as their
internet provider would, and its address is what the internet sees: share only from a machine the line trusts that much. If no member shares, nothing goes out (it does not leak to the network you
sit on), and `longwire show` warns `way out: no member of the line shares its internet now`. Your own network (the printer) stays yours, and the moment longwire is off or stops, everything goes the
usual way again.

If the far site already has a router on the line that passes traffic on (a home or office router does for any machine on its network), you can name it in **Gateway** instead
(`longwire profile set home gateway=192.168.152.254 everything=yes`; then the Address must be static); **Name servers** is then often that same router.

Moving from Wi-Fi to a cable while on is handled: longwire lets go for a moment, so the machine uses its own network meanwhile, and starts again on the new one
(tested on the engine; a laptop really moving has not been tried). It fails open: while longwire is off or restarting, traffic goes the usual way; a switch that blocks everything while the line is down
*(planned: M4)*. Switching this on or off takes effect at the next Turn on. IPv6 is sent into the line too, so it does not leak past it, and it works only if the far side routes it; an exit does not
carry it out ([ADR 0045](adr/0045-everything-through-the-line-is-a-table-of-the-jacks-own.md), [ADR 0046](adr/0046-a-node-that-takes-everything-lets-go-when-the-road-changes.md)).
Sharing is tested on the engine and on the real kernel in network namespaces; a real exit on the internet has not been tried.

## 5. Make the first call

From **B**, ring A's number.

**In the desktop:** click the telephone on the menu bar and choose **Dial…**, or use **Dial…** in Control Panel → Longwire, and type `447-120-933`.

**On the command line:** `longwire dial 447-120-933`.

On **A** the telephone shows `☎ ◉ 312-884-076` and a dialog appears (on the command line, run `longwire watch` to see it):

```
☎  312-884-076 is calling
   the safety phrase comes once you are connected: compare it with theirs
   picking up adds 312-884-076 to your network (your line): it stays a member, and reconnects by itself, until you evict it
   first call from this number
   run: longwire pickup | longwire reject
```

### Pick up, then check the safety phrase

**Pick up** in the dialog, or `longwire pickup`. A moment after the call connects, both ends get the same **eight-word safety phrase**. Neither side has it
before: it is made from numbers the two ends exchange once the call is up. On the desktop a note tells it, and it stays under the call in the pane and in the
telephone's menu; on the command line it is in `longwire watch` (or the terminal running `longwire up`), `longwire show` and `longwire who NUMBER`.

**Compare the words on another channel at once** (a message, a phone call). If they match, you have good evidence that nobody is between you: a switchboard that
put itself in the middle would have about one chance in 2^64 per call ([SECURITY](SECURITY.md#the-safety-phrase-honestly)). If they differ, **hang up at once**
(`longwire hangup`): the call is already on your line. After this first call the other machine's key is pinned.

**Or let longwire compare.** Type the words the other end shows into **Verify…** under the call in the pane, or `longwire verify 312-884-076 word word …`: a match is
recorded and the call is marked *verified*; a difference ends the call. On a server you cannot see, that one command in its console, with the words read off your own
screen, is the whole check ([ADR 0054](adr/0054-a-phrase-is-checked-by-typing-the-other-ends-words.md)).

## 6. See that it works

```
ping 10.77.0.2            # from A
ping 10.77.0.1            # from B
```

Anything that works on a LAN works now: file shares, printers, `ssh`, mDNS names.

## Day to day

| I want to… | Desktop | Command line |
|---|---|---|
| turn it on | menu bar telephone → **Turn On**, or the pane's **Turn on** | `longwire on` (or just `longwire`) |
| turn it off | telephone → **Turn Off** | `longwire off` |
| see how it is | the telephone and its menu | `longwire show` |
| watch what happens | the telephone rings and menus update | `longwire watch` |
| call someone | telephone → **Dial…** | `longwire dial NUMBER` |
| answer a call | the dialog, or telephone → **Pick up** | `longwire pickup` |
| hang up | pane → the connection's row | `longwire hangup NUMBER` (with no number: every call, and leave the line) |
| change the connection | pane → the form | `longwire profile set …` |
| see a call's safety phrase | under the call in the pane, or the telephone's menu | `longwire show`, or `longwire who NUMBER` |
| check an update took | the pane's **Out of step** rows | `longwire doctor` |

A number you have answered before is recognised: set **When a call rings** to `known` and those calls are answered by themselves.

## Next

- [DESKTOP](DESKTOP.md): every part of the graphical side.
- [OPERATING](OPERATING.md): run a switchboard, run a site as a service, back up your identity, upgrade.
- [CLI](CLI.md) and `man longwire`: every command and option.
- [SECURITY](SECURITY.md): what protects you, and what does not.
- [TROUBLESHOOTING](TROUBLESHOOTING.md): when something does not work.
