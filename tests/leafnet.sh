#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The phone's engine on a real line against a home node and a scripted LAN (ADR 0076). Needs ../hibr/build/hibr (or $HIBR) and python3.
set -e
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -g -Ivendor/mlkem768 -DLW_VERSION='"leafnet"' -Wall -o "$B/leafnet" tests/leafnet.c $VENDOR
T=$(mktemp -d /tmp/lwleafnet.XXXXXX)
cleanup() { [ -S "$T/ctlsb" ] && $HIBR -c "mod load $PWD/$B/longwire.so; lw stop --sock $T/ctlsb" > /dev/null 2>&1; [ -S "$T/ctlhome" ] && $HIBR -c "mod load $PWD/$B/longwire.so; lw stop --sock $T/ctlhome" > /dev/null 2>&1 || true; sleep 0.3; rm -rf "$T"; }
trap cleanup EXIT
HIBR=$HIBR python3 -I tests/leafnet.py "$T"
