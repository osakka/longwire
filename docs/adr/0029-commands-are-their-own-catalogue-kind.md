# 0029 — Commands are their own catalogue kind

Status: accepted for longwire; the change it needs in hibr and in hibr-apps is proposed *(planned: M4)*. Amends
[0013](0013-packages-for-the-module-the-catalogue-for-the-scripts.md) and [0028](0028-one-deb-and-the-others-from-it.md).

## Context

hibr's catalogue knows nine kinds, and eight of them are places the desktop scans. A script placed in `apps` is an
application: it can appear on the Applications menu and is shaped like one. `longwire` and `longwire-switchboard` are not
that. They are commands, run from a prompt or a service, with no window; a text-mode desktop application of ours (the
Lines pane, M4) would be a different thing, and that kind of script belongs in `apps` or `control-panel` as hibr's own do.

## Decision

Scripts that are commands are published under a tenth kind, **`cli`**, and installed with `catalogue install cli/NAME`.

- **They land outside every scan root:** `$XDG_DATA_HOME/hibr/cli/NAME.hibr` for a user, `$HIBR_SYSDIR/cli/NAME.hibr` for the
  machine (`--system`). The desktop never reads those folders, so nothing appears on a menu. A script is still a plain file
  you can read first (`catalogue show cli/NAME`) and the catalogue still checks its `sha256` and the index's signature.
- **They declare themselves as apps do:** an `app NAME "description" VERSION` line and `# modules:`, which mkindex reads
  for the title, version and the module the catalogue should find before it fetches.
- **The launchers look in `cli` first**, then in the places an application would be, so a copy installed either way runs.
- **The change in hibr is two lines**: `cli` in `CAT_KINDS` and in `CAT_KEXT` (`[cli]=.hibr`) in `programs/catalogue.hibr`,
  with the comments that count the kinds. It is in [packaging/catalogue/hibr-cli-kind.patch](../../packaging/catalogue/hibr-cli-kind.patch)
  against hibr as it stands. Tried on a scratch copy against a local catalogue: `catalogue install cli/longwire` placed the
  script at `$XDG_DATA_HOME/hibr/cli/longwire.hibr` and nothing else. A current `catalogue` meeting a `cli` entry lists
  it and refuses to install it with a plain message ("the index says kind cli, which is not one an app installs into"), so
  publishing such an entry harms no one who has not upgraded.
- **The change in hibr-apps** is for its `addapp` (to take a file that is not under a desktop tree, with `--kind cli`) and
  `mkindex` (to list the kind, as "Commands"), `README.md` and its notes. They are that repository's to make.

## Consequences

- Publishing to the catalogue waits for a hibr that has the kind and a hibr-apps that can publish it; the packages say
  `catalogue install cli/longwire cli/longwire-switchboard`, which works from then on.
- Any other command-line tool written in hibr has somewhere to live that is not a menu.
