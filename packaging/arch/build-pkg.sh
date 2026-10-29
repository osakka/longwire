#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build longwire's Arch package from the Debian package of the same release.
#
#   packaging/arch/build-pkg.sh build/pkg/longwire_<ver>-1_amd64.deb [outdir]
#
# Like the RPM, the package holds the deb's own files, byte for byte. It is built here, on Debian, with makepkg and a
# configuration of its own: no Arch machine is needed to make one, only to install it.

set -eu

[ $# -ge 1 ] || { echo "usage: build-pkg.sh longwire_<ver>-1_amd64.deb [outdir]" >&2; exit 2; }
DEB=$(readlink -f "$1")
SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(readlink -f "${2:-$SRC/build/pkg}")
VER=$(dpkg-deb -f "$DEB" Version | sed 's/-[^-]*$//')
REL=${ARCH_PKGREL:-1}
HVER=$(dpkg-deb -f "$DEB" Depends | sed -n 's/.*hibr (>= \([^)]*\)).*/\1/p')
case $(dpkg-deb -f "$DEB" Architecture) in
amd64) CARCH=x86_64 ;;
arm64) CARCH=aarch64 ;;
*) echo "build-pkg: no Arch name for $(dpkg-deb -f "$DEB" Architecture)" >&2; exit 1 ;;
esac
TOP=$(mktemp -d "${TMPDIR:-/tmp}/longwire-arch-XXXXXX")
trap 'rm -rf "$TOP"' EXIT
STAGE=$TOP/stage

echo "== building longwire $VER-$REL for $CARCH, from $(basename "$DEB")"
dpkg-deb -x "$DEB" "$STAGE"
mkdir -p "$STAGE/usr/share/licenses/longwire"
mv "$STAGE/usr/share/doc/longwire/copyright" "$STAGE/usr/share/licenses/longwire/COPYING"

cat > "$TOP/longwire.install" <<'EOF'
post_install() {
	systemd-sysusers /usr/lib/sysusers.d/longwire.conf > /dev/null 2>&1 || getent group longwire > /dev/null || groupadd -r longwire
	echo "longwire: the scripts come from the catalogue now:"
	echo "longwire:   catalogue install cli/longwire cli/longwire-switchboard"
	echo "longwire: (add --system as root to install them for the whole machine and for the services)."
}
post_upgrade() { :; }
EOF
cat > "$TOP/makepkg.conf" <<EOF
CARCH="$CARCH"
CHOST="$CARCH-pc-linux-gnu"
BUILDENV=(!distcc !color !ccache !check !sign)
OPTIONS=(!strip !docs !libtool !staticlibs emptydirs zipman !purge !debug !lto)
INTEGRITY_CHECK=(sha256)
PKGEXT='.pkg.tar.zst'
SRCEXT='.src.tar.gz'
COMPRESSZST=(zstd -c -T0 -19 -)
PACKAGER="osakka <osakka@gmail.com>"
EOF
cat > "$TOP/PKGBUILD" <<EOF
pkgname=longwire
pkgver=$VER
pkgrel=$REL
pkgdesc='Layer-2 mesh VPN engine for hibr, with post-quantum handshakes'
arch=('$CARCH')
license=('GPL-3.0-or-later')
depends=('glibc' 'hibr>=$HVER')
optdepends=('polkit: the password step that makes the network device (pkexec)' 'iproute2: the network device and its address' 'dhcpcd: an address from a DHCP server on the line' 'acl: reach a site node you started without logging in again')
install=longwire.install
options=(!strip !debug)

package() {
	cp -a "$STAGE"/. "\$pkgdir"/
}
EOF

(cd "$TOP" && MAKEPKG_CONF="$TOP/makepkg.conf" PKGDEST="$TOP/out" \
	makepkg --nodeps --noconfirm --force --skipinteg > "$TOP/log" 2>&1) ||
	{ cat "$TOP/log" >&2; exit 1; }
mkdir -p "$OUT"
pkg=$(find "$TOP/out" -name '*.pkg.tar.zst' | head -1)
cp "$pkg" "$OUT/"
echo "== $OUT/$(basename "$pkg")"
