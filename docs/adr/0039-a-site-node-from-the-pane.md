# 0039 — A site node from the pane

Status: accepted. Builds on [0030](0030-three-surfaces-one-engine.md), [0032](0032-the-services-share-their-control-socket-with-a-group.md) and [0034](0034-a-connection-is-a-profile.md).

## Context

A profile with Attach = splice used to stop at a note: "Splice needs a one-time grant. In a terminal: longwire setup splice NIC". Splicing a card needs `CAP_NET_RAW`, which the node does not
hold ([0004](0004-a-node-never-runs-as-root.md)); the answer is a system service that the service manager grants it ([0028](0028-one-deb-and-the-others-from-it.md)), and writing
`/etc/longwire/NIC.conf` and enabling `longwire@NIC` were by hand. For a desktop-first product ([the owner's wish](0034-a-connection-is-a-profile.md)) that is the same gap the jack had.

## Decision

1. **`longwire site NIC [PROFILE]`**, which the pane's Turn on runs for a splice profile: a second root helper, `/usr/lib/longwire/site`, run through polkit (`org.longwire.site`, an administrator's password, remembered
   for a few minutes), writes `/etc/longwire/NIC.conf` and enables and starts `longwire@NIC`; a service that already runs is restarted to take new options. `longwire site --stop NIC` stops and disables it.
2. **The options go in a file, not on a command line.** The script writes one line (`--switchboard`, `--token`, `--name`, `--answer`, `--listed`, taken from the profile) to a file only the user can read, and the helper
   reads it once. The token never appears in the process list. The helper accepts only those five options, each with a value of the shape it must have (no space, quote, backslash, `$`, `%`, backtick or leading `#` in a token; a name of letters, digits and `. _ -`), so that
   a person who may run it can start a site node and nothing else; a name with a space is left out and the script says so. It refuses `lo`, jacks (`lw*`) and cards that do not exist, a file that is not the user's own or is a link, and root as the user.
3. **The pane follows the service.** The unit already gives its control socket to the group `longwire` ([0032](0032-the-services-share-their-control-socket-with-a-group.md)); a user who joined that group has to log in again,
   so the helper also sets a POSIX ACL on the socket for the user who asked. The pane then points its Node row at the service ([0034](0034-a-connection-is-a-profile.md)'s pane, the Node row of 0.5.13), and **Stop the service** runs `longwire site --stop`.

## Consequences

A person who can authenticate to polkit as an administrator can start a site node on any real card with a switchboard of their choosing; that is the action's purpose, and the service it starts is the unit's own
(DynamicUser, only `CAP_NET_RAW`, the system-call filter). Tested without privilege against stand-ins for `systemctl` and `setfacl` and a made-up card (`tests/site.sh`), and the script and the pane against a stand-in helper.
Not tried: a real `systemctl` and a real card, polkit's dialog, the ACL on a real socket, and the pane's password dialog on a real desktop. A machine without ACL tools falls back to telling the user to join the group and log in again.
