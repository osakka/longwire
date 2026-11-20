#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Two splice boxes on one LAN segment that BOTH forward (no site secret: the election cannot work), and what a bridge never carries (ADR 0083). Needs unprivileged user and network namespaces.
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/m2b.py
