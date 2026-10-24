#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A line with an operator (ADR 0026). Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/operator.py
