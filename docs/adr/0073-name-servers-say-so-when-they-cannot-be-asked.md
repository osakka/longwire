# 0073 — Name servers say so when they cannot be asked, and systemd-resolved is suggested

Status: accepted; built in 0.5.59. Found when the owner entered name servers in a profile and, on their machine, names kept going to the Wi-Fi's own server.

## Context

A profile's `dns` is set on the jack in systemd-resolved ([0047](0047-names-through-the-line-are-set-on-the-jack.md), [0059](0059-name-servers-without-everything.md)). On a machine without systemd-resolved
(no `resolvectl`, `/etc/resolv.conf` a plain file) nothing is set and names go to the machine's own server. The helper printed a line when the jack was made, in the terminal or the pane's window of its
own, and `longwire show` mentioned names only while everything went through the line. A person who had typed servers into the Name servers row saw them there, saw the line work, and had no sign that names did
not use them.

## Decision

1. **`longwire show` says so**, whenever the node holds a jack and the profile has name servers: with systemd-resolved it says names go *through the line, from* the servers the jack holds, or, if the jack has none
   of them, that they are not set and to turn longwire off and on; without it, that the servers are not asked and names go to the machine's own server, and that systemd-resolved is what is missing.
2. **The pane says so on the row below Name servers**, where the person is looking: *not asked through the line: systemd-resolved is not installed*.
3. **The packages suggest systemd-resolved** (deb `Suggests`, rpm `Suggests`); they do not depend on it. Only the name servers of a profile need it, and installing it takes over how the whole machine resolves names
   (the resolver, `/etc/resolv.conf`), which a package must not do as a side effect of installing a VPN. On Arch it is part of `systemd`. Nothing here installs it or changes `/etc/resolv.conf`: the person does,
   with the line the pane and `show` print.

## Tests

`tests/showdns.sh` (a stand-in node and a profile with name servers: no systemd-resolved, systemd-resolved with nothing set, with the servers set, and a profile without any), `tests/pane.sh` (the row appears with
servers and no systemd-resolved, and not with it), `tests/pkg.sh` (the deb and rpm suggest it and do not depend on it).
