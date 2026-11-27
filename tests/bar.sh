#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Longwire on the menu bar and the Control Strip (menubar/longwire.hibr, control-strip/longwire.hibr). Needs unprivileged user and
# network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/bar.py
# The same files on hibr's real desktop: tests/hosted.sh (installed as the catalogue installs them, each run in a process of its own, hibr ADR 0050).
