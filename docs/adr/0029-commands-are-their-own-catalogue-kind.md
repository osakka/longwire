# 0029 — Commands are their own catalogue kind

Status: accepted, and built in hibr (`c5bf491`) and hibr-apps (`697f789`); in a hibr release *(planned: M4)*. Amends
[0013](0013-packages-for-the-module-the-catalogue-for-the-scripts.md) and [0028](0028-one-deb-and-the-others-from-it.md).

## Context

hibr's catalogue knows nine kinds, and eight of them are places the desktop scans. A script placed in `apps` is an
application: it can appear on the Applications menu and is shaped like one. `longwire` and `longwire-switchboard` are not
that. They are commands, run from a prompt or a service, with no window; a text-mode desktop piece of ours (the control panel pane and the switchboard app, M4, [ADR 0030](0030-three-surfaces-one-engine.md)) would be a different thing, and that kind of script belongs in `apps` or `control-panel` as hibr's own do.

## Decision

Scripts that are commands are published under a tenth kind, **`cli`**, and installed with `catalogue install cli/NAME`.

- **They land outside every scan root:** `$XDG_DATA_HOME/hibr/cli/NAME.hibr` for a user, `$HIBR_SYSDIR/cli/NAME.hibr` for the
  machine (`--system`). The desktop never reads those folders, so nothing appears on a menu. A script is still a plain file
  you can read first (`catalogue show cli/NAME`) and the catalogue still checks its `sha256` and the index's signature.
- **They declare themselves as apps do:** an `app NAME "description" VERSION` line and `# modules:`, which mkindex reads
  for the title, version and the module the catalogue should find before it fetches.
- **The launchers look in `cli` first**, then in the places an application would be, so a copy installed either way runs.
- **The change in hibr is two lines** (`cli` in `CAT_KINDS` and `[cli]=.hibr` in `CAT_KEXT`, in `programs/catalogue.hibr`), with
  the Catalogue window naming the kind "Commands", the catalogue's documentation, and three checks in hibr's catalogue suite. Tried
  first on a scratch copy against a local catalogue. A `catalogue` without it meeting a `cli` entry lists it and refuses to
  install it with a plain message ("the index says kind cli, which is not one an app installs into"), so publishing such an
  entry harms no one who has not upgraded.
- **The change in hibr-apps** is `addapp --kind cli` (a command is not under a desktop tree, so the kind is said, not found),
  `mkindex` listing the kind as "Commands", reading the licence from a script's own `# SPDX-License-Identifier:` line instead of
  calling every entry LGPL, and an `app` line that may name a script with a dash. Tried end to end: both scripts published to a
  scratch catalogue, the committed `catalogue` showed them signed, refused them by name while the `longwire` module was not
  there, and installed them under `cli/` once it was.

## Consequences

- **`HIBR_MIN` rises with it.** The packages depend on hibr 0.99.265, which runs the module and passes the whole suite but
  cannot install a `cli` entry. When a release carries the kind, `HIBR_MIN` becomes that release, because from then on a
  package that tells you to `catalogue install cli/…` is only true of a hibr that can.
- **Trust.** The services run the installed script unattended, with a packet socket. The catalogue treats a `cli` script as it
  treats an application (an unverifiable catalogue is a warning), so a machine's operator should read it first with
  `catalogue show cli/NAME`; whether a command should be refused from an unsigned catalogue, as a module is, is hibr's to decide.
  The services read the script only from the machine's scope (`LONGWIRE_SCRIPTS=/var/lib/hibr/cli`), never from their own
  writable home.
- The bundles are published when a hibr release has the kind (a client without it lists them and cannot install them), and the
  packages are published with them: they say `catalogue install cli/longwire cli/longwire-switchboard`, which works from
  then on *(planned: M4)*.
- Any other command-line tool written in hibr has somewhere to live that is not a menu.
