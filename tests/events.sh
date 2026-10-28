#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# An event torn in two by the one-second deadline of `read -t` is joined, not thrown away: `longwire watch` against a stand-in control socket that
# sends the first half of an event, waits longer than the deadline, and then sends the rest. Needs no namespaces and no privilege.
set -e
cd "$(dirname "$0")/.."
python3 -I tests/events.py
