#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Two nodes that name each other as peers, with addresses, start at the same moment, so each begins a handshake of its
# own. One must stand (the lower key id's): the link has to work every time (docs/PROTOCOL.md section 3).
set -e
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
MOD=$PWD/build/longwire.so
T=$(mktemp -d /tmp/lwst.XXXXXX)
cleanup() { for s in A B; do [ -S $T/ctl$s ] && $HIBR -c "mod load $MOD; lw stop --sock $T/ctl$s" >/dev/null 2>&1 || true; done; sleep 0.3; rm -rf "$T"; }
trap cleanup EXIT
python3 -I tests/start.py "$T" "$HIBR" "$MOD"
