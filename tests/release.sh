#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Number release: release are opt-in at the node and gated at the switchboard.
# Needs unprivileged user and network releasepaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/release.py
