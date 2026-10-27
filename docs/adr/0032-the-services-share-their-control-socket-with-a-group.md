# 0032 — The services share their control socket with the group longwire

Status: accepted. Amends [0004](0004-a-node-never-runs-as-root.md) and resolves the open question in [0030](0030-three-surfaces-one-engine.md). The restart policy below is amended by [0033](0033-a-switchboard-is-built-for-strangers.md): the units say `Restart=always`.

## Context

The control socket is mode 0600 and the worker checks the caller's uid (`SO_PEERCRED`). That is right for a node you start
yourself, and wrong for the two services: `longwire@NIC.service` and `longwire-switchboard.service` run as transient users
(`DynamicUser=yes`), so the person at the desktop is a different user and cannot reach the socket. The control panel pane could
not drive a service node, and the switchboard application could not watch a service switchboard. Two more facts made the obvious
fix (loosen the mode) insufficient: the socket lived inside the service's state directory, which is mode 0700, and
`SO_PEERCRED` reports only a caller's *primary* group, so a person whose primary group is their own and who is in `longwire`
as a supplementary group cannot be recognised by comparing a gid in C.

## Decision

1. **A group, `longwire`, opt-in per socket.** `lw start --group NAME|GID` (and `longwire up --group …`,
   `longwire-switchboard --group …`) gives the control socket to that group at mode 0660. Without the option nothing changes:
   0600, owner only, uid checked, as before. An unknown group, or a group the process cannot give a socket to, stops the start
   with a plain message; it never falls back to a more open mode, or to a less open one that looks like it worked.
2. **The file mode is the check.** On Linux `connect()` to a socket needs write permission on the socket file, which the kernel
   decides with the caller's full group list. With a group named, the worker therefore does not compare uids: whoever the kernel
   lets connect is let in. This is stated here because it is a change in *where* the check is, not only in who passes it. Root
   passes as it passes any file mode. We did not add a second check in C with `getgrouplist()`: it would need name-service
   lookups inside a worker that closes everything it inherits, to repeat what the kernel has already decided.
3. **The services use it.** Both units run in the group (`Group=longwire`, so the dynamic user's primary group is `longwire`),
   get a runtime directory their group can enter (`RuntimeDirectory=…`, mode 0750), and put their socket there:
   `/run/longwire/INSTANCE/ctl` for a node, `/run/longwire-switchboard/ctl` for the switchboard. The state directory stays 0700:
   the seed never leaves it. (With `DynamicUser=yes`, systemd moves the *state*, cache and log directories below a root-only
   `private/` folder; its manual lists those three and not `RuntimeDirectory=`, whose innermost directory is owned by the
   configured `Group=`. That is what this relies on, and it is from the manual, not from a run.)
4. **The package makes the group** (a `sysusers.d` file, and a `groupadd` fallback in each package's install step), so it
   exists before the units start. A person joins it with `usermod -aG longwire NAME` and logs in again. The package adds no one.
5. **The desktop uses it.** The pane, when it finds no node of the person's own, looks for `/run/longwire/*/ctl` and drives the
   first node that answers; the switchboard application, when it finds no switchboard of its own, uses
   `/run/longwire-switchboard/ctl`. Both say what to do when the service is there and the socket is not reachable.

## What a member of the group can do

Everything the control socket does, which is what the owner can do, and nothing else: see the node's status and events;
for a node, dial, pick up, reject, hang up, unpin, evict and carry an operator's eviction (`revoke`). (`stop`, and for a switchboard `reply`, the script's decisions, are the owner's alone: [0033](0033-a-switchboard-is-built-for-strangers.md).)
For a switchboard, read its status, its registry page (numbers, names, ages: no addresses or keys) and the call events, which
hold numbers only. **No verb moves key material**: the seed is read and wiped inside the module and never crosses the socket
([ARCHITECTURE](../ARCHITECTURE.md)), and the key export commands write a file the person names, as the person. But a member can
disconnect a site, evict a member of a line, or end a call. Put on the group only people who may do that to the machine's
service, in the way that putting someone in `docker` or `dialout` is a statement about the machine.

## Consequences

- **Amends 0004:** "a node never runs as root" and the one-time privilege are unchanged; the control socket's *owner-only*
  rule becomes "owner-only unless a group is named", and the services name one. The threat model says so.
- **A member cannot stop a service** ([0033](0033-a-switchboard-is-built-for-strangers.md) amends this: the first version allowed it, and a worker stopped
  over its socket makes `longwire up` exit with status 0, so systemd with `Restart=on-failure` would have left it down). Now `stop` is refused to
  anyone but the user the worker runs as, and the units say `Restart=always` besides, so the owner's own stop is a bounce and `systemctl stop` still stops.
- **What stays with the service.** The pane does not offer to stop a service node (that is `systemctl`) and does not offer the answer policy, which is a file in the
  service's own state and set by `/etc/longwire/NAME.conf`. A ringing call on a service node can be answered or declined from the pane.
- **Several service nodes:** the pane drives the first one that answers; choosing among them *(planned: M4)*.
- **A person's own node wins:** if they have started one, the pane shows that and not the service.
- **Tested:** the gate with three real users in a user namespace made with `newuidmap` (`tests/group.sh`): the owner, a user
  whose *supplementary* group is the socket's, and a user in neither, against a socket with and without `--group`; the unknown and
  impossible group at the start; the pane and the application reading a socket under a `/run` of their own and sending verbs
  to it; the units and the group in all three packages. **Not tested:** the units under a running systemd (`DynamicUser=yes` with
  a static `Group=` is from systemd's documentation, not run here, because this machine's container has no running systemd);
  the group being made by an install on a clean machine of each kind; a desktop session after `usermod`.
