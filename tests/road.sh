#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The road under the node (ADR 0061): an address change and a lost network are found again at once, retries are quiet, and nothing is sent while there is no network.
# Needs unprivileged user and network namespaces (see docs/BUILD.md). Takes about four minutes: it waits out sessions that die and back-offs that cap.
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/road.py
