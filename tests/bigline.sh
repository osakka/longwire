#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A line of 14 members (ADR 0067): the line message that tells a newcomer who is on it is more than one session message holds once the line has 12 members, and is sent in two parts.
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/bigline.py
