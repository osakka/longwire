#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A jack's own network is no way to reach the other end (docs/adr/0037): the engine leaves it out of what it offers and what it tries.
# Built with TCC's bounds checker; the loopback interface stands in for the jack, so nothing needs privilege.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"jacknet"' -Wall -o "$B/jacknet" tests/jacknet.c $VENDOR
D=$(mktemp -d /tmp/lwjn-run.XXXXXX)
trap 'rm -rf "$D"' EXIT
"$B/jacknet" "$D"
