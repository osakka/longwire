#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# M3 stage 2: two sites behind strict NATs reach each other through a third member, then find a direct path.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/m3b.py
