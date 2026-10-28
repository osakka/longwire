#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# `longwire-switchboard token`: make, list and remove registration tokens. It touches only the tokens file and needs no running switchboard.
cd "$(dirname "$0")/.." || exit 1
HIBR=${HIBR:-../hibr/build/hibr}
SB=$PWD/scripts/longwire-switchboard.hibr
T=$(mktemp -d /tmp/lwtok.XXXXXX) || exit 1
trap 'rm -rf "$T"' EXIT
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
sb() { SB_DIR="$T/sb" "$HIBR" "$SB" "$@"; }

out=$(sb token new 3 2>&1); rc=$?
n=$(printf '%s\n' "$out" | grep -c '^[0-9a-f]\{20\}$')
[ $rc = 0 ] && [ "$n" = 3 ] && ok "three tokens are made, 20 hex digits each" || bad "token new 3: rc=$rc $out"
mode=$(stat -c %a "$T/sb/tokens")
[ "$mode" = 600 ] && ok "the file is for its owner only" || bad "mode $mode"
t1=$(printf '%s\n' "$out" | sed -n 1p)
t2=$(printf '%s\n' "$out" | sed -n 2p)
[ "$t1" != "$t2" ] && ok "and they differ" || bad "same token twice"
out2=$(sb token list 2>&1)
[ "$(printf '%s\n' "$out2" | grep -c '^[0-9a-f]\{20\}$')" = 3 ] && printf '%s' "$out2" | grep -q "(3 unspent)" && ok "list shows them and counts them" || bad "list: $out2"
sb token remove "$t1" > /dev/null 2>&1; rc=$?
[ $rc = 0 ] && ! grep -q "$t1" "$T/sb/tokens" && grep -q "$t2" "$T/sb/tokens" && ok "remove takes out that one and keeps the rest" || bad "remove: rc=$rc $(cat "$T/sb/tokens")"
sb token remove "$t1" > /dev/null 2>&1; [ $? = 1 ] && ok "a token that is not there is an error" || bad "remove twice"
printf '# a comment\n\nabc123 312-884-076\n' >> "$T/sb/tokens"
sb token remove abc123 > /dev/null 2>&1; grep -q '^# a comment' "$T/sb/tokens" && ! grep -q abc123 "$T/sb/tokens" && ok "a token with a reserved number goes by its first word; comments stay" || bad "reserved: $(cat "$T/sb/tokens")"
sb token new 0 > /dev/null 2>&1; [ $? = 2 ] && ok "a count of 0 is refused" || bad "count 0"
sb token new abc > /dev/null 2>&1; [ $? = 2 ] && ok "so is a count that is not a number" || bad "count abc"
sb token bogus > /dev/null 2>&1; [ $? = 2 ] && ok "and an unknown subcommand" || bad "bogus"
sb token new 1 --tokens "$T/other" > /dev/null 2>&1; [ -s "$T/other" ] && ok "--tokens names another file" || bad "--tokens"
[ $fail = 0 ] && echo "all tokens checks passed"
exit $fail
