#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# What the desktop pane and the switchboard app read (ADR 0030): names in status, the registry as pages, call progress events.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/panel.py
