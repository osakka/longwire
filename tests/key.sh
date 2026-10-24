#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The identity as words and back.
set -e
cd "$(dirname "$0")/.."
python3 -I tests/key.py
