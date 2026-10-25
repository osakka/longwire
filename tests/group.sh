#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The control socket's group (docs/adr/0032): who may talk to a node's control socket, as three real users. It needs a user
# namespace with more than one user in it, which only newuidmap/newgidmap and a /etc/subuid entry give; where they are
# missing the test says so and is skipped, because the mode and the group of the socket file are then all it could check.
set -e
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
MOD=$PWD/build/longwire.so
[ "$(id -u)" != 0 ] || { echo "skip the control socket group test: it needs an ordinary user to map more ids from"; exit 0; }
command -v newuidmap > /dev/null && command -v newgidmap > /dev/null && command -v setpriv > /dev/null &&
	grep -q "^$(id -un):" /etc/subuid && grep -q "^$(id -un):" /etc/subgid ||
	{ echo "skip the control socket group test: no newuidmap, setpriv or subuid range for $(id -un)"; exit 0; }
T=$(mktemp -d /tmp/lwgr.XXXXXX)
chmod 755 "$T"
sub=$(grep "^$(id -un):" /etc/subuid | head -1 | cut -d: -f2)
subg=$(grep "^$(id -un):" /etc/subgid | head -1 | cut -d: -f2)
unshare -Un --kill-child sleep 120 &
P=$!
cleanup() { kill "$P" 2> /dev/null || true; rm -rf "$T"; }
trap cleanup EXIT
n=0
while [ "$(readlink /proc/$P/ns/user 2> /dev/null)" = "$(readlink /proc/self/ns/user)" ]; do
	n=$((n + 1)); [ $n -lt 100 ] || { echo "FAIL the user namespace did not appear"; exit 1; }
	sleep 0.05
done
newuidmap $P 0 "$(id -u)" 1 1 "$sub" 3
newgidmap $P 0 "$(id -g)" 1 1 "$subg" 3
nsenter -t $P -U -n -S 0 -G 0 python3 -I tests/group.py "$T" "$HIBR" "$MOD"
