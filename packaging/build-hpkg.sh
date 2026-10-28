#!/bin/sh
# Build gonx-<version>-1-x86_64.hpkg on a Haiku/Renku system from the built
# files in <build-dir> (Gonx, control_look/Gonx, gonx-theme).
#
#   sh packaging/build-hpkg.sh <build-dir> [<version>]
#
# The decorator uses app_server's private classes, so it only fits the system
# it was compiled against. The package therefore requires exactly the
# installed "haiku" package: pkgman then refuses a mismatched system instead
# of letting a stale decorator take app_server down at boot. Build it on (or
# from a sysroot of) the system it is for.
set -eu

build=${1:?usage: build-hpkg.sh <build-dir> [<version>]}
version=${2:-0.2}
here=$(cd "$(dirname "$0")" && pwd)

haiku=$(pkgman search -i -D haiku 2>/dev/null \
	| awk '$2 == "haiku" { print $3; exit }')
haiku=${haiku%-*}	# drop the package revision
[ -n "$haiku" ] || { echo "can't tell the installed haiku version" >&2; exit 1; }

work=$(mktemp -d /tmp/gonx-hpkg.XXXXXX)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/add-ons/decorators" "$work/add-ons/control_look" "$work/bin" \
	"$work/documentation/packages/gonx"
cp "$build/Gonx" "$work/add-ons/decorators/Gonx"
cp "$build/control_look/Gonx" "$work/add-ons/control_look/Gonx"
cp "$build/gonx-theme" "$work/bin/gonx-theme"
cp "$here/../README.md" "$here/../LICENSE" \
	"$work/documentation/packages/gonx/"

cat > "$work/.PackageInfo" <<EOF
name			gonx
version			$version-1
architecture	x86_64
summary			"The Gonx concept look: window decorator, controls and colours"
description		"A window decorator and colour scheme after the Gonx BeOS \
concept artwork (cotito.free.fr, c. 2001): a rounded yellow title tab hanging \
past the window edge, the close, zoom and minimize buttons on a raised shelf \
beside it, and a thick blue-grey frame; light controls with outlined \
arrows, blue check marks and rounded tabs. Run 'gonx-theme apply' to switch to \
it and 'gonx-theme revert' to go back. Built for $haiku only."
packager		"Gonx contributors"
vendor			"Gonx"
copyrights		"2026 Gonx contributors"
licenses		"MIT"
urls			"https://github.com/idumych/gonx"
provides {
	gonx = $version
	cmd:gonx_theme = $version
}
requires {
	haiku == $haiku
}
EOF

out="$PWD/gonx-$version-1-x86_64.hpkg"
(cd "$work" && package create -C . "$out")
echo "$out (for haiku $haiku)"
