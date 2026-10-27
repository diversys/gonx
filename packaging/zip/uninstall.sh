#!/bin/sh
# Remove the Gonx look installed by install.sh: put the previous look back,
# then delete the decorator, the control look and gonx-theme from
# ~/config/non-packaged.

addons=$(finddir B_USER_NONPACKAGED_ADDONS_DIRECTORY)
bin=$(finddir B_USER_NONPACKAGED_BIN_DIRECTORY)

if [ -x "$bin/gonx-theme" ]; then
	"$bin/gonx-theme" revert || true
fi
if setdecor -l 2>/dev/null | grep -q '^\*Gonx'; then
	setdecor Default >/dev/null
fi

rm -f "$addons/decorators/Gonx" "$addons/control_look/Gonx" \
	"$bin/gonx-theme"

echo "Gonx is removed. Restart running applications to get your previous"
echo "controls and fonts back everywhere."
