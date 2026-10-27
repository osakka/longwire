#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Longwire on the menu bar and the Control Strip (menubar/longwire.hibr, control-strip/longwire.hibr). Needs unprivileged user and
# network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/bar.py

# the same files against hibr's own desktop code instead of stubs (read-only; nothing is drawn): a misnamed function or an
# undeclared variable shows up here and not in the stubs
HIBR=${HIBR:-../hibr/build/hibr}
DD=$(dirname "$HIBR")/../programs/desktop
if [ -d "$DD" ] && [ "$(id -u)" != 0 ]; then
	out=$(HIBR_MODPATH="$PWD/build:$(dirname "$HIBR")/mods" LW_SOCK=/nonexistent/ctl "$HIBR" -c "
. $DD/desktop.hibr
. $DD/system/panel.hibr
. $PWD/control-panel/longwire.hibr
. $PWD/menubar/longwire.hibr
. $PWD/control-strip/longwire.hibr
lwb_state
t := lwb_text
echo \"STATE \$LWB_ST [\$t]\"
dt_has lwb_context && echo has-context
dt_has lwstrip_draw && echo has-draw" 2>&1)
	case $out in
	*"STATE off [☎ ○]"*"has-context"*"has-draw"*) case $out in *"not declared"*) echo "FAIL the bar item under hibr's real desktop code: $out"; exit 1 ;; esac
		echo "ok   the bar item and the strip module load under hibr's real desktop code, and read the line as off" ;;
	*) echo "FAIL the bar item under hibr's real desktop code: $out"; exit 1 ;;
	esac
fi
