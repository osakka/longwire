#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The control panel pane (control-panel/longwire.hibr): its rows against a live node, and what its actions run.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/pane.py

# the same file against hibr's own desktop and Control Panel code instead of stubs (read-only; nothing is drawn). Outside the
# namespace, because hibr finds its display module through HIBR_MODPATH only for an ordinary user.
HIBR=${HIBR:-../hibr/build/hibr}
DD=$(dirname "$HIBR")/../programs/desktop
if [ -d "$DD" ] && [ "$(id -u)" != 0 ]; then
	out=$(HIBR_MODPATH="$PWD/build:$(dirname "$HIBR")/mods" LW_SOCK=/nonexistent/ctl "$HIBR" -c "
. $DD/desktop.hibr
. $DD/system/panel.hibr
. $PWD/control-panel/longwire.hibr
n := longwire_rows 1
for ((i = 0; i < n; i++)); do echo \"\${CP[1][\$i][kind]}|\${CP[1][\$i][text]}\"; done" 2>&1)
	case $out in
	*"head|No node is running"*"action|Start"*) case $out in *"not declared"*) echo "FAIL the pane under hibr's real desktop code: $out"; exit 1 ;; esac
		echo "ok   the pane loads and builds its rows under hibr's real desktop and panel code" ;;
	*) echo "FAIL the pane under hibr's real desktop code: $out"; exit 1 ;;
	esac
else
	echo "skip the real-desktop check (no programs/desktop beside hibr, or running as root)"
fi
