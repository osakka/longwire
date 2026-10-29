#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Strikes and bans (docs/adr/0043) at the level of the tables, under TCC's bounds checker: what bans, what lets go, the ceiling, what is never struck, and a flood of identities.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"ban"' -Wall -o "$B/ban" tests/ban.c $VENDOR
D=$(mktemp -d /tmp/lwban.XXXXXX)
trap 'rm -rf "$D"' EXIT
"$B/ban" "$D"
