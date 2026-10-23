#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Caller ID and the directory (ADR 0023): names are opt-in at the node and gated at the switchboard.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/names.py
