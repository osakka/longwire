#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# M0: two nodes handshake, bridge frames and survive rekeys across a hostile
# network; a third, unknown node is refused. Everything is driven from hibr,
# as in real use. Needs ../hibr/build/hibr (or $HIBR) and python3.
set -e
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
MOD=$PWD/build/longwire.so
T=$(mktemp -d /tmp/lwm0.XXXXXX)
cleanup() { for s in A B C; do [ -S $T/ctl$s ] && $HIBR -c "mod load $MOD; lw stop --sock $T/ctl$s" >/dev/null 2>&1 || true; done; sleep 0.3; rm -rf "$T"; }
trap cleanup EXIT

lw() { $HIBR -c "mod load $MOD; lw $*"; }

for n in A B C; do lw key new $T/seed$n >/dev/null; lw key bundle $T/seed$n > $T/bundle$n; done
echo "key ids: A=$(lw key id $T/seedA | cut -c1-12) B=$(lw key id $T/seedB | cut -c1-12) C=$(lw key id $T/seedC | cut -c1-12)"

# A waits; B dials A through the proxy on :41010; C knows A, A doesn't know C
lw start --seed $T/seedA --sock $T/ctlA --listen 127.0.0.1:41001 --sock-port $T/Aport:$T/Ahost \
   --peer $T/bundleB --rekey 4 --keepalive 2 >/dev/null
lw start --seed $T/seedB --sock $T/ctlB --listen 127.0.0.1:41002 --sock-port $T/Bport:$T/Bhost \
   --peer $T/bundleA@127.0.0.1:41010 --rekey 4 --keepalive 2 >/dev/null
lw start --seed $T/seedC --sock $T/ctlC --listen 127.0.0.1:41003 --sock-port $T/Cport:$T/Chost \
   --peer $T/bundleA@127.0.0.1:41001 --rekey 4 --keepalive 2 >/dev/null

# the privilege story: no capabilities, no_new_privs, in the worker
for pid in $(pgrep -x lw-worker); do
  cap=$(awk '/CapEff|CapPrm|CapInh|CapAmb/ {print $2}' /proc/$pid/status | sort -u | tr '\n' ' ')
  nnp=$(awk '/NoNewPrivs/ {print $2}' /proc/$pid/status)
  [ "$cap" = "0000000000000000 " ] && [ "$nnp" = 1 ] && echo "ok   worker $pid: every capability set empty, no_new_privs=1" \
    || { echo "FAIL worker $pid caps=[$cap] nnp=$nnp"; exit 1; }
done

# one node per control socket: a second start must refuse, not replace it
if lw start --seed $T/seedA --sock $T/ctlA --listen 127.0.0.1:41099 --sock-port $T/Xport:$T/Xhost >/dev/null 2>&1; then
  echo "FAIL a second node replaced a running one"; exit 1
else echo "ok   a second start on a live control socket is refused"; fi

python3 -I tests/m0.py "$T"
