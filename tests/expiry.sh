#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# An admit has an end, and a roll voids what came before it (ADR 0065), with real nodes on loopback. Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -Ivendor/mlkem768 -DLW_VERSION='"expiry"' -Wall -o build/mkadmit tests/mkadmit.c $VENDOR
unshare -Urn python3 -I tests/expiry.py
