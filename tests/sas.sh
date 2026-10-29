#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The committed safety phrase (docs/adr/0040): what an end does with a commit, a reveal, a wrong one, silence and --require-sas. Built with TCC's bounds checker.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"sas"' -Wall -o "$B/sas" tests/sas.c $VENDOR
D=$(mktemp -d /tmp/lwsas.XXXXXX)
trap 'rm -rf "$D"' EXIT
mkdir "$D/a" "$D/b"
"$B/sas" "$D/a" "$D/b"
