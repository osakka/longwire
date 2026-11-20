#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The guard (ADR 0080): what the line may say to a node's own side, through the tricks that get past a header-reading switch. Built with TCC's bounds checker.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"guard"' -Wall -o "$B/guard" tests/guard.c $VENDOR
D=$(mktemp -d /tmp/lwguard.XXXXXX)
trap 'rm -rf "$D"' EXIT
"$B/guard" "$D"
