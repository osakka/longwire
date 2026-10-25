#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The packages (ADR 0028): a deb, an rpm and a pacman package are built, hold the same files, say what they depend on, and
# the module in them loads and the launcher finds and runs a script. Needs dpkg-deb, rpmbuild and makepkg (skipped,
# and said so, where one is missing). Nothing here needs root.
set -u
cd "$(dirname "$0")/.."
HIBR=${HIBR:-../hibr/build/hibr}
VER=$(cat VERSION)
fails=0
check() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi; }
for t in dpkg-deb rpmbuild rpm rpm2cpio cpio makepkg bsdtar zstd fakeroot objdump; do
	command -v $t > /dev/null || { echo "skip: $t is not installed, so the packages cannot be built here"; exit 0; }
done

T=$(mktemp -d /tmp/lwpkg.XXXXXX)
trap 'rm -rf "$T"' EXIT
make -s packages > "$T/build.log" 2>&1 || { cat "$T/build.log"; echo "FAIL the packages build"; exit 1; }
DEB=build/pkg/longwire_${VER}-1_$(dpkg --print-architecture).deb
RPM=$(ls build/pkg/longwire-${VER}-1.*.rpm)
ARC=$(ls build/pkg/longwire-${VER}-1-*.pkg.tar.zst)
[ -f "$DEB" ] && [ -f "$RPM" ] && [ -f "$ARC" ]; check $? "a deb, an rpm and a pacman package are built"

mkdir "$T/deb" "$T/rpm" "$T/arch"
dpkg-deb -x "$DEB" "$T/deb"
(cd "$T/rpm" && rpm2cpio "$OLDPWD/$RPM" | cpio -idm --quiet 2> /dev/null)
bsdtar -xf "$ARC" -C "$T/arch" 2> /dev/null

# --- what is in them
for f in usr/bin/longwire usr/bin/longwire-switchboard usr/lib/hibr/longwire.so \
	 usr/lib/systemd/system/longwire-switchboard.service usr/lib/systemd/system/longwire@.service usr/lib/sysusers.d/longwire.conf; do
	[ -e "$T/deb/$f" ] && [ -e "$T/rpm/$f" ] && [ -e "$T/arch/$f" ]; check $? "all three hold $f"
done
! find "$T/deb" "$T/rpm" "$T/arch" -name '*.hibr' | grep -q .; check $? "none holds a script: those come from the catalogue"
for d in deb rpm arch; do (cd "$T/$d" && find usr/bin usr/lib -type f | sort | xargs sha256sum) > "$T/files.$d"; done
cmp -s "$T/files.deb" "$T/files.rpm" && cmp -s "$T/files.deb" "$T/files.arch"; check $? "the commands, module and services are byte-identical in all three, and nothing else is in them"
for d in deb rpm arch; do (cd "$T/$d" && find usr -type f -perm /022 | grep -v . ); done > "$T/writable"; [ ! -s "$T/writable" ]; check $? "nothing in any of them is writable by group or other"
a=$(sha256sum < "$T/deb/usr/lib/hibr/longwire.so"); b=$(sha256sum < "$T/rpm/usr/lib/hibr/longwire.so"); c=$(sha256sum < "$T/arch/usr/lib/hibr/longwire.so")
[ "$a" = "$b" ] && [ "$b" = "$c" ]; check $? "the module is the same file in all three"
cmp -s "$T/deb/usr/bin/longwire" packaging/launcher.sh && cmp -s "$T/deb/usr/bin/longwire-switchboard" packaging/launcher.sh; check $? "the two launchers are the launcher"

