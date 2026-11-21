#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A desktop that answers by hand (policy ask) joins a founder's line, a second box joins through the founder, then both boxes restart: the desktop is called by both and must let both in
# by itself (they are members of its line), with nobody at the keyboard.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/meshask.py
