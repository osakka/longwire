#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# IPv6 underlay. Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/ipv6.py
