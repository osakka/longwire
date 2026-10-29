#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The jack helper (packaging/longwire-jack): what it runs as root, and what it refuses. It is run here as an ordinary user with a stand-in
# `ip` that only writes down what it was asked, so nothing on this machine changes.
cd "$(dirname "$0")/.." || exit 1
HIBR=$(readlink -f "${HIBR:-../hibr/build/hibr}")
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

run() { : > "$LWJ_LOG"; out=$("$HIBR" "$H" "$@" 2>&1); rc=$?; }

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

echo "-- jack --dhcp: a client on the jack, without a default route or name servers"
RUN=$T/run
export LONGWIRE_RUN=$RUN
mkclient() {			# a stand-in client that writes down what it was asked
	cat > "$T/bin/$1" <<CL
#!/bin/sh
echo "$1 \$*" >> "\$LWJ_LOG"
CL
	chmod +x "$T/bin/$1"
}
cat > "$T/real-script" <<'RS'
#!/bin/sh
echo "REAL ip=${new_ip_address:-} routers=${new_routers:-} dns=${new_domain_name_servers:-} router=${router:-} dnsu=${dns:-} args=$*" >> "$LWJ_LOG"
RS
chmod +x "$T/real-script"
export LONGWIRE_DHCP_SCRIPT=$T/real-script
cp /bin/sleep "$T/bin/dhclient"	# a client of ours from before: a process called dhclient
"$T/bin/dhclient" 60 & oldpid=$!
mkdir -p "$RUN"; echo $oldpid > "$RUN/dhcp-lw7.pid"
rm -f "$T/bin/dhclient"; cp /bin/sleep "$T/dhclient"; mkclient dhclient
run --dhcp lw7 1000
[ $rc = 0 ] && grep -q "dhclient -nw -pf $RUN/dhcp-lw7.pid -lf $RUN/dhcp-lw7.leases -sf $RUN/dhcp-hook-lw7 lw7" "$LWJ_LOG" && ok "--dhcp starts dhclient on the jack, with its own pid file, lease file and hook" || bad "dhclient: rc=$rc $out $(cat "$LWJ_LOG")"
sleep 0.2
kill -0 $oldpid 2> /dev/null && { bad "the client from before is still there"; kill $oldpid; } || ok "and the client of ours from before is stopped, so there is only one"
: > "$LWJ_LOG"
new_routers=9.9.9.9 new_domain_name_servers=8.8.8.8 router=9.9.9.9 dns=8.8.8.8 new_ip_address=10.1.1.5 "$RUN/dhcp-hook-lw7" BOUND
[ "$(head -n 1 "$RUN/dhcp-hook-lw7")" = "#!$HIBR" ] && grep -q "REAL ip=10.1.1.5 routers= dns= router= dnsu= args=BOUND" "$LWJ_LOG" && ok "the hook (a hibr script, run directly through its shebang) keeps the address and takes out the default route and the name servers before the system script runs" || bad "hook: $(cat "$LWJ_LOG")"
rm -f "$T/bin/dhclient"
mkclient dhcpcd
run --dhcp lw7 1000
grep -q "dhcpcd -b -G -C resolv.conf -C ntp.conf lw7" "$LWJ_LOG" && ok "without dhclient, dhcpcd: in the background, no gateway, no resolv.conf or ntp.conf" || bad "dhcpcd: rc=$rc $(cat "$LWJ_LOG")"
rm -f "$T/bin/dhcpcd"
mkclient udhcpc
run --dhcp lw7 1000
grep -q "udhcpc -b -i lw7 -p $RUN/dhcp-lw7.pid -s $RUN/dhcp-hook-lw7" "$LWJ_LOG" && ok "and udhcpc after that, with the hook as its script" || bad "udhcpc: rc=$rc $(cat "$LWJ_LOG")"
rm -f "$T/bin/udhcpc"
if command -v dhclient dhcpcd udhcpc > /dev/null 2>&1; then
	echo "skip: no client at all (this machine has one in the search path)"
else
	run --dhcp lw7 1000
	[ $rc = 3 ] && echo "$out" | grep -q "no DHCP client found" && ok "with no client installed it says which to install" || bad "no client: rc=$rc $out"
fi
run --dhcp --address 10.0.0.2/24 lw7 1000
[ $rc = 2 ] && ok "an address and --dhcp together are refused" || bad "both: rc=$rc $out"

echo "-- longwire jack: the script asks the helper (a stand-in) the right thing"
cat > "$T/fake-helper" <<'FH'
#!/bin/sh
echo "helper $*" >> "$LWJ_LOG"
FH
chmod +x "$T/fake-helper"
cat > "$T/bin/pkexec" <<'PK'
#!/bin/sh
exec "$@"
PK
chmod +x "$T/bin/pkexec"
export LONGWIRE_JACK=$T/fake-helper LONGWIRE_MOD=$PWD/build/longwire.so HOME=$T LW_SOCK=$T/none.sock LW_DIR=$T/lw
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address dhcp lw7 2>&1)
grep -qx "helper --dhcp lw7" "$LWJ_LOG" && ok "longwire jack --address dhcp asks the helper for --dhcp" || bad "jack dhcp: $out $(cat "$LWJ_LOG")"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address 10.77.0.2/24 lw7 2>&1)
grep -qx "helper --address 10.77.0.2/24 lw7" "$LWJ_LOG" && ok "and an address as --address" || bad "jack addr: $out $(cat "$LWJ_LOG")"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack lw7 2>&1)
grep -qx "helper lw7" "$LWJ_LOG" && ok "and with neither, neither" || bad "jack plain: $out $(cat "$LWJ_LOG")"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p address=dhcp 2>&1); rc=$?
[ $rc = 0 ] && ok "a profile may say address=dhcp" || bad "profile dhcp: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p address=dhcp2 2>&1); rc=$?
[ $rc = 2 ] && ok "but not address=dhcp2" || bad "profile dhcp2: rc=$rc $out"
[ $fail = 0 ] && echo "all jack checks passed"
exit $fail
