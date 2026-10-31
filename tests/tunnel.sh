#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Everything through the line (docs/adr/0045), on the real kernel: the jack helper is run in a network namespace of its own (no root needed, nothing on
# this machine changes), on a stand-in jack (a veth pair: its carrier goes when its other end does, as a tap's goes when no node holds it). What is
# checked is what the kernel decides: where an application's packet goes, where a socket tied to the real device goes, that the machine's own
# networks stay its own, that IPv6 does not leak past the line, and that the machine has its own road back the moment the jack loses its carrier.
cd "$(dirname "$0")/.." || exit 1
HIBR=$(readlink -f "${HIBR:-../hibr/build/hibr}")
if ! unshare -rnm true 2> /dev/null; then
	echo "skip tunnel: no user and network namespaces here"
	exit 0
fi
T=$(mktemp -d /tmp/lwtun.XXXXXX) || exit 1
trap 'rm -rf "$T"' EXIT
export HIBR T
unshare -rnm sh -c '
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
mkdir -p "$T/sys/lw0"; export LONGWIRE_SYSNET=$T/sys		# the host'"'"'s /sys shows the host'"'"'s devices: say lw0 is there, so the helper does not make a tap
PATH=$PATH:/usr/sbin:/sbin
ip link set lo up
ip link add eth9 type veth peer name eth9p
ip link add lw0 type veth peer name lw0p
ip link set eth9p up; ip link set eth9 up; ip link set lw0 up
ip addr add 10.9.0.2/24 dev eth9
ip route add default via 10.9.0.1 dev eth9
ip -6 addr add 2001:db8:9::2/64 dev eth9 nodad
ip -6 route add default via 2001:db8:9::1 dev eth9
get() { ip route get "$@" 2> /dev/null | head -1; }
tied() { python3 -I -c "
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET,25,b\"eth9\"); s.connect((\"203.0.113.9\",9)); print(s.getsockname()[0])
"; }

out=$("$HIBR" packaging/longwire-jack --address 192.168.152.50/24 --via 192.168.152.254 --everything lw0 1000 2>&1); rc=$?
[ $rc = 0 ] && ok "the helper puts everything through the line on a jack with no node on it" || bad "helper: rc=$rc $out"
case $(get 203.0.113.9) in *"via 192.168.152.254 dev lw0"*) bad "with no node on the jack, everything must still go the machine'"'"'s own way: $(get 203.0.113.9)" ;; *"via 10.9.0.1 dev eth9"*) ok "with no node holding the jack, the machine keeps its own road" ;; *) bad "no carrier: $(get 203.0.113.9)" ;; esac
ip link set lw0p up; sleep 0.3					# a node takes the jack: carrier
case $(get 203.0.113.9) in *"via 192.168.152.254 dev lw0"*) ok "with a node on the jack, an application'"'"'s packet goes through the line" ;; *) bad "app: $(get 203.0.113.9)" ;; esac
case $(tied) in 10.9.0.2) ok "a socket tied to the real device (the engine'"'"'s own) goes the real way" ;; *) bad "tied: $(tied)" ;; esac
case $(get 10.9.0.77) in *"dev eth9"*) ok "the machine'"'"'s own network stays its own" ;; *) bad "lan: $(get 10.9.0.77)" ;; esac
case $(get 192.168.152.9) in *"dev lw0"*) ok "the line'"'"'s own network is reached directly" ;; *) bad "line: $(get 192.168.152.9)" ;; esac
case $(ip route show default) in *"via 10.9.0.1 dev eth9"*) ok "the machine'"'"'s own default route is untouched" ;; *) bad "default: $(ip route show default)" ;; esac
case $(ip -6 route get 2001:4860::8888 2> /dev/null | head -1) in *"dev lw0"*) ok "IPv6 goes into the line too: it does not leak past it" ;; *) bad "IPv6 leaks past the line: $(ip -6 route get 2001:4860::8888 2>&1 | head -1)" ;; esac
ip link set lw0p down; sleep 0.3					# the node stops or crashes
case $(get 203.0.113.9) in *"via 10.9.0.1 dev eth9"*) ok "the node gone, the machine is back on its own road at once" ;; *) bad "after: $(get 203.0.113.9)" ;; esac
case $(ip -6 route get 2001:4860::8888 2> /dev/null | head -1) in *"dev eth9"*) ok "and IPv6 too" ;; *) bad "v6 after: $(ip -6 route get 2001:4860::8888 2>&1 | head -1)" ;; esac
ip link set lw0p up; sleep 0.3
out=$("$HIBR" packaging/longwire-jack --address 192.168.152.50/24 --via 192.168.152.254 --everything lw0 1000 2>&1); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "has carrier" && ok "refused while the jack has carrier (a node on it, or a kernel that would not let go)" || bad "carrier refusal: rc=$rc $out"
out=$("$HIBR" packaging/longwire-jack --address 192.168.152.50/24 lw0 1000 2>&1); rc=$?
[ $rc = 0 ] && [ -z "$(ip rule show pref 7701)" ] && [ -z "$(ip -6 rule show pref 7701)" ] && [ -z "$(ip route show table 7700)" ] \
	&& ok "run without --everything, the helper takes its rules and its table away" || bad "off: rc=$rc $out $(ip rule show)"
case $(get 203.0.113.9) in *"dev eth9"*) ok "and everything goes the machine'"'"'s own way again" ;; *) bad "off route: $(get 203.0.113.9)" ;; esac
ip link set lw0p down; sleep 0.2
"$HIBR" packaging/longwire-jack --address 192.168.152.50/24 --via 192.168.152.254 --everything lw0 1000 > /dev/null 2>&1
"$HIBR" packaging/longwire-jack --address 192.168.152.50/24 --via 192.168.152.254 --everything lw0 1000 > /dev/null 2>&1
[ "$(ip rule show | grep -c "^770[01]:")" = 2 ] && ok "run twice, the rules are there once" || bad "twice: $(ip rule show)"
ip rule add pref 7701 lookup 9999
ip rule del pref 7701 lookup 7700
out=$("$HIBR" packaging/longwire-jack --address 192.168.152.50/24 --via 192.168.152.254 --everything lw0 1000 2>&1); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "something else already takes everything" && ok "a place in the rules held by something else is refused" || bad "collision: rc=$rc $out"
ip rule del pref 7701 lookup 9999
"$HIBR" packaging/longwire-jack --address 192.168.152.50/24 --via 192.168.152.254 --everything lw0 1000 > /dev/null 2>&1
echo 1000 > "$T/sys/lw0/owner"
out=$("$HIBR" packaging/longwire-jack --remove lw0 1000 2>&1); rc=$?
[ $rc = 0 ] && [ -z "$(ip rule show pref 7701)$(ip rule show pref 7700)$(ip -6 rule show pref 7701)" ] && ! ip link show lw0 > /dev/null 2>&1 \
	&& ok "taking the jack away takes the rules with it" || bad "remove: rc=$rc $out $(ip rule show)"
case $(get 203.0.113.9) in *"via 10.9.0.1 dev eth9"*) ok "and the machine is on its own road" ;; *) bad "after remove: $(get 203.0.113.9)" ;; esac
[ $fail = 0 ] && echo "all tunnel checks passed"
exit $fail
'
