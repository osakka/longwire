#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The sandbox of a node at every limit fits its buffer, and a buffer too small is never written past (ADR 0063). Built with TCC's bounds checker.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -g -Ivendor/mlkem768 -DLW_VERSION='"sandbox"' -Wall -o "$B/sandbox" tests/sandbox.c $VENDOR
D=$(mktemp -d /tmp/lwsandbox.XXXXXX)
trap 'rm -rf "$D"' EXIT
"$B/sandbox" "$D"
