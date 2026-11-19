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
n=$(printf '%s\n' "$out" | grep -c '^[0-9a-f]\{32\}$')
[ $rc = 0 ] && [ "$n" = 3 ] && ok "three tokens are made, 32 hex digits (128 bits) each" || bad "token new 3: rc=$rc $out"
mode=$(stat -c %a "$T/sb/tokens")
[ "$mode" = 600 ] && ok "the file is for its owner only" || bad "mode $mode"
t1=$(printf '%s\n' "$out" | sed -n 1p)
t2=$(printf '%s\n' "$out" | sed -n 2p)
[ "$t1" != "$t2" ] && ok "and they differ" || bad "same token twice"
out2=$(sb token list 2>&1)
[ "$(printf '%s\n' "$out2" | grep -c '^[0-9a-f]\{32\}$')" = 3 ] && printf '%s' "$out2" | grep -q "(3 unspent)" && ok "list shows them and counts them" || bad "list: $out2"
sb token remove "$t1" > /dev/null 2>&1; rc=$?
[ $rc = 0 ] && ! grep -q "$t1" "$T/sb/tokens" && grep -q "$t2" "$T/sb/tokens" && ok "remove takes out that one and keeps the rest" || bad "remove: rc=$rc $(cat "$T/sb/tokens")"
sb token remove "$t1" > /dev/null 2>&1; [ $? = 1 ] && ok "a token that is not there is an error" || bad "remove twice"
printf '# a comment\n\nabc123 312-884-076\n' >> "$T/sb/tokens"
sb token remove abc123 > /dev/null 2>&1; grep -q '^# a comment' "$T/sb/tokens" && ! grep -q abc123 "$T/sb/tokens" && ok "a token with a reserved number goes by its first word; comments stay" || bad "reserved: $(cat "$T/sb/tokens")"
sb token new 0 > /dev/null 2>&1; [ $? = 2 ] && ok "a count of 0 is refused" || bad "count 0"
sb token new abc > /dev/null 2>&1; [ $? = 2 ] && ok "so is a count that is not a number" || bad "count abc"
sb token bogus > /dev/null 2>&1; [ $? = 2 ] && ok "and an unknown subcommand" || bad "bogus"
sb token new 1 --tokens "$T/other" > /dev/null 2>&1; [ -s "$T/other" ] && ok "--tokens names another file" || bad "--tokens"
# invite: one token and the link a phone opens
MOD=$PWD/build/longwire.so
"$HIBR" -c "mod load $MOD; lw key new $T/sb/seed" > /dev/null 2>&1
before=$(grep -c . "$T/sb/tokens")
link=$(LONGWIRE_MOD=$MOD sb token invite --host sb.example.org:6741 --dial 312-884-076 --name "Omar's phone" 2> /dev/null); rc=$?
tk=$(printf '%s' "$link" | sed -n 's/.*[?&]token=\([0-9a-f]*\).*/\1/p')
[ $rc = 0 ] && grep -q "^$tk\$" "$T/sb/tokens" && [ "$(grep -c . "$T/sb/tokens")" = $((before + 1)) ] && ok "invite makes one token, in the file, and prints it in a link" || bad "invite: rc=$rc $link"
printf '%s' "$link" | grep -qE '^longwire://enroll\?sb=sb\.example\.org:6741/lw1[a-z2-7]{52}&token=[0-9a-f]{32}&dial=312-884-076&name=[%0-9a-f]+$' && ok "the link says the switchboard's full address, the token, home and the name" || bad "link shape: $link"
# the app takes only what matches these (android/app/java/org/longwire/MainActivity.java): what the script makes must pass them
python3 -I - "$link" <<'PY' && ok "...and it passes the checks the app makes of a link" || bad "the app would refuse the link"
import re, sys, urllib.parse
u = urllib.parse.urlparse(sys.argv[1]); q = urllib.parse.parse_qs(u.query)
assert u.scheme == "longwire" and u.netloc == "enroll"
assert re.fullmatch(r"[A-Za-z0-9._-]{1,200}:[0-9]{1,5}/lw1[a-z2-7]{52}", q["sb"][0])
assert re.fullmatch(r"[0-9a-f]{32}", q["token"][0])
assert re.fullmatch(r"[0-9]{3}-[0-9]{3}-[0-9]{3}|lw1[a-z2-7]{52}", q["dial"][0])
assert len(q["name"][0]) <= 40 and re.fullmatch(r"[\w .'-]*", q["name"][0]) and q["name"][0] == "Omar's phone"
PY
sb token invite > /dev/null 2>&1; [ $? = 2 ] && ok "invite without --host is refused" || bad "invite without host"
sb token invite --host 'a b:1' > /dev/null 2>&1; [ $? = 2 ] && ok "so is a host that is not a name and a port" || bad "invite odd host"
SB_DIR="$T/none" "$HIBR" "$SB" token invite --host h:1 > /dev/null 2>&1; [ $? = 1 ] && ok "and an invitation from a place no switchboard has run" || bad "invite without seed"
# show, and invite without --host: from a running switchboard (its own port, in its status)
"$HIBR" -c "mod load $MOD; lw sb start --seed $T/sb/seed --sock $T/sb/ctl --state $T/sb --listen 127.0.0.1:41993 --registry none" > /dev/null 2>&1
sleep 1
addr=$("$HIBR" -c "mod load $MOD; lw key address $T/sb/seed")
shown=$(LONGWIRE_MOD=$MOD sb show 2>&1)
printf '%s' "$shown" | grep -q "^address      $addr$" && printf '%s' "$shown" | grep -q "^port         41993 (udp)" && ok "show says the switchboard's address and port, from the switchboard itself" || bad "show: $shown"
printf '%s' "$shown" | grep -q "token invite --host .*:41993 --dial" && ok "...and the command that invites a phone, with its host and port filled in" || bad "show, no invite line: $shown"
link2=$(LONGWIRE_MOD=$MOD sb token invite --dial 312-884-076 2> /dev/null)
printf '%s' "$link2" | grep -qE "^longwire://enroll\?sb=[A-Za-z0-9._-]+:41993/$addr&token=[0-9a-f]{32}&dial=312-884-076$" && ok "invite without --host takes this machine's name and the switchboard's own port" || bad "invite without host: $link2"
"$HIBR" -c "mod load $MOD; lw stop --sock $T/sb/ctl" > /dev/null 2>&1
SB_DIR="$T/none" LONGWIRE_MOD=$MOD "$HIBR" "$SB" show > /dev/null 2>&1; [ $? = 1 ] && ok "show with no switchboard running says so" || bad "show with none"
[ $fail = 0 ] && echo "all tokens checks passed"
exit $fail
