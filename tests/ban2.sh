#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Strikes and bans on a real switchboard (docs/adr/0043): a wrong token earns a ban, a good node is untouched, noise earns nothing, the ban ends, the operator lifts it.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/ban.py
