#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Saved connections (ADR 0034): profiles, `longwire on` and `off`, and the default profile, against a real switchboard that
# wants a token. Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/profile.py
