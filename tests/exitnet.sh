#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The exit on the real kernel (ADR 0056): the client's route to the exit gateway, and the exit's forwarding and NAT (needs nft; skipped without it).
# Needs unprivileged user and network namespaces (see docs/BUILD.md).
set -e
cd "$(dirname "$0")/.."
unshare -Urn python3 -I tests/exitnet.py
