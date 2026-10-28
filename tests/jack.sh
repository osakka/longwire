#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The jack helper (packaging/longwire-jack): what it runs as root, and what it refuses. It is run here as an ordinary user with a stand-in
# `ip` that only writes down what it was asked, so nothing on this machine changes.
cd "$(dirname "$0")/.." || exit 1
H=$PWD/packaging/longwire-jack
T=$(mktemp -d /tmp/lwjack.XXXXXX) || exit 1
trap 'rm -rf "$T"' EXIT
mkdir "$T/bin"
cat > "$T/bin/ip" <<'IP'
#!/bin/sh
echo "ip $*" >> "$LWJ_LOG"
IP
chmod +x "$T/bin/ip"
export LWJ_LOG="$T/log" PATH="$T/bin:$PATH"
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }

run() { : > "$LWJ_LOG"; out=$(sh "$H" "$@" 2>&1); rc=$?; }

echo "-- jack: the privileged helper"
run lw7 1000
[ $rc = 0 ] && grep -q "tuntap add dev lw7 mode tap user 1000" "$LWJ_LOG" && grep -q "link set lw7 mtu 1400 up" "$LWJ_LOG" && ! grep -q "addr" "$LWJ_LOG" \
	&& ok "a jack is made, owned by the user, up, with no address when none is asked" || bad "plain jack: rc=$rc $(cat "$LWJ_LOG")"
run --address 10.77.0.2/24 lw7 1000
[ $rc = 0 ] && grep -q "addr replace 10.77.0.2/24 dev lw7" "$LWJ_LOG" && ok "an address is put on it" || bad "address: rc=$rc $(cat "$LWJ_LOG")"
for a in 10.77.0.2/7 10.77.0.2/31 10.77.0.2 10.77.0.256/24 10.77.0/24 127.0.0.1/8 0.1.2.3/8 224.0.0.1/24 "10.0.0.1/24;id" "10.0.0.1/24 x" '$(id)/24' 10..0.1/24 10.0.0.1.1/24 a.b.c.d/24 /24 10.0.0.1/ 10.0.0.1/-8; do
	run --address "$a" lw7 1000
	[ $rc = 2 ] && [ ! -s "$LWJ_LOG" ] && ok "refused before anything is run: '$a'" || bad "'$a': rc=$rc log=$(cat "$LWJ_LOG")"
done
run --address
[ $rc = 2 ] && ok "--address with no value is refused" || bad "bare --address: rc=$rc"
run lw100 1000
[ $rc = 2 ] && [ ! -s "$LWJ_LOG" ] && ok "only lw0 to lw99" || bad "lw100: rc=$rc"
run eth0 1000
[ $rc = 2 ] && [ ! -s "$LWJ_LOG" ] && ok "never a real interface (eth0)" || bad "eth0: rc=$rc"
run lw7 0
[ $rc = 2 ] && [ ! -s "$LWJ_LOG" ] && ok "never given to root" || bad "uid 0: rc=$rc"
run lw7
[ $rc = 2 ] && ok "and not without a user" || bad "no uid: rc=$rc"
PKEXEC_UID=1234 run --address 192.168.152.5/24 lw3
grep -q "tuntap add dev lw3 mode tap user 1234" "$LWJ_LOG" && ok "under pkexec the user is PKEXEC_UID" || bad "pkexec: $(cat "$LWJ_LOG")"
[ $fail = 0 ] && echo "all jack checks passed"
exit $fail
