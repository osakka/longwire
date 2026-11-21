#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A member away while another is evicted takes the eviction when it links again (ADR 0110).
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/evictsync.py
