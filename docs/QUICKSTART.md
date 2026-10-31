# Quick start: two machines on one line

About ten minutes. At the end, two machines in different places can reach each other as if they were plugged into the same
switch, and you will have seen how to turn it on and off, how a call is answered, and how to check nobody is listening in.

Every step is shown two ways: **in the desktop** (hibr's Control Panel and menu bar) and **on the command line**. Use whichever you like;
they do the same thing and share one saved connection, so you can mix them.

## What you need

- **Two Linux machines** with hibr installed (the package asks for 0.99.315 or newer; the menu bar
  telephone wants 0.99.351 or newer, and the Blit telephone icons 0.99.364). Call them **A** and **B**.
- **A switchboard** both can reach. There is no public one to borrow, so you run your own: see [OPERATING](OPERATING.md)
  ("Run a switchboard", about five minutes on any small server). You will need three things from it:
  its address `HOST:1877`, its **key** (it prints `lw1…` when it starts), and, if you gave it tokens, **one token for each machine**.
- **Your administrator password** on each machine, to make the network device. It is asked for through polkit (`pkexec`; you can tune it in Control Panel > Administrative Access) the first time, and again after a
  restart, because the device does not survive one.

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
   - **Switchboard:** `HOST:1877/lw1…`, the address, a slash, and the key.
   - **Token:** the token for this machine, if your switchboard wants one.
   - **Attach:** `jack`. **Interface:** `lw0`.
   - **Name:** optional, a name callers will see. It is a claim, never proof.
3. Press **Turn on**. When the network device has to be made (the first time, and after a restart), a dialog asks for your administrator password.
4. A note says **on, your number is 447-120-933**. A telephone `☎ ●` appears on the menu bar.

### On the command line

```
longwire profile set home switchboard=HOST:1877/lw1… token=YOURTOKEN attach=jack interface=lw0
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
([ADR 0044](adr/0044-routes-beyond-the-line-go-through-a-gateway-on-it.md)), and they need a static Address.

## 5. Make the first call

From **B**, ring A's number.

**In the desktop:** click the telephone on the menu bar and choose **Dial…**, or use **Dial…** in Control Panel → Longwire, and type `447-120-933`.

**On the command line:** `longwire dial 447-120-933`.

On **A** the telephone shows `☎ ◉ 312-884-076` and a dialog appears (on the command line, run `longwire watch` to see it):

```
☎  312-884-076 is calling
   safety phrase: gopher daisy castle cactus island flint
   first call from this number
   run: longwire pickup | longwire reject
```

### Pick up, then check the safety phrase

**Pick up** in the dialog, or `longwire pickup`. Both sides print `✓ connected`, with the path (`direct` is the good one), and a moment later the eight-word
safety phrase. Neither side has it before, because it is made from numbers the two ends exchange once the call is up.

**Compare the words on another channel at once** (a message, a phone call). If they match, you have good evidence that nobody is between
you. If they differ, **hang up at once** (`longwire hangup`): the other machine is already on your line. The phrase is short, and a
switchboard that is itself the attacker has work to do but not an impossible job ([SECURITY](SECURITY.md#the-safety-phrase-honestly)); if the
switchboard is yours, you are safe from that. After this first call the other machine's key is pinned.

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

A number you have answered before is recognised: set **When a call rings** to `known` and those calls are answered by themselves.

## Next

- [DESKTOP](DESKTOP.md): every part of the graphical side.
- [OPERATING](OPERATING.md): run a switchboard, run a site as a service, back up your identity, upgrade.
- [CLI](CLI.md) and `man longwire`: every command and option.
- [SECURITY](SECURITY.md): what protects you, and what does not.
- [TROUBLESHOOTING](TROUBLESHOOTING.md): when something does not work.
