#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The switchboard is restarted under connected nodes (a service restart, a reboot of its host): they come back by themselves, as the same numbers, and a call between them works again.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/sbrestart.py
