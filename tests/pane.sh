#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The control panel pane (control-panel/longwire.hibr): its rows against a live node, and what its actions run.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/pane.py
# The same files on hibr's real desktop: tests/hosted.sh (installed as the catalogue installs them, each run in a process of its own, hibr ADR 0050).
