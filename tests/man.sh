#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The manual pages (man/): they lint without a warning, and they name every command and option the scripts have, so they cannot fall
# behind the code. Needs groff (the tool that renders them).
set -e
cd "$(dirname "$0")/.."
if ! command -v groff > /dev/null; then
	echo "skip man: groff is not installed"
	exit 0
fi
rc=0
for f in man/*.1 man/*.5 man/*.7; do
	out=$(groff -man -Tutf8 -ww -z "$f" 2>&1) || true
	if [ -n "$out" ]; then echo "FAIL $f does not lint: $out"; rc=1; else echo "ok   $f lints without a warning"; fi
done
python3 -I tests/man.py || rc=1
exit $rc
