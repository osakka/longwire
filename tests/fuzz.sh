#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# What a stranger can send, with TCC's own bounds checker watching (docs/adr/0033). tests/fuzz.c includes the engine, makes a
# switchboard and a node in one process, and feeds their receive paths: raw datagrams, a real handshake bent byte by byte, and
# a stranger's encrypted session carrying control messages, split frames and Ethernet frames. It also holds the regressions for
# what the audit found: strangers filling a switchboard's places, a token writing into the script's event, a call connected
# before anyone answered. Fixed seeds, so a failure repeats:  build/fuzz SEED STAGE ITERATIONS
# Needs nothing but the sockets of the loopback interface.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# -b: every pointer access is checked against the object it points into. No other compiler is used (docs/adr/0021).
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"fuzz"' -Wall -o "$B/fuzz" tests/fuzz.c $VENDOR
TMPDIR=$(mktemp -d "${TMPDIR:-/tmp}/lwfuzz-run.XXXXXX")
export TMPDIR
trap 'rm -rf "$TMPDIR"' EXIT
n=${FUZZ_ITERATIONS:-12000}
for seed in ${FUZZ_SEEDS:-1 2}; do
	"$B/fuzz" "$seed" all "$n" 2>&1 | tail -n 12
done
# the regressions on their own, so a failure names itself
for st in fill inject gopickup; do
	"$B/fuzz" 1 "$st" 1 2>&1 | grep "^fuzz: $st"
done
echo "fuzz: all ok"
