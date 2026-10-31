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
case "$*" in
"-o link show dev "*) [ -n "${LWJ_CARRIER:-}" ] && echo "9: lw7: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1400" || echo "9: lw7: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1400" ;;
"-4 rule show pref 7701") [ -n "${LWJ_HELD:-}" ] && echo "7701:	from all lookup 51820" ;;
esac
IP
chmod +x "$T/bin/ip"
printf '#!/bin/sh\necho "resolvectl $*" >> "$LWJ_LOG"\n' > "$T/bin/resolvectl"; chmod +x "$T/bin/resolvectl"		# systemd-resolved, written down too
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
export PATH="$T/bin:$PATH"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address 192.168.152.50/24 --via 192.168.152.254 --routes 192.168.150.0/24,10.20.0.0/16 lw7 2>&1)
grep -qx "helper --address 192.168.152.50/24 --via 192.168.152.254 --routes 192.168.150.0/24,10.20.0.0/16 lw7" "$LWJ_LOG" && ok "longwire jack passes the gateway and the routes to the helper" || bad "jack routes: $out $(cat "$LWJ_LOG")"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address 192.168.152.50/24 --via 192.168.152.254 --everything lw7 2>&1)
grep -qx "helper --address 192.168.152.50/24 --via 192.168.152.254 --everything lw7" "$LWJ_LOG" && ok "longwire jack passes everything through the line to the helper" || bad "jack everything: $out $(cat "$LWJ_LOG")"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address 192.168.152.50/24 --via 192.168.152.254 lw7 2>&1)
grep -qx "helper --address 192.168.152.50/24 lw7" "$LWJ_LOG" && ok "a gateway with nothing to be the gateway for is not passed on" || bad "lone via: $out $(cat "$LWJ_LOG")"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address 192.168.152.50/24 --everything lw7 2>&1); rc=$?
[ $rc != 0 ] && echo "$out" | grep -q "needs a gateway" && ! grep -q helper "$LWJ_LOG" && ok "everything without a gateway is refused, with what to set" || bad "jack everything no gw: rc=$rc $out"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address 192.168.152.50/24 --via 192.168.152.254 --everything --dns 192.168.152.254 lw7 2>&1)
grep -qx "helper --address 192.168.152.50/24 --via 192.168.152.254 --everything --dns 192.168.152.254 lw7" "$LWJ_LOG" && ok "and the name server with it" || bad "jack dns: $out $(cat "$LWJ_LOG")"
: > "$LWJ_LOG"; out=$("$HIBR" scripts/longwire.hibr jack --address 192.168.152.50/24 --via 192.168.152.254 --routes 10.20.0.0/16 --dns 192.168.152.254 lw7 2>&1)
grep -qx "helper --address 192.168.152.50/24 --via 192.168.152.254 --routes 10.20.0.0/16 lw7" "$LWJ_LOG" && ok "but not without everything" || bad "jack dns alone: $out $(cat "$LWJ_LOG")"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p dns=192.168.152.254 2>&1); rc=$?
[ $rc = 0 ] && ok "a profile keeps a name server" || bad "profile dns: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p 'dns=1.1.1.1;id' 2>&1); rc=$?
[ $rc = 2 ] && ok "and refuses one that is not an address" || bad "profile bad dns: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p everything=yes 2>&1); rc=$?
[ $rc = 0 ] && ok "a profile may say everything=yes" || bad "profile everything: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p everything=maybe 2>&1); rc=$?
[ $rc = 2 ] && ok "but not everything=maybe" || bad "profile everything maybe: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p gateway=192.168.152.254 routes=192.168.150.0/24,10.20.0.0/16 2>&1); rc=$?
[ $rc = 0 ] && ok "a profile keeps a gateway and its routes" || bad "profile routes: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p 'routes=10.0.0.0/8;reboot' 2>&1); rc=$?
[ $rc = 2 ] && ok "and refuses routes with anything but digits, dots, slashes and commas" || bad "profile bad routes: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p gateway=router 2>&1); rc=$?
[ $rc = 2 ] && ok "and a gateway that is not an address" || bad "profile bad gateway: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p address=dhcp 2>&1); rc=$?
[ $rc = 0 ] && ok "a profile may say address=dhcp" || bad "profile dhcp: rc=$rc $out"
out=$(XDG_CONFIG_HOME=$T/cfg "$HIBR" scripts/longwire.hibr profile set p address=dhcp2 2>&1); rc=$?
[ $rc = 2 ] && ok "but not address=dhcp2" || bad "profile dhcp2: rc=$rc $out"

