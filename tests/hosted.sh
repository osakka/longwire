#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Longwire's pane, menu bar item and strip module on hibr's real desktop, each in a process of its own (tests/hosted.py). Needs hibr's tree beside
# $HIBR (its tests/screen.py and programs/desktop) and the catalogue checkout (~/hibr-apps, or HIBR_APPS); skipped without them.
set -e
cd "$(dirname "$0")/.."
python3 -I tests/hosted.py
