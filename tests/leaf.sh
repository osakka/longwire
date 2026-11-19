#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The phone's adapter (ADR 0076): a MAC, DHCP, ARP and IPv4 wrapped and unwrapped, against a scripted LAN, and four thousand frames of noise. Built with TCC's bounds checker.
set -e
cd "$(dirname "$0")/.."
B=build
mkdir -p "$B"
VENDOR="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h)"
# shellcheck disable=SC2086
tcc -b -g -Ivendor/mlkem768 -DLW_VERSION='"leaf"' -Wall -o "$B/leaf" tests/leaf.c $VENDOR
"$B/leaf"
