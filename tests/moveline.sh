#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A pair of boxes moved to a new host, with new keys and a new line (ADR 0105): a desktop on the old line, whose members are all gone, dials a new box and moves to the new line by
# itself; a desktop that still has a member of its line up stays where it is.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/moveline.py
