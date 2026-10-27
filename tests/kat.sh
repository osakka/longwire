#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Known answers for the vendored crypto, built by TCC (docs/adr/0021): see tests/kat.py.
set -e
cd "$(dirname "$0")/.."
if ! python3 -c "import cryptography" 2> /dev/null; then
	echo "skip kat: Python's cryptography package is not installed"
	exit 0
fi
mkdir -p build
tcc -Ivendor/monocypher -o build/kat tests/kat.c vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c
python3 -I tests/kat.py build/kat
