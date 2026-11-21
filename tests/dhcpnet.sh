#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The jack's DHCP client on a real interface (docs/adr/0102): lw-dhcp gets an address from a DHCP server, keeps asking until one answers, refuses a lease that is not a LAN's, says what it did to the
# host, puts nothing on the jack but the address, and takes the address off again when it is stopped. A veth pair stands in for the jack; the other end of it is the LAN, played by a script.
# Needs unprivileged user and network namespaces (see docs/BUILD.md) and `ip`.
set -e
cd "$(dirname "$0")/.."
make -s build/lw-dhcp > /dev/null
unshare -Urn python3 -I tests/dhcpnet.py
