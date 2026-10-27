#!/bin/sh
# Copy the libraries, headers and app_server of a running Renku system into
# build/sysroot, for cross-building add-ons that must match that system.
#
#   sh tools/sync-sysroot.sh <ssh-host>
#
# The host needs freetype_devel installed (pkgman install freetype_devel):
# app_server's font headers include FreeType's.
set -eu

host=${1:?usage: sync-sysroot.sh <ssh-host>}
root=$(cd "$(dirname "$0")/.." && pwd)/build/sysroot/boot/system

mkdir -p "$root/develop" "$root/servers"
chmod -R u+w "$root" 2>/dev/null || true

ssh "$host" 'test -f /boot/system/develop/headers/freetype2/ft2build.h' \
	|| { echo "$host: install freetype_devel first" >&2; exit 1; }

ssh "$host" 'cd /boot/system/develop && tar chf - lib headers' \
	| tar xf - -C "$root/develop"
# the C++ runtime lives with the system libraries, not under develop/
ssh "$host" 'cd /boot/system/lib && tar chf - libstdc++.so* libgcc_s.so*' \
	| tar xf - -C "$root/develop/lib"
scp -q "$host:/boot/system/servers/app_server" "$root/servers/"
chmod -R u+w "$root"

# the haiku package version, which prebuilt files will be marked as made for
ssh "$host" "pkgman search -i -D haiku | awk '\$2 == \"haiku\" { print \$3; exit }'" \
	| sed 's/-[0-9]*$//' > "$root/../../BUILT_FOR"
echo "sysroot from $host (haiku $(cat "$root/../../BUILT_FOR"))"
