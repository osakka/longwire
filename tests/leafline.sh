#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A leaf (LW-3): in a line of four, a node started with --leaf holds a session with the number it dialled and with no other member, though the others call it.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/leafline.py
