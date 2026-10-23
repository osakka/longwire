#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# M2: two LANs as one subnet, carried by splice nodes, and a site group failover.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/m2.py
