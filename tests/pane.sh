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
	# and the rate code, with hibr's own dt_ms and two readings of a node's counters (the first test used a stub clock)
	out=$(HIBR_MODPATH="$PWD/build:$(dirname "$HIBR")/mods" LW_SOCK=/nonexistent/ctl "$HIBR" -c "
. $DD/desktop.hibr
. $DD/system/panel.hibr
. $PWD/control-panel/longwire.hibr
json parse LWP_S '{\"peers\":[{\"number\":\"111-111-111\",\"state\":\"up\",\"path\":\"direct\",\"age\":1,\"rx_bytes\":1000,\"tx_bytes\":1000}]}'
lwp_rates
sleep 0.3
json parse LWP_S '{\"peers\":[{\"number\":\"111-111-111\",\"state\":\"up\",\"path\":\"direct\",\"age\":2,\"rx_bytes\":2000000,\"tx_bytes\":1000}]}'
lwp_rates
LWP_UP=1; LWP_T=\$SECONDS
n := longwire_rows 1
for ((i = 0; i < n; i++)); do echo \"\${CP[1][\$i][kind]}|\${CP[1][\$i][text]}|\${CP[1][\$i][val]}\"; done" 2>&1)
	case $out in
	*"action|  rx "*"Mbit/s   tx 0 bit/s|"*" MB in, 1.0 kB out"*) echo "ok   the rates are made with hibr's own dt_ms, and the connection's row shows them" ;;
	*) echo "FAIL the rates under hibr's real desktop code: $out"; exit 1 ;;
	esac
else
	echo "skip the real-desktop check (no programs/desktop beside hibr, or running as root)"
fi
