#!/bin/sh
# Install the Gonx look for this user, without a package: the window
# decorator, the control look and the gonx-theme command go into
# ~/config/non-packaged, then the look is switched on.
#
#   sh install.sh             build against this system if a compiler is
#                             installed, otherwise use the prebuilt files
#   sh install.sh --build     always build here (needs gcc, make, haiku_devel)
#   sh install.sh --prebuilt  always use the prebuilt files
#
# The decorator runs inside app_server and uses its private classes, so it
# is safest built on the system it runs on; this script does that whenever
# it can. sh uninstall.sh removes everything again.
set -e

here=$(cd "$(dirname "$0")" && pwd)

mode=auto
case "${1:-}" in
	"") ;;
	--build) mode=build ;;
	--prebuilt) mode=prebuilt ;;
	*) echo "usage: sh install.sh [--build | --prebuilt]" >&2; exit 2 ;;
esac

if [ "$(uname -s)" != Haiku ]; then
	echo "This installs on Renku OS or Haiku." >&2
	exit 1
fi

if pkgman search -i gonx 2>/dev/null | awk '$2 == "gonx"' | grep -q .; then
	echo "The gonx package is installed. Remove it first:" >&2
	echo "    gonx-theme revert; pkgman uninstall gonx" >&2
	exit 1
fi

system=$(pkgman search -i -D haiku 2>/dev/null \
	| awk '$2 == "haiku" { print $3; exit }')
system=${system%-*}
builtFor=$(cat "$here/prebuilt/BUILT_FOR" 2>/dev/null || echo unknown)

canBuild() {
	command -v gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 \
		&& command -v rc >/dev/null 2>&1 && command -v xres >/dev/null 2>&1 \
		&& [ -f /boot/system/develop/headers/os/interface/View.h ] \
		&& [ -f /boot/system/develop/lib/libbe.so ]
}

if [ $mode = auto ]; then
	if canBuild; then mode=build; else mode=prebuilt; fi
fi

case $mode in
	build)
		if ! canBuild; then
			echo "Building needs a compiler and the system headers:" >&2
			echo "    pkgman install gcc make haiku_devel" >&2
			exit 1
		fi
		echo "Building Gonx against this system ($system)..."
		work=$(mktemp -d /tmp/gonx-build.XXXXXX)
		cp -R "$here/source/." "$work/"
		(cd "$work" && make clean >/dev/null && make >"$work/build.log" 2>&1) \
			|| { echo "The build failed; see $work/build.log" >&2; exit 1; }
		files=$work/build
		;;
	prebuilt)
		files=$here/prebuilt
		if [ "$system" != "$builtFor" ]; then
			echo "Note: the prebuilt files were made on $builtFor; this system"
			echo "is ${system:-unknown}. The interfaces they use haven't changed"
			echo "since Haiku R1/beta6, but building here is safer:"
			echo "    pkgman install gcc make haiku_devel && sh install.sh --build"
		fi
		;;
esac

addons=$(finddir B_USER_NONPACKAGED_ADDONS_DIRECTORY)
bin=$(finddir B_USER_NONPACKAGED_BIN_DIRECTORY)
mkdir -p "$addons/decorators" "$addons/control_look" "$bin"

# A decorator in use is mapped by app_server, and the control look by every
# running application: never overwrite them in place, put new files in
# place by renaming.
place() {
	cp "$1" "$2.new"
	mv -f "$2.new" "$2"
}

if setdecor -l 2>/dev/null | grep -q '^\*Gonx'; then
	setdecor Default >/dev/null
fi
place "$files/Gonx" "$addons/decorators/Gonx"
place "$files/control_look/Gonx" "$addons/control_look/Gonx"
place "$files/gonx-theme" "$bin/gonx-theme"

appServer() {
	ps | awk '$1 ~ /\/app_server$/ { print $(NF-3); exit }'
}

before=$(appServer)
"$bin/gonx-theme" apply
sleep 2
after=$(appServer)

if [ -z "$after" ] || [ "$before" != "$after" ]; then
	echo "app_server restarted after switching to Gonx; switching back." >&2
	"$bin/gonx-theme" revert || setdecor Default
	exit 1
fi

cat <<EOF

Gonx is installed in ~/config/non-packaged and switched on.
Restart running applications (or log out and in) to get the new controls
and fonts everywhere.

    gonx-theme revert      go back to your previous look
    sh uninstall.sh        remove Gonx

If the desktop ever fails to start after a system update: hold Space while
booting, open "Select safe mode options" and choose "Disable user add-ons";
then run sh uninstall.sh, or reinstall with sh install.sh --build.
EOF
