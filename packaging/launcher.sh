#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The launcher: /usr/bin/longwire and /usr/bin/longwire-switchboard are this file, twice. The package ships the module;
# the two scripts come from hibr's catalogue (`catalogue install longwire longwire-switchboard`), one route for them and no
# second copy to keep in step. This finds the script the catalogue placed, yours first, then the machine's, and runs it.
#
# LONGWIRE_SCRIPTS (a directory) goes first when set: for a test, or a checkout.

name=${0##*/}
want=$name.hibr

find_script() {
	for d in "${LONGWIRE_SCRIPTS:-}" \
		 "${XDG_CONFIG_HOME:-$HOME/.config}/hibr/apps" \
		 "${XDG_DATA_HOME:-$HOME/.local/share}/hibr/apps" \
		 "${HIBR_SYSDIR:-/var/lib/hibr}/apps" \
		 /usr/share/hibr/apps; do
		[ -n "$d" ] || continue
		for f in "$d/$want" "$d"/*/"$want"; do		# the catalogue may file an app in a submenu folder
			[ -f "$f" ] && { echo "$f"; return 0; }
		done
	done
	return 1
}

script=$(find_script) || {
	echo "$name: its script is not installed. Fetch it from the catalogue:" >&2
	echo "$name:     catalogue install $name        (for this user)" >&2
	echo "$name:     sudo catalogue --system install $name        (for the machine, and for the services)" >&2
	exit 127
}
exec hibr "$script" "$@"
