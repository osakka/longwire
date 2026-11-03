#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The policy script spends a token only once its node has a number; a late verdict earns no strike (CHANGELOG 0.5.41).
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/policy.py
