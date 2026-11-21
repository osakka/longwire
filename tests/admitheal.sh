#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A member whose own admit went wrong (ADR 0107): the members that have it on record let it in, and the first one it reaches makes it a new admit.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/admitheal.py
