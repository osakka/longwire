# 0042 — The jack stays unless asked

Status: accepted. Builds on [0034](0034-a-connection-is-a-profile.md) and [0038](0038-the-jack-may-ask-a-dhcp-server.md).

## Context

Turning the node off stops the process. The tap device it used (the jack) is persistent: `ip tuntap add` makes a device that outlives whoever opens it. The first reaction to a jack left behind
after Turn off was that it should have gone with the node. But making it needs the administrator's password (polkit remembers an approval for five minutes), and removing it needs the same: if Turn off
removed it every time, every Turn on would ask for the password again, which is the very thing the jack helper exists to avoid.

## Decision

1. **The default is to keep it.** A profile's `keep_jack` is `yes` unless it says `no`. Turn off leaves the device (with its address, and a DHCP client of ours if one was started), and the next Turn on finds it and asks for nothing.
2. **`keep_jack = no` removes it.** `longwire off` (and the pane's Turn off) waits up to three seconds for the node to let go, then runs `longwire jack --remove TAP` through the same helper and polkit action.
   The pane has a row **Keep the jack when turned off**.
3. **The helper removes only what is the user's.** `longwire-jack --remove TAP` deletes a device called `lw0` to `lw99` only if the kernel says its owner is the user who asked; it stops a DHCP client of ours on it first (by its pid file, and only if the process is `dhclient`,
   `dhcpcd` or `udhcpc`) and removes its hook and lease files. A device that is not there is not an error.

## Consequences

A machine keeps `lw0` after the node is off; it is harmless (down traffic, no node), and `longwire jack --remove` takes it away by hand. Nothing removes a jack the user did not ask to remove, and a reboot removes it anyway.
Tested against a stand-in `ip` (`tests/jack.sh`), the script's off against a stand-in control socket (`tests/events.py`) and the pane (`tests/pane.py`); not tried on a real device.
