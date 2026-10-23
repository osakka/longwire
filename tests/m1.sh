#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# M1: switchboard + nodes as hibr scripts, across a userspace NAT built from
# unprivileged network namespaces. Two runs: both NATs cone (the call must
# connect) and one NAT symmetric (the call must fail cleanly).
set -e
cd "$(dirname "$0")/.."
for mode in cone sym; do
  unshare -Urn python3 -I tests/m1.py $mode
done
