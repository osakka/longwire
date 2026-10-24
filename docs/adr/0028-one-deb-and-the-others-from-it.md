# 0028 — One deb, the others made from it, and the launchers find what the catalogue installed

Status: accepted. Fills in [0013](0013-packages-for-the-module-the-catalogue-for-the-scripts.md).

## Context

[0013](0013-packages-for-the-module-the-catalogue-for-the-scripts.md) settled that the module ships as a package and the two
scripts through hibr's catalogue. Building that needs answers to smaller questions: how the three packagings stay the same,
what the package depends on, how a command typed at a prompt reaches a script that is not in the package, and how the two
services run without a login.

## Decision

- **The deb is built first**, from a `make install` into a staging directory (the same install a source build uses), with
  TCC like everything else. **The rpm and the pacman package are made from the deb's own files**, byte for byte, so the
  packagings cannot differ in what they ship. This is how hibr's own packages are made, and the layout follows them.
- **The package carries the module, two launchers, two services and the documentation. It carries no script.** A script
  in two places is two versions to keep in step; the catalogue is the one route and can be updated without a release.
- **The package depends on the hibr it was built against** (`hibr >= VERSION`, read from `hibr.h`) and on libc at the
  newest symbol version the module uses, read from the module. A module is built for one hibr module ABI, and hibr refuses
  one it cannot satisfy, so the dependency is what makes an upgrade of hibr and a stale module visible to the package manager.
- **The launchers** `/usr/bin/longwire` and `/usr/bin/longwire-switchboard` are one small shell file, twice. It looks for
  `NAME.hibr` the way the catalogue places a script: the user's folder, the user's data folder, the machine's
  (`$HIBR_SYSDIR/apps`), each with the one level of submenu folder the catalogue may use, and runs it with hibr. With none it
  says `catalogue install NAME` and exits 127. `LONGWIRE_SCRIPTS` names a folder to look in first, for a checkout or a test.
- **Two services**, both off until configured: `longwire-switchboard.service` (it needs `/etc/longwire/switchboard.conf`) and
  `longwire@NIC.service` for a spliced site (it needs `/etc/longwire/NIC.conf`). Each runs as a transient user
  (`DynamicUser`) with its own state directory and almost nothing else; the node holds `CAP_NET_RAW` only, which `lw start`
  drops the moment the socket is open, and loads the module by absolute path. They run the launchers, so the scripts come
  from `sudo catalogue --system install`.
- **The scripts declare what the catalogue needs** in their own headers: an `app` line with the name, a description and the
  project's version, and `# modules: longwire`. A test checks that the version in them is the package's.
- **There is no Homebrew formula yet** *(planned: M5)*. The module cannot be built for macOS before then (the splice port is
  `AF_PACKET`), and a formula that cannot build is worse than none.

## Consequences

- One version number, in `VERSION`, is the package's, the module's and the scripts'.
- A fresh install of the package alone does nothing until `catalogue install` brings the scripts; the package says so when it
  installs, and the launcher says so when asked.
- Publishing to signed repositories is a separate step, with a key that is not in this repository.
