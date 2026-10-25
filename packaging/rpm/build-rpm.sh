#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build longwire's RPM from the Debian package of the same release.
#
#   packaging/rpm/build-rpm.sh build/pkg/longwire_<ver>-1_amd64.deb [outdir]
#
# The RPM holds the deb's own files, byte for byte, so the packagings cannot differ in what they ship; nothing is stripped,
# recompressed or rebuilt. It needs hibr at the version the deb needs (HIBR_MIN).

set -eu

[ $# -ge 1 ] || { echo "usage: build-rpm.sh longwire_<ver>-1_amd64.deb [outdir]" >&2; exit 2; }
DEB=$(readlink -f "$1")
SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(readlink -f "${2:-$SRC/build/pkg}")
VER=$(dpkg-deb -f "$DEB" Version | sed 's/-[^-]*$//')
REL=${RPM_RELEASE:-1}
HVER=$(dpkg-deb -f "$DEB" Depends | sed -n 's/.*hibr (>= \([^)]*\)).*/\1/p')
case $(dpkg-deb -f "$DEB" Architecture) in
amd64) ARCH=x86_64 ;;
arm64) ARCH=aarch64 ;;
*) echo "build-rpm: no RPM name for $(dpkg-deb -f "$DEB" Architecture)" >&2; exit 1 ;;
esac
TOP=$(mktemp -d "${TMPDIR:-/tmp}/longwire-rpm-XXXXXX")
trap 'rm -rf "$TOP"' EXIT
STAGE=$TOP/stage

echo "== building longwire $VER-$REL for $ARCH, from $(basename "$DEB")"
dpkg-deb -x "$DEB" "$STAGE"
mv "$STAGE/usr/share/doc/longwire/copyright" "$STAGE/usr/share/doc/longwire/COPYING"

mkdir -p "$TOP/SPECS" "$TOP/RPMS"
cat > "$TOP/SPECS/longwire.spec" <<EOF
%global debug_package %{nil}
%global __os_install_post %{nil}
%global _build_id_links none
%global __provides_exclude_from ^/usr/lib/hibr/.*\$
%define _binary_payload w6.xzdio

Name:           longwire
Version:        $VER
Release:        $REL
Summary:        Layer-2 mesh VPN engine for hibr, with post-quantum handshakes
License:        GPL-3.0-or-later
BuildArch:      $ARCH
Requires:       hibr >= $HVER

%description
longwire joins sites, and single machines, into one Ethernet segment over the
Internet. You dial a number; a tiny switchboard introduces two nodes, they
punch through their NATs and talk directly, and the switchboard drops out of
the data path. The handshake is hybrid X25519 and ML-KEM-768.

This package is the engine (a hibr module) and the launchers. The two scripts,
longwire and longwire-switchboard, come from hibr's catalogue:
catalogue install cli/longwire cli/longwire-switchboard

%install
cp -a $STAGE/. %{buildroot}/

%pre
systemd-sysusers /usr/lib/sysusers.d/longwire.conf > /dev/null 2>&1 || getent group longwire > /dev/null || groupadd -r longwire

%post
echo "longwire: the scripts come from the catalogue now:"
echo "longwire:   catalogue install cli/longwire cli/longwire-switchboard"
echo "longwire: (add --system as root to install them for the whole machine and for the services)."

%files
%defattr(-,root,root,-)
/usr/bin/longwire
/usr/bin/longwire-switchboard
/usr/lib/hibr/longwire.so
/usr/lib/systemd/system/longwire-switchboard.service
/usr/lib/systemd/system/longwire@.service
/usr/lib/sysusers.d/longwire.conf
%dir /usr/share/doc/longwire
%license /usr/share/doc/longwire/COPYING
%doc /usr/share/doc/longwire/*.md
/usr/share/doc/longwire/changelog.gz
EOF

rpmbuild -bb --quiet --define "_topdir $TOP" --define "_rpmdir $TOP/RPMS" \
	--target "$ARCH" "$TOP/SPECS/longwire.spec" > "$TOP/log" 2>&1 ||
	{ cat "$TOP/log" >&2; exit 1; }
mkdir -p "$OUT"
rpm=$(find "$TOP/RPMS" -name '*.rpm' | head -1)
cp "$rpm" "$OUT/"
echo "== $OUT/$(basename "$rpm")"
