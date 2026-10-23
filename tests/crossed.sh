#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Crossed handshakes: both nodes are told the other's address, so both start a handshake while the other's is still in
# flight (a proxy delays every datagram 0.8 s to make that certain). The link must come up, keep rekeying and not stall.
set -e
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
MOD=$PWD/build/longwire.so
T=$(mktemp -d /tmp/lwcx.XXXXXX)
cleanup() { for s in A B; do [ -S $T/ctl$s ] && $HIBR -c "mod load $MOD; lw stop --sock $T/ctl$s" >/dev/null 2>&1 || true; done; sleep 0.3; rm -rf "$T"; }
trap cleanup EXIT
lw() { $HIBR -c "mod load $MOD; lw $*"; }
for n in A B; do lw key new $T/seed$n >/dev/null; lw key bundle $T/seed$n > $T/bundle$n; done
python3 -I tests/crossed.py "$T" "$HIBR" "$MOD"