# --- what they say they need: the oldest hibr the suite has been run against (HIBR_MIN), and the version they carry
hv=$(cat HIBR_MIN)
HSRC=${HIBR_SRC:-../hibr}
dpkg-deb -f "$DEB" Depends | grep -q "hibr (>= $hv)"; check $? "the deb depends on hibr $hv or newer"
[ "$(printf "%s\n%s\n" "$hv" "$(sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' "$HSRC/include/hibr.h")" | sort -V | head -1)" = "$hv" ]; check $? "...and that is no newer than the hibr the module is built against"
rpm -qp --requires "$RPM" 2> /dev/null | grep -q "hibr >= $hv"; check $? "the rpm requires it too"
bsdtar -xOf "$ARC" .PKGINFO 2> /dev/null | grep -q "depend = hibr>=$hv"; check $? "and so does the pacman package"
! rpm -qp --provides "$RPM" 2> /dev/null | grep -q '\.so'; check $? "the rpm provides no library names (it is a module, not a library)"
strings -a "$T/deb/usr/lib/hibr/longwire.so" | grep -qx "$VER"; check $? "the module carries the version $VER"

# --- the module in the package loads and works
$HIBR -c "mod load $T/deb/usr/lib/hibr/longwire.so; lw key new $T/seed; lw key id $T/seed" > "$T/id" 2>&1
[ "$(wc -c < "$T/id")" -ge 64 ]; check $? "the module from the package loads and makes a key"

# --- the scripts say what the catalogue needs to know, and the version is the package's
for s in longwire longwire-switchboard; do
	grep -q "^app $s \".*\" $VER\$" scripts/$s.hibr && grep -q '^# modules: longwire$' scripts/$s.hibr; check $? "scripts/$s.hibr declares its app line at $VER and the module it needs"
done

# --- the launcher finds a script the way the catalogue places it, and says what to do when there is none
mkdir -p "$T/home/.local/share/hibr/cli" "$T/none"
printf '#!/usr/bin/env hibr\necho "ran with: $*"\n' > "$T/home/.local/share/hibr/cli/longwire.hibr"
out=$(HOME="$T/home" XDG_CONFIG_HOME="$T/none" HIBR_SYSDIR="$T/none" PATH="$(dirname "$(readlink -f "$HIBR")"):$PATH" sh "$T/deb/usr/bin/longwire" up --now 2>&1)
[ "$out" = "ran with: up --now" ]; check $? "the launcher runs the script the catalogue put in its cli folder: $out"
mkdir -p "$T/home3/.local/share/hibr/apps/Network" && cp "$T/home/.local/share/hibr/cli/longwire.hibr" "$T/home3/.local/share/hibr/apps/Network/longwire.hibr"
out=$(HOME="$T/home3" XDG_CONFIG_HOME="$T/none" HIBR_SYSDIR="$T/none" PATH="$(dirname "$(readlink -f "$HIBR")"):$PATH" sh "$T/deb/usr/bin/longwire" x 2>&1)
[ "$out" = "ran with: x" ]; check $? "...and one installed as an app, in a submenu folder, is found too"
out=$(HOME="$T/home" XDG_CONFIG_HOME="$T/none" XDG_DATA_HOME="$T/none" HIBR_SYSDIR="$T/none" PATH="$(dirname "$(readlink -f "$HIBR")"):$PATH" sh "$T/deb/usr/bin/longwire-switchboard" 2>&1); rc=$?
[ $rc -eq 127 ] && echo "$out" | grep -q "catalogue install cli/longwire-switchboard"; check $? "with no script it says how to get it, and exits 127"

# --- the real thing, end to end: the launcher, the real script, the module from the package
out=$(HOME="$T/home2" XDG_CONFIG_HOME="$T/none" LONGWIRE_SCRIPTS="$PWD/scripts" LONGWIRE_MOD="$T/deb/usr/lib/hibr/longwire.so" \
	PATH="$(dirname "$(readlink -f "$HIBR")"):$PATH" sh "$T/deb/usr/bin/longwire" id 2>&1)
[ ${#out} -eq 64 ]; check $? "longwire id through the launcher and the packaged module gives a key id"

# --- the group that may use the services' control sockets (ADR 0032): a sysusers.d file, and each package makes the group
grep -q '^g longwire' "$T/deb/usr/lib/sysusers.d/longwire.conf"; check $? "the package ships the group longwire as a sysusers.d file"
for u in longwire-switchboard.service longwire@.service; do
	grep -q '^Group=longwire' "$T/deb/usr/lib/systemd/system/$u" && grep -q '^RuntimeDirectoryMode=0750' "$T/deb/usr/lib/systemd/system/$u" && grep -q -- '--group longwire' "$T/deb/usr/lib/systemd/system/$u"; check $? "$u runs in the group longwire, with a runtime directory its members can reach, and gives it the control socket"
done
dpkg-deb -I "$DEB" postinst | grep -q 'groupadd -r longwire'; check $? "the deb makes the group when it is installed"
rpm -qp --scripts "$RPM" 2> /dev/null | grep -q 'groupadd -r longwire'; check $? "the rpm makes it too"
bsdtar -xOf "$ARC" .INSTALL 2> /dev/null | grep -q 'groupadd -r longwire'; check $? "and so does the pacman package"

if command -v systemd-analyze > /dev/null; then
	mkdir -p "$T/units"
	cp "$T/deb/usr/lib/systemd/system/"*.service "$T/units/"
	for u in longwire-switchboard.service longwire@.service; do
		systemd-analyze verify --man=no "$T/units/$u" 2> "$T/sd.log"; rc=$?
		! grep -qi "unknown\|invalid\|unit file .* is\|failed to parse\|bad " "$T/sd.log"; check $? "systemd reads $u without complaint (rc $rc)"
	done
	grep -q "AF_NETLINK" "$T/deb/usr/lib/systemd/system/longwire@.service" && grep -q "AF_NETLINK" "$T/deb/usr/lib/systemd/system/longwire-switchboard.service"; check $? "both services allow AF_NETLINK, which listing interface addresses needs"
	grep -q "StateDirectory=longwire/%i" "$T/deb/usr/lib/systemd/system/longwire@.service"; check $? "each node instance has a state directory of its own"
fi
[ $fails -eq 0 ] && echo "packages: all ok" || { echo "FAILED: $fails"; exit 1; }
