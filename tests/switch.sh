#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# What a member can and cannot do to the switch (ADR 0063): site frames from the line, and stealing a local host's address. Built with TCC's bounds checker.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"switch"' -Wall -o "$B/switch" tests/switch.c $VENDOR
D=$(mktemp -d /tmp/lwswitch.XXXXXX)
trap 'rm -rf "$D"' EXIT

"$B/switch" "$D"
