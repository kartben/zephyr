#!/bin/sh
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Make the Kite Rush model the current model of an edgetx-cli simulator radio
# and install the KiteRush widget on its SD card.
#
# Usage: sim-setup.sh [RADIO_KEY]
#
# RADIO_KEY is the simulator cache directory name of the radio, for example
# jumper-t15 (default) or radiomaster-tx16s. The simulator must have been
# started once for that radio so that its settings exist; when edgetx-cli is
# in PATH this script does that itself.

set -eu

radio=${1:-jumper-t15}
here=$(cd "$(dirname "$0")" && pwd)

case "$(uname -s)" in
Darwin) cache="$HOME/Library/Caches" ;;
*) cache="${XDG_CACHE_HOME:-$HOME/.cache}" ;;
esac
base="$cache/edgetx-cli/simulator/$radio"
settings="$base/settings"
sdcard="$base/sdcard"

if [ ! -f "$settings/RADIO/radio.yml" ]; then
	if ! command -v edgetx-cli >/dev/null 2>&1; then
		echo "error: no settings in $settings; start the simulator once:" >&2
		echo "  edgetx-cli dev simulator --radio $radio" >&2
		exit 1
	fi
	echo "Creating default settings for $radio..."
	(cd "$base/.." 2>/dev/null || cd /; \
	 edgetx-cli dev simulator --radio "$radio" --headless --no-watch --timeout 8s \
		>/dev/null)
fi
if [ ! -f "$settings/RADIO/radio.yml" ]; then
	echo "error: $settings/RADIO/radio.yml not found" >&2
	exit 1
fi

# EdgeTX only lists model files named modelN.yml: reuse the file that already
# holds the Kite Rush model, or take the first free number.
model=""
for f in "$settings"/MODELS/model*.yml; do
	[ -f "$f" ] || continue
	if grep -q '^ *name: "KiteRush"' "$f"; then
		model=$(basename "$f")
		break
	fi
done
if [ -z "$model" ]; then
	n=1
	while [ -e "$settings/MODELS/model$n.yml" ]; do
		n=$((n + 1))
	done
	model="model$n.yml"
fi

cp "$here/MODELS/kiterush.yml" "$settings/MODELS/$model"

# Select it and use stick mode 2 (throttle on the left stick, steering on the
# right one). radio.yml carries a checksum, so flag the file as edited by hand.
radio_yml="$settings/RADIO/radio.yml"
tmp="$radio_yml.tmp"
has_flag=0
grep -q '^manuallyEdited:' "$radio_yml" && has_flag=1
has_mode=0
grep -q '^stickMode:' "$radio_yml" && has_mode=1
awk -v model="$model" -v has_flag="$has_flag" -v has_mode="$has_mode" '
	/^currModelFilename:/ { print "currModelFilename: \"" model "\""; next }
	/^manuallyEdited:/ { print "manuallyEdited: 1"; next }
	/^stickMode:/ { print "stickMode: 1"; next }
	/^checksum:/ {
		print
		if (!has_flag) print "manuallyEdited: 1"
		if (!has_mode) print "stickMode: 1"
		next
	}
	{ print }
' "$radio_yml" >"$tmp"
mv "$tmp" "$radio_yml"

mkdir -p "$sdcard/WIDGETS"
rm -rf "$sdcard/WIDGETS/KiteRush"
cp -R "$here/WIDGETS/KiteRush" "$sdcard/WIDGETS/KiteRush"

echo "Kite Rush model installed as $settings/MODELS/$model (current model)"
echo "Run the simulator from $here:"
echo "  edgetx-cli dev simulator --radio $radio"
