#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build longwire's Debian package from this tree, for the machine it runs on.
#
#   packaging/deb/build-deb.sh [outdir]      default: build/pkg
#
# The package is the module (/usr/lib/hibr/longwire.so), the two launchers, the two services and the documentation. It
# is built with TCC like everything else here (`make`), installed into a staging directory with the same
# `make install` a source install uses, and packed with dpkg-deb; nothing here needs root. The two scripts are NOT in it:
# they come from hibr's catalogue, the one route for them (docs/adr/0028).
#
# The package depends on libc at the newest symbol version the module uses (read from it, not guessed) and on hibr at
# HIBR_MIN, the oldest release the test suite has been run against: a module is built for one hibr module ABI (16, the same
# for all of 0.99.74 to now), and hibr refuses one it cannot satisfy.

set -eu

SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$SRC/build/pkg}
HIBR_SRC=${HIBR_SRC:-$SRC/../hibr}
cd "$SRC"

VER=$(cat VERSION)
REV=${DEB_REVISION:-1}
ARCH=$(dpkg --print-architecture)
PKG=longwire_${VER}-${REV}_${ARCH}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
STAGE=$OUT/$PKG
HVER=$(sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' "$HIBR_SRC/include/hibr.h")
HMIN=$(cat HIBR_MIN)
HABI=$(sed -n 's/^#define HIBR_ABI \([0-9]*\)u*/\1/p' "$HIBR_SRC/include/hibr.h")

[ -n "$HVER" ] || { echo "build-deb: no HIBR_VER in $HIBR_SRC/include/hibr.h (set HIBR_SRC)" >&2; exit 1; }
echo "== building longwire $VER for $ARCH, against hibr $HVER (module ABI $HABI), needing hibr $HMIN or newer"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN"
make -s HIBR_SRC="$HIBR_SRC" > /dev/null
make -s HIBR_SRC="$HIBR_SRC" PREFIX=/usr DESTDIR="$STAGE" install > /dev/null

glibc=$(objdump -T "$STAGE/usr/lib/hibr/longwire.so" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
size=$(du -sk "$STAGE/usr" | cut -f1)

install -d "$STAGE/usr/share/doc/longwire"
install -m 644 COPYING "$STAGE/usr/share/doc/longwire/copyright"
gzip -9n < CHANGELOG.md > "$STAGE/usr/share/doc/longwire/changelog.gz"
rm -f "$STAGE/usr/share/doc/longwire/CHANGELOG.md" "$STAGE/usr/share/doc/longwire/COPYING"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: longwire
Version: $VER-$REV
Architecture: $ARCH
Maintainer: osakka <osakka@gmail.com>
Installed-Size: $size
Depends: libc6 (>= ${glibc:-2.34}), hibr (>= $HMIN)
Recommends: ca-certificates
Section: net
Priority: optional
Description: layer-2 mesh VPN engine for hibr, with post-quantum handshakes
 longwire joins sites, and single machines, into one Ethernet segment over
 the Internet. You dial a number; a tiny switchboard introduces two nodes,
 they punch through their NATs and talk directly, and the switchboard drops
 out of the data path. The handshake is hybrid X25519 and ML-KEM-768.
 .
 This package is the engine (a hibr module) and the launchers. The two
 scripts, longwire and longwire-switchboard, come from hibr's catalogue:
 \`catalogue install cli/longwire cli/longwire-switchboard\`.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
	echo "longwire: the scripts come from the catalogue now:"
	echo "longwire:   catalogue install cli/longwire cli/longwire-switchboard"
	echo "longwire: (add --system as root to install them for the whole machine and for the services)."
fi
EOF
chmod 755 "$STAGE/DEBIAN/postinst"
find "$STAGE" -type d -exec chmod 755 {} +

fakeroot dpkg-deb --build --root-owner-group "$STAGE" "$OUT/$PKG.deb" > /dev/null
rm -rf "$STAGE"
echo "== $OUT/$PKG.deb"
