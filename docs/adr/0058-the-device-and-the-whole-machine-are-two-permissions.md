# 0058 — The device and the whole machine are two permissions

Status: accepted; built in 0.5.45. Amends [0041](0041-the-helpers-and-the-launcher-are-hibr-scripts.md) (the jack helper) and [0056](0056-a-member-can-share-its-internet.md) (sharing).

## Context

Everything the jack needs root for went through one polkit action, `org.longwire.jack`: making or removing a person's own network device with its address and
routes, and also sending the whole machine's traffic through the line and sharing its internet. The owner wants to connect and disconnect, routes or not and with
the jack removed at disconnect (`keep_jack = no`), without typing a password each time; polkit does that when the action is set to need none. But with one
action, "no password for my device" also meant "anyone at this screen may reroute the whole machine, or open it as a way out", without being asked.

## Decision

1. **Two actions, one helper.** The helper is installed twice: `/usr/lib/longwire/jack` (`org.longwire.jack`, *Set up the network device longwire uses on this
   computer (its address and routes)*) and `/usr/lib/longwire/jack-everything` (`org.longwire.everything`, *Send all of this computer's traffic through
   longwire, or share its internet*). polkit decides by the path `pkexec` runs, so the name the helper was started as is what the person allowed.
2. **Run as `jack`, it refuses `--everything` and `--share`** before anything changes. It still makes, changes and removes the person's own device with its
   address, DHCP and routes, and still takes everything through the line or sharing away again: turning off never needs the wider permission.
3. **The script picks the helper** by what the profile asks: `jack-everything` only for everything through the line or sharing, `jack` for the rest. The
   pane's password dialog says which ("To send this computer's traffic via longwire").
4. Both default to an administrator's password, remembered for a few minutes, as before. Control Panel > Administrative Access sets each one.

## Consequences

Setting *Set up the network device* to no password gives connect and disconnect with no prompt, routes included, and nothing more; everything through the line
and sharing still ask unless that action is allowed too. A package update is needed for the new helper path; until then, everything through the line says the
helper is not installed. Tested: run as `jack`, everything and sharing are refused and routes and turning off are not; run as `jack-everything`, everything is
set; the script asks the right helper for each (`tests/jack.sh`); all three packages carry both helpers and both actions (`tests/pkg.sh`).
