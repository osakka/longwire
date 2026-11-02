# 0050 — A switchboard's tokens are made in its window

Status: accepted; built in 0.5.32. Follows [0049](0049-the-switchboard-window-shows-traffic-and-names-every-key.md) and [0033](0033-a-switchboard-is-built-for-strangers.md).

## Context

A switchboard that matters hands out tokens: one invitation per machine. Making, seeing and removing them was a command (`longwire-switchboard token`), and for the service a command run as
root, after which the file belonged to root and had to be given back to the service by hand, or the service could no longer spend its tokens. The operator who watches the switchboard in its
window had to leave it for a terminal to invite a machine.

Tokens are not behind the switchboard's control socket: the script keeps them in a file, and the service's file is its own user's, readable by nobody else, on purpose (a token is a key to the door).

## Decision

1. **The Switchboard window has a Tokens view** (`t`, View > Tokens; a Tokens menu): the unspent tokens, **New Token** (`n`, which shows the new one and says it in a note), **Remove Token…**
   (`x`, which asks), **Copy Token** (`c`), **Read Again** (`r`).
2. **It works on the file the switchboard uses**: the one named in File > Tokens File… (`T`); for the service, the `--tokens` in `/etc/longwire/switchboard.conf`, or its state folder's; else the one
   beside a switchboard run by hand. It runs the same `longwire-switchboard token` command, so there is one way tokens are written.
3. **A service's file goes through `please`**, with the administrator's password, and is read only when asked (`r`), so opening the view never asks for a password by surprise. A file that is the
   person's own is read at once.
4. **The token command keeps the file the folder's**: run as root, a rewrite leaves the file owned by the owner of the folder it is in, so the service's file stays the service's.

## Consequences

Inviting a machine is a key press in the window. Tested against the real token command and a real switchboard, with the desktop's calls recorded (`tests/sw.sh`): the view, a token made, shown,
copied and removed, a service's file going through `please` only when asked, and the menus. Not tried: `please` behind the desktop's pseudo-terminal with a real polkit agent.
