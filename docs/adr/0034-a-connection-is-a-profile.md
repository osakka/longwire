# 0034 — A connection is a profile, and the Control Panel turns it on

Status: accepted. Extends [0030](0030-three-surfaces-one-engine.md) (the command, the pane and the application are three
views of one engine) and amends [0004](0004-a-node-never-runs-as-root.md) in one respect: a small root helper may make the
network device a single machine attaches to.

## Context

0.4.0 shipped an engine, a command that runs in the foreground, and a Control Panel pane that could only start a node with
settings kept inside the desktop. In use that was wrong in four ways. Nothing was *saved* as a connection: the switchboard, the
token and the attach mode were typed again each time. The pane had no place for a **token**, so it could not register at a
switchboard that wanted one. `longwire up` stayed attached to a terminal, which looked like a hang. And a single machine had to
make its own tap device by hand (`ip tuntap …` as root), which a person using the desktop must never be asked to do.

## Decision

1. **A profile is a saved connection**: switchboard, token, attach mode, interface, name, whether it is listed, and the answer
   policy. They are in one hand-editable file, `~/.config/longwire/profiles` (`$XDG_CONFIG_HOME`), directory 0700 and file 0600
   because it holds tokens. `longwire profile list | show | set | token | default | remove` read and rewrite it whole (a comment
   in the file is not kept). The first profile is the default. The command, the Control Panel pane and the menu bar item all read
   this one file; none keeps a second copy of a setting.
2. **The token is never on a command line.** `profile token NAME` reads it from standard input, `show` prints only its length,
   and the pane passes it by a pipe from a masked dialog. (A command line is visible to every user in the process list.) A
   token is only needed the first time a machine registers; after that the switchboard knows its key, and it can be removed.
3. **`longwire on [PROFILE]` runs the node in the background** and says the number it got, or why not (the tail of its log). `off` is
   `down`. `up` with no switchboard named uses the default profile; `up --profile NAME` a named one; anything on the command
   line overrides the profile. `on` is idempotent.
4. **The jack is made by a helper, not by hand.** `packaging/longwire-jack` is installed as `/usr/lib/longwire/jack` with a polkit
   action (`org.longwire.jack`) that names exactly that file. It makes a tap device called `lw0` to `lw99`, owned by the user who
   asked (`PKEXEC_UID`), and brings it up; it does nothing else, refuses any other name, and refuses root as the owner.
   `longwire jack [TAP]` runs it through `pkexec`; `on` does so when the jack is missing; the Control Panel runs the same on a
   pseudo-terminal job (`dt_ptyjob`) so that the password prompt appears in the desktop as the Networks item's does. The node
   itself still never runs as root and holds no capability: only this one creation step does.
   `setup jack` still prints the commands for those who would rather type them.
5. **The pane is the front of this.** With no node it shows *Longwire is off*, the profile (switch between them, make one, remove
   one), the switchboard, the token (as "set, 14 characters" or "none"), attach, interface, name, listed, the answer policy, and
   **Turn on**. With a node it shows the node, the calls and the line as before, and **Turn off**. Turn on makes the jack if
   it is missing and then runs `longwire on`.

## Consequences

- A machine is connected once, by filling in a form (or one `profile set`), and `longwire on` or the button does the rest.
- The profiles file is a secret like a key: 0600, and it should not be put in a repository or a backup that others read. It holds
  tokens, which are invitations, not identities; the seed is elsewhere and never enters it.
- A new privileged component exists: the helper. Its attack surface is its arguments (a name matching `lw0`..`lw99`, taken from
  the caller; the owner is polkit's own `PKEXEC_UID`) and polkit's decision (an administrator's password, kept for the session).
  A person who can already run commands as root gains nothing; one who cannot gains a tap device of their own, which is what
  the kernel's `tuntap` permissions would allow them anyway.
- **Not done, and said so** (the menu bar item and the control strip switch were done next: [0035](0035-longwire-on-the-menu-bar.md)):
  choosing the attach mode automatically; assigning an address to the jack (the node carries Ethernet frames; an address comes
  from a DHCP server on the other side, or you set one); the splice mode from the pane (it needs a one-time grant that is not a
  tap, `longwire setup splice`).
- **Not tested:** the pane drawn on a real desktop; `pkexec` with a real polkit agent behind the desktop's pty job; the jack on a
  real tap device (this machine has no `/dev/net/tun`). `tests/profile.sh` runs the commands against a real switchboard that wants
  a token; `tests/pane.sh` drives the pane's rows and actions against stubs and against hibr's own desktop code; `tests/pkg.sh`
  checks the helper and the policy are in all three packages and agree on the path.
