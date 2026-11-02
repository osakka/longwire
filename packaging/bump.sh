#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Set the release version everywhere it is stated, and nowhere else: VERSION, the `app` headers of the scripts, their own idea of their version, the
# manual pages' heading (with this month), the engine's LW_VERSION, and BUILD.md's example header. A version mentioned in prose ("since 0.5.21") is
# history and is never touched.   sh packaging/bump.sh 0.5.38
set -e
cd "$(dirname "$0")/.."
new=${1:?usage: sh packaging/bump.sh X.Y.Z}
case $new in *[!0-9.]* | "") echo "bump: '$new' is not a version" >&2; exit 2 ;; esac
old=$(cat VERSION)
month=$(LC_ALL=C date +'%B %Y')
printf '%s\n' "$new" > VERSION
for f in scripts/*.hibr apps/switchboard.hibr control-panel/longwire.hibr menubar/longwire.hibr control-strip/longwire.hibr docs/BUILD.md; do
	sed -i -e "s/^\(app [a-z][a-z0-9-]* \".*\" \)$old\$/\1$new/" \
	       -e "s/^\(SELF_VERSION=\${LONGWIRE_SELF_VERSION:-\)$old}/\1$new}/" \
	       -e "s/^\(LWP_SELF=\"\${LONGWIRE_SELF_VERSION:-\)$old}\"/\1$new}\"/" "$f"
done
for f in man/*.1 man/*.5 man/*.7; do
	sed -i "s/^\(\.TH [A-Z-]* [0-9] \)\"[A-Za-z]* [0-9]*\" \"longwire $old\"/\1\"$month\" \"longwire $new\"/" "$f"
done
sed -i "s/^#define LW_VERSION \"$old\"/#define LW_VERSION \"$new\"/" module/longwire.c
left=$(grep -l "$old" VERSION module/longwire.c man/*.1 man/*.5 man/*.7 2>/dev/null || true)
n=$(grep -c "\"$new\"\|$new\$\|longwire $new\|$new}" scripts/*.hibr apps/switchboard.hibr control-panel/longwire.hibr menubar/longwire.hibr control-strip/longwire.hibr module/longwire.c man/*.1 man/*.5 man/*.7 docs/BUILD.md | awk -F: '{s+=$2} END {print s}')
echo "bump: $old -> $new ($n places)${left:+; still $old in: $left}"
