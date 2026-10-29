#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The site helper (packaging/longwire-site): what it writes and starts, and what it refuses. Run as an ordinary user with stand-ins for systemctl and
# setfacl and a made-up network card, so nothing on this machine changes.
cd "$(dirname "$0")/.." || exit 1
H=$PWD/packaging/longwire-site
T=$(mktemp -d /tmp/lwsite.XXXXXX) || exit 1
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/etc" "$T/sysnet/eth9" "$T/run/eth9"
: > "$T/run/eth9/ctl.plain"
for c in systemctl setfacl; do
	cat > "$T/bin/$c" <<CL
#!/bin/sh
echo "$c \$*" >> "\$LWS_LOG"
[ "$c" = systemctl ] && [ "\$1" = is-active ] && exit \${LWS_ACTIVE:-1}
exit 0
CL
	chmod +x "$T/bin/$c"
done
export LWS_LOG=$T/log PATH="$T/bin:$PATH" LONGWIRE_ETC=$T/etc LONGWIRE_SYSNET=$T/sysnet LONGWIRE_RUN_DIR=$T/run
uid=$(id -u)
[ "$uid" != 0 ] || export LONGWIRE_SITE_ROOT_OK=1
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
# a control socket for the helper to find
python3 - "$T/run/eth9/ctl" <<'PY' &
import socket, sys, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.bind(sys.argv[1]); s.listen(1); time.sleep(40)
PY
sock=$!
sleep 0.5
run() { : > "$LWS_LOG"; out=$(sh "$H" "$@" 2>&1); rc=$?; }
args=$T/args

echo "-- site: the privileged helper"
printf -- '--switchboard sb.example.org:1877/lw1abcdef --token 5fe1c0ab12 --name vpn-1 --answer known --listed\n' > $args
run eth9 $args $uid
[ $rc = 0 ] && grep -q '^ARGS="--switchboard sb.example.org:1877/lw1abcdef --token 5fe1c0ab12 --name vpn-1 --answer known --listed"$' "$T/etc/eth9.conf" && ok "the service file is made from the options" || bad "conf: rc=$rc $out $(cat "$T/etc/eth9.conf" 2>&1)"
[ "$(stat -c %a "$T/etc/eth9.conf")" = 600 ] && ok "and is for root alone (it holds a token)" || bad "mode $(stat -c %a "$T/etc/eth9.conf")"
grep -q "systemctl enable --now longwire@eth9.service" "$LWS_LOG" && ok "longwire@eth9 is enabled and started" || bad "systemctl: $(cat "$LWS_LOG")"
grep -q "setfacl -m u:$uid:rw $T/run/eth9/ctl" "$LWS_LOG" && ok "the user may use its control socket, without logging in again" || bad "setfacl: $(cat "$LWS_LOG")"
LWS_ACTIVE=0 run eth9 $args $uid
grep -q "systemctl restart longwire@eth9.service" "$LWS_LOG" && ok "a node that already runs is restarted, to take the new options" || bad "restart: $(cat "$LWS_LOG")"
run --stop eth9
[ $rc = 0 ] && grep -q "systemctl disable --now longwire@eth9.service" "$LWS_LOG" && ok "--stop stops and disables it" || bad "stop: rc=$rc $(cat "$LWS_LOG")"

