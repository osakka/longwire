#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Address claims (ADR 0090): an address a host of the node's own LAN holds may not be claimed by a member, through the tricks that get past a header-reading switch. Built with TCC's bounds checker.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"claim"' -Wall -o "$B/claim" tests/claim.c $VENDOR
D=$(mktemp -d /tmp/lwclaim.XXXXXX)
trap 'rm -rf "$D"' EXIT
"$B/claim" "$D"
