#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# `longwire show` says plainly when the profile asks name servers through the line and they cannot be (docs/adr/0073): a stand-in node answers `status`, and resolvectl is
# present, present and empty, or not there at all.
set -e
cd "$(dirname "$0")/.."
python3 -I tests/showdns.py
