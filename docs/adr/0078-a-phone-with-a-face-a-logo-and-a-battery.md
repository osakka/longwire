# 0078 — A phone with a face, a logo and a battery

Status: accepted; built in 0.5.63. The owner: a logo from the retro glyph, a better look for the Android screen, and a battery mode, since the phone will be connected all the time; and a way to take the address from DHCP in the desktop pane
instead of typing it.

## Decision

1. **The logo is the classic rotary telephone** (the glyph the menu bar draws under Blit's glyphs: the handset over a trapezoid body with a dial) in copper and insulator on line black, the wire hanging below it and
   the signal on it (`brand/app-icon.svg`). In the app it is an adaptive launcher icon and a one-colour notification icon, vector drawables, built into the APK by the resource compiler.
2. **The screen is rebuilt** on the brand's palette: a status card whose colour says the state (green connected, amber connecting, red needs attention, grey off), the detail under it and the safety phrase; one large
   button; a Home card that says the switchboard and the number and keeps the fields behind Edit; the two switches; and, until it is allowed, a card asking to let it run in the background. It follows the system's light or dark
   mode. It is monospace, as numbers are.
3. **The battery**: an engine inside an app sleeps a second at a time when no call is being made and every session is up (a call or a handshake wakes it as before, a packet wakes it at once; only the timers are coarser);
   the app's own loop looks every second with the screen on and every five with it off; a phone renews its keys every five minutes, not two; **Battery saver** sends the keep-alive every 55 s, not 25; and the app asks
   once for the system's exemption from battery optimisation (Android pauses background apps and a paused app is a dropped line). The first three change nothing a person sees; the fourth is the person's choice and is said plainly:
   it has not been tried on a mobile network, and the home node's own keep-alive keeps the phone's radio waking unless it is slowed too. The leaf role *(planned: M6)* is where the rest of the saving is.
4. **The desktop pane's Address is a choice**, **dhcp** or **fixed**, with the fixed address on a row of its own. `dhcp` has been accepted by the profile since 0.5.20; it was a word to know and is now a click.
5. **The switchboard says its own details**: `longwire-switchboard show` prints its address, its port (the status now carries it), its version and the commands that name it to a node and invite a phone; and `token invite`
   without `--host` takes this machine's name and that port (and says so, since the name may not be where a phone reaches it).

## Tests

`tests/tokens.sh` (show, and invite without a host, against a running switchboard); `tests/pane.sh` (the Address row is a choice, fixed shows its address, choosing dhcp sets it); `tests/leafnet.sh` still passes with the
engine sleeping longer when idle (its DHCP, calls, restarts and kills all run embedded). Not tested: any of the look, the icon on a launcher, the battery's saving, the exemption dialog. They are for the phone.
