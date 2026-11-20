#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The guard on a real line (ADR 0080): a node with the guard on, a member it dialled, and a rogue that rang it. Both send a DHCP reply; only the one it dialled reaches the LAN. Needs ../hibr/build/hibr (or $HIBR) and python3.
set -e
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
MOD=$PWD/build/longwire.so
T=$(mktemp -d /tmp/lwguardnet.XXXXXX)
cleanup() { for s in A B C; do [ -S $T/ctl$s ] && $HIBR -c "mod load $MOD; lw stop --sock $T/ctl$s" > /dev/null 2>&1 || true; done; sleep 0.3; rm -rf "$T"; }
trap cleanup EXIT
lw() { $HIBR -c "mod load $MOD; lw $*"; }
for n in A B C; do lw key new $T/seed$n > /dev/null; $HIBR -c "mod load $MOD; lw key bundle $T/seed$n" > $T/bundle$n; done
# A dials B (a member it asked for); C dials A (a rogue that rang it). A guards: on.
lw start --seed $T/seedB --sock $T/ctlB --listen 127.0.0.1:41811 --sock-port $T/Bport:$T/Bhost --peer $T/bundleA --rekey 4 --keepalive 2 > /dev/null
lw start --seed $T/seedA --sock $T/ctlA --listen 127.0.0.1:41810 --sock-port $T/Aport:$T/Ahost --peer $T/bundleB@127.0.0.1:41811 --peer $T/bundleC --dhcp-guard on --rekey 4 --keepalive 2 > /dev/null
lw start --seed $T/seedC --sock $T/ctlC --listen 127.0.0.1:41812 --sock-port $T/Cport:$T/Chost --peer $T/bundleA@127.0.0.1:41810 --rekey 4 --keepalive 2 > /dev/null
python3 -I tests/guardnet.py "$T"
