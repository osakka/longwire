#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The switchboard is restarted under a splice pair that sends its site claim (BUG-9, LW-21): both boxes register again, as the same numbers, and stay one active and one standby.
# SBROOT=DIR runs the switchboard from another tree (an older release); DOWN=SECONDS is how long it is gone.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/pairauto.py
