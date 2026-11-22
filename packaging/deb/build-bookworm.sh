#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build longwire's Debian package in Debian 12 (packaging/deb/Containerfile.bookworm), rootless, from the files git tracks in this tree and
# hibr's headers: the package then runs on Debian 12 and up, whatever the machine building it runs (BUG-14). The image and the build are both
# build steps with chroot isolation (rootless podman cannot start a container on every machine); the .deb is copied out with --output.
#
#   packaging/deb/build-bookworm.sh [outdir]      default: build/pkg
set -eu
SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$SRC/build/pkg}
HIBR_SRC=${HIBR_SRC:-$SRC/../hibr}
IMG=longwire-build-bookworm
REV=${DEB_REVISION:-1}
podman image exists "$IMG" 2> /dev/null ||
	podman build --isolation chroot -q -t "$IMG" -f "$SRC/packaging/deb/Containerfile.bookworm" "$SRC/packaging/deb" > /dev/null
CTX=$(mktemp -d)
trap 'rm -rf "$CTX"' EXIT
(cd "$SRC" && git ls-files -z | xargs -0 tar cf -) | (cd "$CTX" && tar xf -)	# what git tracks: nothing built, nothing stray
mkdir -p "$CTX/.hibr" && cp -r "$HIBR_SRC/include" "$CTX/.hibr/"			# hibr's headers only: its tree is read, never built here
cat > "$CTX/.Containerfile" <<CF
FROM localhost/$IMG
COPY . /src
WORKDIR /src
RUN rm -rf build && DEB_REVISION=$REV HIBR_SRC=/src/.hibr sh packaging/deb/build-deb.sh /out
FROM scratch
COPY --from=0 /out/*.deb /
CF
mkdir -p "$OUT"
podman build --isolation chroot -q -f "$CTX/.Containerfile" -o "type=local,dest=$OUT" "$CTX" > /dev/null
ls "$OUT"/longwire_*.deb
