#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A second box joins the first on one LAN segment through a real switchboard, with the site secret coming from the line key, while a host on the LAN keeps broadcasting (ADR 0083); once with the
# newcomer's key id the lower, once the higher. Needs unprivileged user and network namespaces.
set -e
cd "$(dirname "$0")/.."
for order in lower higher; do
	unshare -Urn python3 -I tests/m2c.py "$order"
done
