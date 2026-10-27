# longwire on the desktop

Everything longwire does can be done from hibr's desktop without opening a terminal. This guide is the tour. For the first
connection, start with the [QUICKSTART](QUICKSTART.md).

There are four things on the desktop, and they all talk to the same node:

| | Where | What it is for |
|---|---|---|
| **The Longwire pane** | Control Panel → Longwire | set up a connection, turn it on and off, dial, answer, see the line |
| **The telephone** | the menu bar | whether it is on, your number, a ring that interrupts, a menu of what you do with a phone line |
| **The strip telephone** | the Control Strip | the same, as one glyph that changes colour |
| **The Switchboard window** | the hibr menu → Switchboard | for someone who *runs* a switchboard: who is on it and who is ringing whom |

They share one saved connection (a **profile**), so a change in one shows in the others.

## Install

```
sudo apt install longwire        # or dnf, or pacman: the engine, the launchers, the network-device helper, man pages
catalogue refresh
catalogue install longwire       # the command, the pane, the menu bar telephone and the strip module, together
```

Then **Refresh Desktop** (or log in again). The pane is under **Apps** in the Control Panel; the telephone is on the menu bar.
The menu bar telephone needs hibr 0.99.351 or newer; the Blit telephone icons need 0.99.364 or newer and the Blit glyph set.
On an older hibr the pane and the command work and the telephone is simply absent.

## The Longwire pane

### When it is off

The page says **Longwire is off** and shows the connection:

| Row | What it is |
|---|---|
| **Profile** | the saved connection being shown. Click it (or use the arrows) to switch to another. |
| **Switchboard** | where to register: `HOST:PORT/KEY`, the address, a slash, and the switchboard's key (`lw1…`). |
| **Token** | the invitation your switchboard gave you, if it wants one. It is typed into a masked box and kept in a file only you can read. The row shows `none` or `set (14 characters)`, never the token itself. |
| **Attach** | **jack** for one machine, **splice** for a whole site. Splice needs a one-time step; the pane tells you the command. |
| **Interface** | for a jack, the network device (`lw0`; it is made for you); for a splice, the network card. |
| **Name** | what callers see when you ring them. A claim, never proof. |
| **List in the directory** | on: others can find you by name on the switchboard. Off by default. |
| **When a call rings** | how this machine answers: `ask`, `known`, `auto` or `never` (see below). |
| **Longwire on the Menu Bar** | show or hide the telephone. |
| **Turn on** | does everything: makes the network device if it is missing (that asks for your password), starts the node in the background, and tells you your number. |
| **New profile…** / **Remove profile** | keep several connections (home, office, a friend's switchboard) and switch between them. |

The first profile you make becomes the default. When you press **Turn on** and the network device has to be made (the first time, and after each restart, because the device does
not survive one), a dialog asks for your administrator password. It is only for making the device, and polkit remembers an approval for a few minutes. If `pkexec` is not
installed, or the password is refused, the pane says it could not make the jack and why; `longwire jack` in a terminal prints the full
instructions, including the two commands to run by hand as root.

### When it is on

| Section | What you see |
|---|---|
| **This node** | your **Number**, your **Name**, the **Switchboard** (it says `dial tone` when all is well) and your **NAT** type. **Turn off** stops the node (calls end; it asks first). |
| **Calls** | anything ringing or being set up. A ring shows who, the caller's name as a claim, and the safety phrase. Click it to **pick up** or decline. Each connected person is a row (path, how long, speed in and out); click it to hang up. **Dial…** rings a number. |
| **Line** | the machines you are connected to as one network. Each can be evicted (the line key then changes), unless the line has an operator, who alone decides. |
| **Answering** | **When a call rings**: the policy for this running node, changeable here. |

If the node is a **service** run by an administrator under another user, the pane shows it and says **Runs as: a service**; you
can use it if you are in the group `longwire`, but only the administrator turns it off.

## The telephone on the menu bar

| It shows | It means |
|---|---|
| `☎ ○` | off |
| `☎ ◌` | on, waiting for the switchboard to give a number |
| `☎ ●` | on, and you have a number |
| `☎ ● 2` | on, and two people are connected |
| `☎ ◉ 312-884-076` | that number is **ringing you** |

(In the plain-text glyph set it writes `tel -`, `tel ~`, `tel *`, `tel ! 312-…`. With the Blit glyph set and hibr 0.99.364 it is
Blit's own telephone: outline when off, dashed while connecting, filled when on, with ring waves when ringing.)

**Click it** for a menu that depends on the state:

- *Off:* the profile it would use, and **Turn On**. With no connection set up it says so.
- *On:* **your number**, any ring with **Pick up…**, each connection with its path and speed, **Dial…**, and **Turn Off**.
- Always: **Longwire Settings…**, which opens the pane.

### A ring interrupts

When someone rings you, a note appears and a dialog asks:

> 312-884-076 (Alice, a claim) rings. Safety phrase: gopher daisy castle cactus island flint. Pick up?

**Compare the phrase with the caller on another channel.** The caller sees the same words as soon as the call connects. A match is good
evidence that nobody is in the middle (a short phrase has limits: [SECURITY](SECURITY.md#the-safety-phrase-honestly)); if the words differ once
you are connected, hang up at once. Answer **Yes** to pick up, **No** to leave it. The same dialog appears once per call, however long it rings (a call that nobody answers is refused after a minute).

A ring is only heard while the desktop is running. The node itself keeps running without it (`longwire on` starts it in the
background), but it is the desktop that rings.

## The Control Strip telephone

One glyph on the Control Strip, coloured by the theme: **dim** when off, the theme's *info* colour while waiting for a number, its *good* colour
(usually green) when on, and its *warning* colour (usually amber) when someone is ringing. Click it for the same menu as the menu bar. The bar and the strip are independent: show either, both, or neither
(Control Panel → Menu Bar Items, or the pane's **Longwire on the Menu Bar** switch for the bar).

## The Switchboard window

Only for someone who runs a switchboard. It is a live view of it: how full it is (nodes, handshake load), the nodes with the names
they gave, and the calls being set up right now (who is ringing whom, for a few seconds). It shows no node's address or key (only the switchboard's own address and code in its heading), keeps no
history, and decides nothing; the switchboard's script does that. It shows a switchboard you started yourself, or one an
administrator runs as a service if you are in the group `longwire` (`sudo usermod -aG longwire YOURNAME`, then log in again).

## The answer policy, in one table

| Policy | A ring from… |
|---|---|
| **ask** (default) | anyone: wait for you |
| **known** | a number you have been connected to before is answered; anyone new waits for you |
| **auto** | anyone: answered. Only for a headless machine you have decided to trust |
| **never** | anyone: refused |

A machine **vouched for by a member of your line**, or holding the line operator's admit, is answered under every policy except
`never`; that is how a mesh of sites builds itself without a prompt on each.

## Good habits

- Always compare the **safety phrase** on a first call.
- Keep **When a call rings** on `ask` or `known`.
- Do not put the token in a chat or a screenshot. Once it has been used it does nothing; before that it is a key.
- Back up your identity once: `longwire key export` prints it as 33 words ([OPERATING](OPERATING.md)).

When something looks wrong, [TROUBLESHOOTING](TROUBLESHOOTING.md) lists the symptoms we know.
