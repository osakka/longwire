#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The exit in the engine (ADR 0056): the frames for the exit gateway reach the member that shares its internet, and nobody else.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/exit.py
