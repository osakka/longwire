#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# NAT-PMP: pmp are opt-in at the node and gated at the switchboard.
# Needs unprivileged user and network pmppaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/pmp.py