echo "-- jack --remove: take the jack away, if it is the user's own"
mkdir -p "$T/sysnet/lw7" "$T/sysnet/lw8"
echo 1000 > "$T/sysnet/lw7/owner"; echo 4242 > "$T/sysnet/lw8/owner"
export LONGWIRE_SYSNET=$T/sysnet
run --remove lw7 1000
[ $rc = 0 ] && grep -q "ip link delete lw7" "$LWJ_LOG" && ok "--remove deletes the jack that is the user's own" || bad "remove: rc=$rc $out $(cat "$LWJ_LOG")"
run --remove lw8 1000
[ $rc = 3 ] && ! grep -q "link delete" "$LWJ_LOG" && ok "and refuses one that belongs to somebody else" || bad "remove other: rc=$rc $out $(cat "$LWJ_LOG")"
run --remove lw5 1000
[ $rc = 0 ] && echo "$out" | grep -q "nothing to remove" && ok "a jack that is not there is not an error" || bad "remove absent: rc=$rc $out"
run --remove eth0 1000
[ $rc = 2 ] && ok "and --remove never touches a real card" || bad "remove eth0: rc=$rc $out"
run --remove --dhcp lw7 1000
[ $rc = 2 ] && ok "--remove with --dhcp is refused" || bad "remove+dhcp: rc=$rc $out"
cp /bin/sleep "$T/dhclient"; "$T/dhclient" 60 & pid2=$!
mkdir -p "$RUN"; echo $pid2 > "$RUN/dhcp-lw7.pid"; touch "$RUN/dhcp-hook-lw7"
mkdir -p "$T/sysnet/lw7"; echo 1000 > "$T/sysnet/lw7/owner"
run --remove lw7 1000
[ ! -e "$RUN/dhcp-lw7.pid" ] && [ ! -e "$RUN/dhcp-hook-lw7" ] && ok "its DHCP files go with it, and its client is stopped" || bad "remove files: $(ls "$RUN")"
sleep 0.2; kill -0 $pid2 2> /dev/null && { bad "the DHCP client is still running"; kill $pid2; } || ok "(the client of ours is gone)"

echo "-- jack --routes: networks behind a gateway on the line"
unset LONGWIRE_SYSNET
run --address 192.168.152.50/24 --via 192.168.152.254 --routes 192.168.150.0/24,10.20.0.0/16 lw7 1000
[ $rc = 0 ] && grep -q "ip route replace 192.168.150.0/24 via 192.168.152.254 dev lw7" "$LWJ_LOG" && grep -q "ip route replace 10.20.0.0/16 via 192.168.152.254 dev lw7" "$LWJ_LOG" \
	&& ok "each network gets a route through the gateway, on the jack" || bad "routes: rc=$rc $out $(cat "$LWJ_LOG")"
