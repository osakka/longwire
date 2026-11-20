#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A node whose keep-alive is long (a phone with its battery saver: 55 s, or 120 s) stays online at the switchboard, which calls a node online if it was heard in the last 80 s (ADR 0099).
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/presence.py
