#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The switchboard application (apps/switchboard.hibr): what it reads from a live switchboard, and the calls it follows.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/sw.py

# the same file sourced under hibr's own desktop code, to see that it loads and registers there (nothing is drawn). Outside the
# namespace for the reason tests/pane.sh gives.
HIBR=${HIBR:-../hibr/build/hibr}
DD=$(dirname "$HIBR")/../programs/desktop
if [ -d "$DD" ] && [ "$(id -u)" != 0 ]; then
	out=$(HIBR_MODPATH="$PWD/build:$(dirname "$HIBR")/mods" "$HIBR" -c "
. $DD/desktop.hibr
. $PWD/apps/switchboard.hibr
dt_has switchboard_draw && echo draw-defined
dt_has switchboard_key && echo key-defined" 2>&1)
	case $out in
	*draw-defined*key-defined*) echo "ok   the app loads under hibr's real desktop code" ;;
	*) echo "FAIL the app under hibr's real desktop code: $out"; exit 1 ;;
	esac
else
	echo "skip the real-desktop check (no programs/desktop beside hibr, or running as root)"
fi
