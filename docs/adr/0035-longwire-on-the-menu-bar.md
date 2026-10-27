# 0035 — Longwire on the menu bar and the Control Strip

Status: accepted. Builds on [0030](0030-three-surfaces-one-engine.md) and [0034](0034-a-connection-is-a-profile.md); resolves the
"menu-bar ☎ and a ring that interrupts" item of the roadmap, which waited for a hook in the desktop that has since arrived.

## Context

With a connection saved and a Turn on button in the Control Panel ([0034](0034-a-connection-is-a-profile.md)), the one thing still
missing for a phone line was to see it without opening anything: is it on, what is my number, is someone calling. A ring is the whole
point of a telephone, and the pane only notices it while it is open. When the roadmap first listed this the desktop had no
background hook for it; hibr's desktop now has a `menubar` kind (an item on the bar with a ticker, a menu and a switch, as Sound,
Network, Battery and Power use) and a `control-strip` kind.

## Decision

1. **A bar item, `menubar/longwire`.** A telephone and a mark for the state: `☎ ○` off, `☎ ◌` on and waiting for the switchboard,
   `☎ ●` on (with a count, `☎ ● 2`, of the people connected), `☎ ◉ 447-120-933` while that number is ringing. In the ASCII
   glyph set it writes `tel -`, `tel ~`, `tel *`, `tel ! 447-…`; the Nerd set draws its own telephone.
2. **A ring interrupts.** The first frame that sees a new ringing call, by its id, raises a note ("447-120-933 is calling") and the
   same dialog the pane uses (the number, the caller's name as a claim, the six-word safety phrase, *Pick up?*). It is once per call,
   however many frames pass. The node itself is a background process (`longwire on`) and does not need the desktop, but a ring is
   only heard where something watches, and that is the bar while the desktop is up.
3. **The menu follows the state.** Off: the profile it would turn on, and **Turn On** (or "no connection set up yet" if there is
   none). On: the number, any ring to **Pick up**, each connection with its path and its rates, **Dial…**, **Turn Off**. **Longwire
   Settings…** opens the pane, always.
4. **A Control Strip module, `control-strip/longwire`** (`lwstrip`): the telephone alone, dim when off, blue while waiting, green
   when on, amber while ringing; a click opens the same menu. The strip and the bar can each show it, or both, or neither.
5. **One switch**, *Longwire on the Menu Bar* in the pane, which Menu Bar Items also shows and turns. Off, the item is empty.
6. **No second reader of the node.** The bar item and the strip read the node through the pane's own functions (its poll, its rates,
   its profile, Turn on, the ring dialog), so a number or a rate is worked out in one place. They are members of one catalogue
   *bundle*, `bundles/longwire.json` (the catalogue's `longwire` in the `control-panel`, `menubar` and `control-strip` kinds), which is
   how hibr groups Sound, Network, Bluetooth and Power, and they are absent on a desktop without the pane.
7. **Telephones that carry the state, where the glyph set has them.** Blit's icon font gets four colour icons (outline, dashed outline,
   filled, filled with ring waves: U+10001B to U+10001E, from Blit 0.2.44), named `GL[phoneoff]`, `GL[phonewait]`, `GL[phoneon]` and
   `GL[phonering]` in hibr's Blit glyph set once that release is on the machine. The bar item and the strip read those names: when a name
   is set, the picture is that one icon (with the count, or the caller's number while ringing); the ring falls back to the "on" icon;
   when the set has none (Unicode, ASCII, Nerd, and Blit until then) the item builds the state from a telephone and a mark, as above.
   No code waits on the icons.
8. **It wakes the desktop every 1.5 s while the line is on**, so a ring is heard within about that; while the line is off it waits for
   the bar's own minute clock and the moment the person turns it on from the menu or the pane. Idle cost is the desktop waking once
   a second and a half to read a local socket.

## Consequences

- A ring that arrives while the desktop is not running is not heard by anything but the node's log (`~/.longwire/log`). That is the
  honest limit of a desktop item; a ring that wakes a locked machine would be a different component.
- A service node under another user (the `longwire@NIC` unit) shows on the bar when the person is in the group `longwire`, as in
  the pane, and **Turn Off** is then the owner's (`systemctl`), as the pane does.
- **Tested:** the item against real nodes with the desktop's functions stubbed (`tests/bar.sh`): off, off with a profile, on, a ring
  announced once with the phrase, picked up from the bar's dialog and connected, the connection count, the menu in each state, the
  strip's colours and click, the ASCII set, the switch; and the two files load under hibr's own desktop code and read the line as off.
- **Not tested:** how it is drawn (the colours the strip's pen actually gives, the glyph widths on a real terminal, the amber of a
  ring), the ring dialog stealing focus on a real desktop, and the wake-up rate on a laptop battery.
