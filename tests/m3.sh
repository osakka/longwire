#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# M3: a line of four sites, joined by number through the switchboard, as one subnet; eviction; a member restarting.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/m3.py
