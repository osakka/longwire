#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Two DHCP servers on one line: the switch sees their offers and warns (ARCHITECTURE section 3).
set -e
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
MOD=$PWD/build/longwire.so
T=$(mktemp -d /tmp/lwdh.XXXXXX)
cleanup() { for s in A B; do [ -S $T/ctl$s ] && $HIBR -c "mod load $MOD; lw stop --sock $T/ctl$s" >/dev/null 2>&1 || true; done; sleep 0.3; rm -rf "$T"; }
trap cleanup EXIT
lw() { $HIBR -c "mod load $MOD; lw $*"; }
for n in A B; do lw key new $T/seed$n >/dev/null; lw key bundle $T/seed$n > $T/bundle$n; done
python3 -I tests/dhcp.py "$T" "$HIBR" "$MOD"
