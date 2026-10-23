#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The registry of switchboard codes (ADR 0024): a switchboard gets its short code the way a node gets a number.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/registry.py
