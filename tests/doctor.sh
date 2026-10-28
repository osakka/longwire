#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# `longwire doctor`: the scripts (the catalogue's), the installed engine (the package's) and the running node each have a version, and a fix can sit in
# one of them and look as if it did nothing. No node runs here; the scripts' version is set by hand to be newer and older than the engine's.
cd "$(dirname "$0")/.." || exit 1
HIBR=${HIBR:-../hibr/build/hibr}
export LONGWIRE_MOD=$PWD/build/longwire.so
T=$(mktemp -d /tmp/lwdoc.XXXXXX) || exit 1
trap 'rm -rf "$T"' EXIT
export HOME=$T LW_SOCK=$T/none.sock LW_DIR=$T/lw
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
ver=$(cat VERSION)

out=$("$HIBR" scripts/longwire.hibr doctor 2>&1); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "agree ($ver)" && ok "same version everywhere: doctor says they agree" || bad "agree: rc=$rc $out"
out=$(LONGWIRE_SELF_VERSION=9.9.9 "$HIBR" scripts/longwire.hibr doctor 2>&1); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q "package ($ver) is older than these scripts (9.9.9): sudo apt install --only-upgrade longwire" && ok "a package older than the scripts: doctor says to update the package" || bad "package older: rc=$rc $out"
out=$(LONGWIRE_SELF_VERSION=0.0.1 "$HIBR" scripts/longwire.hibr doctor 2>&1); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q "scripts (0.0.1) are older than the installed engine ($ver): catalogue refresh" && ok "scripts older than the engine: doctor says to update the scripts" || bad "scripts older: rc=$rc $out"
out=$(LONGWIRE_SELF_VERSION=0.5.9 "$HIBR" scripts/longwire.hibr show 2>&1); rc=$?
echo "$out" | grep -q "longwire is off" && ok "show says nothing about versions when no node runs (it is off)" || bad "show off: $out"
out=$(LONGWIRE_MOD=/nonexistent/none.so "$HIBR" scripts/longwire.hibr doctor 2>&1); rc=$?
[ $rc != 0 ] && ok "and no engine at all is an error, not a silent pass" || bad "no module: rc=$rc $out"
[ $fail = 0 ] && echo "all doctor checks passed"
exit $fail
