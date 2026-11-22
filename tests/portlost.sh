#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A splice box whose LAN card disappears ends, saying why, instead of spinning (ADR 0112).
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/portlost.py
