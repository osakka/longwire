#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A phone-style client dials vpn-1 and gets its lease from a DHCP server on vpn-1's LAN, through whichever box of the pair is active (the guard lets the dialled member's site-mate answer), renews it, and renews
# it again after vpn-1 is stopped (ADR 0083); with the newcomer's key id lower and higher. Needs unprivileged user and network namespaces, and ../hibr/build/hibr.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -g -Ivendor/mlkem768 -DLW_VERSION='"leafnet"' -Wall -o "$B/leafnet" tests/leafnet.c $VENDOR
for order in lower higher; do
	unshare -Urn python3 -I tests/m2d.py "$order"
done
