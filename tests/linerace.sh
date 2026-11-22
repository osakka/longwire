#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Two numbers dialled at once end on one line (BUG-13, ADR 0113).
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/linerace.py
