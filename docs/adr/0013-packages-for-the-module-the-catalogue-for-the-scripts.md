# 0013 — Packages for the module, hibr's catalogue for the scripts

Status: accepted *(planned: M4)*

## Context

The scripts change often and the C rarely, and hibr already has a place for each kind of thing.

## Decision

The module ships as `longwire` packages for deb, rpm, pacman and Homebrew, from signed repositories like hibr's own, each
depending on `hibr`. The two scripts ship through hibr's catalogue as commands (`catalogue install cli/longwire`; [0029](0029-commands-are-their-own-catalogue-kind.md)), declaring
`# modules: longwire` and the ABI in their headers so the catalogue refuses them by name, and places nothing, when the
module is missing. A site box installs the node into the machine's scope (`sudo catalogue --system install cli/longwire`),
so the splice service runs a script its own user cannot modify. The package also ships two launchers (`/usr/bin/longwire`
and `/usr/bin/longwire-switchboard`, each a few lines that `exec hibr` on the catalogue-installed script) and a systemd
unit template, because a catalogue app is not a command on `PATH` by itself.

## Consequences

None of the packaging exists yet *(planned: M4)*; today the module is built from source with `make`, and the scripts are
run from the repository. Four repositories to publish to is the cost of reaching four package managers, and why the
default switchboard's address lives in a script rather than the module
([0014](0014-a-default-public-switchboard.md)).

---

[← decisions](README.md)