grep -n "addr replace\|route replace" "$LWJ_LOG" | head -1 | grep -q "addr replace" && ok "the address goes on first (the gateway must be on-link)" || bad "order: $(cat "$LWJ_LOG")"
refr() {	# description, expected message, then the arguments after --address 192.168.152.50/24
	d=$1; m=$2; shift 2
	: > "$LWJ_LOG"; out=$("$HIBR" "$H" --address 192.168.152.50/24 "$@" lw7 1000 2>&1); rc=$?
	[ $rc = 2 ] && echo "$out" | grep -q "$m" && ! grep -q "route replace" "$LWJ_LOG" && ok "$d" || bad "$d: rc=$rc $out"
}
refr "the default route is never offered" "a prefix of 8 to 32" --via 192.168.152.254 --routes 0.0.0.0/0
refr "nor half of everything (0.0.0.0/1)" "a prefix of 8 to 32" --via 192.168.152.254 --routes 0.0.0.0/1
refr "nor anything wider than a /8" "a prefix of 8 to 32" --via 192.168.152.254 --routes 10.0.0.0/7
refr "a gateway off the jack's network is refused" "not on the jack's network" --via 192.168.10.1 --routes 192.168.150.0/24
refr "the gateway cannot be this machine" "own address" --via 192.168.152.50 --routes 192.168.150.0/24
refr "nor the network's first address" "first or last" --via 192.168.152.0 --routes 192.168.150.0/24
refr "a network with host bits set is refused, with what to write" "host bits" --via 192.168.152.254 --routes 192.168.150.1/24
refr "the jack's own network needs no route" "overlaps the jack's own network" --via 192.168.152.254 --routes 192.168.0.0/16
refr "loopback is never routed" "can be routed" --via 192.168.152.254 --routes 127.0.0.0/8
refr "nor multicast" "can be routed" --via 192.168.152.254 --routes 224.0.0.0/8
refr "nor link-local" "link-local" --via 192.168.152.254 --routes 169.254.0.0/16
refr "nor something that is not a network" "not a network like" --via 192.168.152.254 --routes '192.168.150.0/24;id'
refr "routes without a gateway are refused" "go together" --routes 192.168.150.0/24
refr "a gateway with nothing to be the gateway for is refused" "for --routes or --everything" --via 192.168.152.254
refr "17 networks are too many" "1 to 16" --via 192.168.152.254 --routes 10.1.0.0/16,10.2.0.0/16,10.3.0.0/16,10.4.0.0/16,10.5.0.0/16,10.6.0.0/16,10.7.0.0/16,10.8.0.0/16,10.9.0.0/16,10.10.0.0/16,10.11.0.0/16,10.12.0.0/16,10.13.0.0/16,10.14.0.0/16,10.15.0.0/16,10.16.0.0/16,10.17.0.0/16
: > "$LWJ_LOG"; out=$("$HIBR" "$H" --dhcp --via 192.168.152.254 --routes 192.168.150.0/24 lw7 1000 2>&1); rc=$?
[ $rc = 2 ] && echo "$out" | grep -q "need the jack's own address" && ok "routes with DHCP are refused plainly (the gateway's network is not known in advance)" || bad "dhcp routes: rc=$rc $out"

echo "-- jack --everything: the default route in the jack's own table, behind two rules (docs/adr/0045)"
P=$T/proc; mkdir -p "$P/ipv4/conf/lw7" "$P/ipv6/conf/lw7"; export LONGWIRE_PROCNET=$P
run --address 192.168.152.50/24 --via 192.168.152.254 --everything lw7 1000
[ $rc = 0 ] && grep -qx "ip route replace default via 192.168.152.254 dev lw7 table 7707" "$LWJ_LOG" && grep -qx "ip -6 route replace default dev lw7 table 7707" "$LWJ_LOG" \
	&& grep -qx "ip -4 rule add pref 7700 lookup main suppress_prefixlength 0" "$LWJ_LOG" && grep -qx "ip -4 rule add pref 7701 lookup 7707" "$LWJ_LOG" \
	&& grep -qx "ip -6 rule add pref 7701 lookup 7707" "$LWJ_LOG" && ok "the default route goes into lw7's own table (7707), behind two rules, IPv6 too" || bad "everything: rc=$rc $out $(cat "$LWJ_LOG")"
