# 0060 — The pane shows whose password a permission asks, and never writes it

Status: accepted; built in 0.5.47. Follows [0058](0058-the-device-and-the-whole-machine-are-two-permissions.md).

## Context

Whether connecting asks for a password is a polkit setting, kept by hibr's Administrative Access (`/var/lib/hibr/access`, one line per action). The person had it set to
"your own password" and could not tell: nothing in longwire showed it, and getting to no password meant knowing to look in another pane and pick a group. A switch in
longwire that wrote the same setting would have been a second place to change one thing.

## Decision

1. **Administrative Access stays the one place the setting is changed.** longwire never writes it and ships no rules of its own.
2. **The pane reads it.** For a jack it shows *Password to connect* (`org.longwire.jack`), and *Password for everything* (`org.longwire.everything`) while everything
   through the line or sharing is on: *an administrator's password*, *your own password*, *none (group G)* when this account is in G, *none for group G, which is not
   you* when it is not, *none (just you, U)* for hibr's "No Password, Just Me" line (`ACTION user U -`, hibr 0.99.444) when U is this account, or *not allowed*; no line is the package's default, an administrator's password.
3. **Its row opens Administrative Access** (`dt_panelpane adminaccess`).

## Consequences

The person sees why a prompt appears and where to change it. If hibr changes the file's format, the rows say what the file says wrongly until longwire follows; the
format is hibr-access's documented one (`ACTION admin|self|group|never [GROUP] [keep]`). Tested in `tests/pane.sh`: each case, membership, the link, and that the
second row is there only when it applies.