echo "-- site: what it refuses (nothing is written or started)"
refuse() {		# description, expected message part, then the helper's arguments
	d=$1; m=$2; shift 2
	rm -f "$T/etc/"*.conf; : > "$LWS_LOG"
	out=$(sh "$H" "$@" 2>&1); rc=$?
	[ $rc != 0 ] && echo "$out" | grep -q "$m" && [ ! -e "$T/etc/eth9.conf" ] && ! grep -q "enable\|restart" "$LWS_LOG" && ok "$d" || bad "$d: rc=$rc $out"
}
printf -- '--exec-start /bin/sh\n' > $T/a1;                       refuse "an option that is not on the list" "not one a site node takes" eth9 $T/a1 $uid
printf -- '--listen 0.0.0.0:22\n' > $T/a2;                        refuse "--listen is not let through" "not one a site node takes" eth9 $T/a2 $uid
printf -- '--token a"b\n' > $T/a3;                                refuse "a token with a quote" "character a service file cannot carry" eth9 $T/a3 $uid
printf -- '--token a\\b\n' > $T/a3b;                              refuse "a token with a backslash" "character a service file cannot carry" eth9 $T/a3b $uid
printf -- '--switchboard a:1/b;reboot\n' > $T/a4;                 refuse "a switchboard with a semicolon" "wants HOST:PORT/KEY" eth9 $T/a4 $uid
printf -- '--name two words\n' > $T/a5;                           refuse "a name with a space" "not one a site node takes\|letters, digits" eth9 $T/a5 $uid
printf -- '--answer maybe\n' > $T/a6;                             refuse "an answer policy that does not exist" "ask, known, auto or never" eth9 $T/a6 $uid
printf -- '--token\n' > $T/a7;                                    refuse "an option with no value" "wants a value" eth9 $T/a7 $uid
printf -- '--token ok\n' > $T/ok1
refuse "lo" "not a real network card\|may splice" lo $T/ok1 $uid
refuse "a jack" "may splice" lw0 $T/ok1 $uid
refuse "a card that is not there" "no network card called" eth77 $T/ok1 $uid
refuse "a name with a slash" "may splice" ../etc $T/ok1 $uid
refuse "no user" "no user" eth9 $T/ok1
ln -sf $T/ok1 $T/link;                                            refuse "a symbolic link" "not a plain file" eth9 $T/link $uid

echo "-- longwire site: the script hands the helper a file, not a command line"
HIBR=${HIBR:-../hibr/build/hibr}
cat > "$T/fake-site" <<'FS'
#!/bin/sh
echo "site $*" >> "$LWS_LOG"
for a in "$@"; do [ -f "$a" ] && { echo "FILE $(cat "$a")" >> "$LWS_LOG"; echo "MODE $(stat -c %a "$a")" >> "$LWS_LOG"; }; done
FS
chmod +x "$T/fake-site"
cat > "$T/bin/pkexec" <<'PK'
#!/bin/sh
exec "$@"
PK
chmod +x "$T/bin/pkexec"
export LONGWIRE_SITE=$T/fake-site LONGWIRE_MOD=$PWD/build/longwire.so HOME=$T XDG_CONFIG_HOME=$T/cfg LW_SOCK=$T/none.sock LW_DIR=$T/lw
lw() { "$HIBR" scripts/longwire.hibr "$@"; }
lw profile set s switchboard=sb.example.org:1877/lw1abc token=tok123 name=vpn-1 answer=known listed=yes attach=splice interface=eth9 > /dev/null 2>&1
: > "$LWS_LOG"; out=$(lw site eth9 s 2>&1); rc=$?
grep -q "^site eth9 $T/lw/site-args" "$LWS_LOG" && grep -qx "FILE --switchboard sb.example.org:1877/lw1abc --token tok123 --name vpn-1 --answer known --listed" "$LWS_LOG" && ok "the helper gets the card and a file with the options" || bad "site: rc=$rc $out $(cat "$LWS_LOG")"
grep -qx "MODE 600" "$LWS_LOG" && ok "the file is for the user alone (it holds the token)" || bad "args mode: $(cat "$LWS_LOG")"
grep -q "tok123" "$LWS_LOG" && ! grep -q "^site .*tok123" "$LWS_LOG" && ok "and the token is not on the command line" || bad "token in argv: $(cat "$LWS_LOG")"
[ ! -e "$T/lw/site-args" ] && ok "and the file is removed afterwards" || bad "args file left behind"
lw profile set s name="Two Words" > /dev/null 2>&1
: > "$LWS_LOG"; out=$(lw site eth9 s 2>&1)
grep -qx "FILE --switchboard sb.example.org:1877/lw1abc --token tok123 --answer known --listed" "$LWS_LOG" && echo "$out" | grep -q "goes without it" && ok "a name with a space is left out, and it says so" || bad "name: $out $(cat "$LWS_LOG")"
: > "$LWS_LOG"; lw site --stop eth9 > /dev/null 2>&1
grep -qx "site --stop eth9" "$LWS_LOG" && ok "--stop asks the helper to stop it" || bad "stop: $(cat "$LWS_LOG")"
out=$(lw site eth9 nosuch 2>&1); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q "no profile called nosuch" && ok "a profile that is not there is an error" || bad "no profile: rc=$rc $out"
out=$(LONGWIRE_SITE=/nonexistent lw site eth9 s 2>&1); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q "site helper is not installed" && ok "and no helper says so, and what to do by hand" || bad "no helper: rc=$rc $out"
kill $sock 2> /dev/null
[ $fail = 0 ] && echo "all site checks passed"
exit $fail