read -r v < "$P/ipv4/conf/lw7/ignore_routes_with_linkdown"; read -r v6 < "$P/ipv6/conf/lw7/ignore_routes_with_linkdown"
[ "$v" = 1 ] && [ "$v6" = 1 ] && ok "the kernel is told to skip the jack's routes when it has no carrier" || bad "linkdown: $v $v6"
grep -n "linkdown\|carrier\|link show\|route replace default\|rule add" "$LWJ_LOG" | head -1 | grep -q "link show" && ok "the carrier is looked at before any route or rule goes on" || bad "order: $(cat "$LWJ_LOG")"
! grep -q "route .*default.* dev [^l]\|route del default\|route replace default via [0-9.]* dev lw7$" "$LWJ_LOG" && ok "the machine's own default route is never touched" || bad "main default: $(cat "$LWJ_LOG")"
LWJ_CARRIER=1 run --address 192.168.152.50/24 --via 192.168.152.254 --everything lw7 1000
[ $rc = 3 ] && echo "$out" | grep -q "has carrier" && ! grep -q "rule add\|table 7707" "$LWJ_LOG" && ok "refused when the jack has carrier with no node on it: the safety net would not hold" || bad "carrier: rc=$rc $out"
LWJ_HELD=1 run --address 192.168.152.50/24 --via 192.168.152.254 --everything lw7 1000
[ $rc = 3 ] && echo "$out" | grep -q "something else already takes everything" && ! grep -q "tuntap\|rule add" "$LWJ_LOG" && ok "refused, before anything changes, when something else holds the rules' place" || bad "held: rc=$rc $out"
: > "$LWJ_LOG"; out=$("$HIBR" "$H" --via 192.168.152.254 --everything lw7 1000 2>&1); rc=$?
[ $rc = 2 ] && echo "$out" | grep -q "need the jack's own address" && ok "everything needs a static address" || bad "no addr: rc=$rc $out"
: > "$LWJ_LOG"; out=$("$HIBR" "$H" --address 192.168.152.50/24 --everything lw7 1000 2>&1); rc=$?
[ $rc = 2 ] && echo "$out" | grep -q "name it with --via" && [ ! -s "$LWJ_LOG" ] && ok "and a gateway" || bad "no via: rc=$rc $out"
: > "$LWJ_LOG"; out=$("$HIBR" "$H" --address 192.168.152.50/24 --via 192.168.10.1 --everything lw7 1000 2>&1); rc=$?
[ $rc = 2 ] && echo "$out" | grep -q "not on the jack's network" && ok "on the jack's own network" || bad "via off-net: rc=$rc $out"


echo "-- jack --dns: names through the line, on the jack in systemd-resolved (docs/adr/0047)"
run --address 192.168.152.50/24 --via 192.168.152.254 --everything --dns 192.168.152.254 lw7 1000
[ $rc = 0 ] && grep -qx "resolvectl dns lw7 192.168.152.254" "$LWJ_LOG" && grep -qx "resolvectl domain lw7 ~." "$LWJ_LOG" && grep -qx "resolvectl default-route lw7 yes" "$LWJ_LOG" \
	&& ok "the name server goes on the jack, for every name" || bad "dns: rc=$rc $out $(cat "$LWJ_LOG")"
grep -n "rule add pref 7701\|resolvectl dns" "$LWJ_LOG" | head -1 | grep -q "rule add" && ok "after the route to it is there" || bad "dns order: $(cat "$LWJ_LOG")"
run --address 192.168.152.50/24 lw7 1000
grep -qx "resolvectl revert lw7" "$LWJ_LOG" && ok "without --dns, the jack's name server is taken away" || bad "revert: $(cat "$LWJ_LOG")"
for d in 127.0.0.53 0.0.0.0 224.0.0.1 "1.1.1.1;id" 1.1.1 dns.example; do
	: > "$LWJ_LOG"; out=$("$HIBR" "$H" --address 192.168.152.50/24 --via 192.168.152.254 --everything --dns "$d" lw7 1000 2>&1); rc=$?
	[ $rc = 2 ] && [ ! -s "$LWJ_LOG" ] && ok "refused before anything is run: --dns '$d'" || bad "dns '$d': rc=$rc $out"
done
: > "$LWJ_LOG"; out=$("$HIBR" "$H" --address 192.168.152.50/24 --dns 192.168.152.254 lw7 1000 2>&1); rc=$?
[ $rc = 2 ] && echo "$out" | grep -q "goes with --everything" && ok "names through the line only with everything through it" || bad "dns alone: rc=$rc $out"
mv "$T/bin/resolvectl" "$T/resolvectl.off"
OPATH=$PATH; PATH=$T/bin:/usr/bin:/bin
command -v resolvectl > /dev/null || {
	run --address 192.168.152.50/24 --via 192.168.152.254 --everything --dns 192.168.152.254 lw7 1000
	[ $rc = 0 ] && echo "$out" | grep -q "needs systemd-resolved" && ok "without systemd-resolved, it says names stay with the machine's own name server, and everything else stands" || bad "no resolved: rc=$rc $out"
}
PATH=$OPATH; mv "$T/resolvectl.off" "$T/bin/resolvectl"

[ $fail = 0 ] && echo "all jack checks passed"
exit $fail
