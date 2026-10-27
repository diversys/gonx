#!/bin/sh
# Assemble dist/gonx-<version>.zip: install.sh / uninstall.sh, prebuilt
# files, the full source (with the private headers needed to build on the
# target system), README, licence and a screenshot.
#
#   sh packaging/zip/make-zip.sh [<version>]
#
# Run after `make`; the prebuilt files are taken from build/, and
# build/BUILD names the system they were built for.
set -eu

version=${1:-0.1}
root=$(cd "$(dirname "$0")/../.." && pwd)
name=gonx-$version
out=$root/dist/$name

for f in build/Gonx build/control_look/Gonx build/gonx-theme; do
	[ -f "$root/$f" ] || { echo "missing $f: run make first" >&2; exit 1; }
done

rm -rf "$out" "$root/dist/$name.zip"
mkdir -p "$out/prebuilt/control_look" "$out/source"

cp "$root/packaging/zip/install.sh" "$root/packaging/zip/uninstall.sh" \
	"$root/packaging/zip/INSTALL.txt" "$out/"
cp "$root/README.md" "$root/LICENSE" "$out/"
cp "$root/docs/screenshot.png" "$out/screenshot.png"

cp "$root/build/Gonx" "$root/build/gonx-theme" "$out/prebuilt/"
cp "$root/build/control_look/Gonx" "$out/prebuilt/control_look/"
# the haiku package version the prebuilt files were built against, written
# by tools/sync-sysroot.sh
cp "$root/build/BUILT_FOR" "$out/prebuilt/BUILT_FOR"

cp -R "$root/src" "$root/tools" "$root/packaging" "$root/build-headers" \
	"$root/Makefile" "$root/README.md" "$root/LICENSE" "$out/source/"

chmod +x "$out/install.sh" "$out/uninstall.sh"

(cd "$root/dist" && python3 -I - "$name" <<'EOF'
import os, sys, zipfile
name = sys.argv[1]
with zipfile.ZipFile(name + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
	for top, dirs, files in os.walk(name):
		dirs.sort()
		for f in sorted(files):
			path = os.path.join(top, f)
			info = zipfile.ZipInfo.from_file(path, path)
			with open(path, "rb") as data:
				z.writestr(info, data.read(), zipfile.ZIP_DEFLATED)
EOF
)
echo "$root/dist/$name.zip"
