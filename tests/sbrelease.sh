#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The operator releases a number (ADR 0106): refused while it is online, done once its node is gone, and still released after a restart of the switchboard.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/sbrelease.py
