#!/bin/sh
# Claude Code hook -> Agent Pad bridge.
#
# Usage (from a Claude Code hook): claude-pad.sh <thinking|input|done|idle|clear>
# Reads the hook JSON on stdin, assigns a stable slot (1-6) per session and
# forwards the status to the pad over its CDC-ACM serial port.

set -eu

status="${1:?usage: claude-pad.sh <thinking|input|done|idle|clear>}"
json="$(cat)"

port="${CLAUDE_PAD_PORT:-}"
if [ -z "$port" ]; then
	for p in /dev/tty.usbmodem* /dev/ttyACM*; do
		[ -e "$p" ] && port="$p" && break
	done
fi
[ -n "$port" ] || exit 0

field() {
	printf '%s' "$json" | sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" | head -n1
}

session="$(field session_id)"
[ -n "$session" ] || exit 0

dir="${TMPDIR:-/tmp}/claude-pad"
mkdir -p "$dir"

slot=""
for i in 1 2 3 4 5 6; do
	[ -f "$dir/$i" ] && [ "$(cat "$dir/$i")" = "$session" ] && slot="$i" && break
done
if [ -z "$slot" ]; then
	for i in 1 2 3 4 5 6; do
		[ -f "$dir/$i" ] && continue
		slot="$i"
		printf '%s' "$session" > "$dir/$i"
		break
	done
fi
[ -n "$slot" ] || exit 0

if command -v stty >/dev/null 2>&1; then
	stty -f "$port" raw -echo 2>/dev/null || stty -F "$port" raw -echo 2>/dev/null || true
fi

if [ "$status" = "clear" ]; then
	rm -f "$dir/$slot"
	printf 'clear %s\n' "$slot" > "$port"
else
	label="$(basename "$(field cwd)")"
	printf 'agent %s %s %s\n' "$slot" "$status" "$label" > "$port"
fi
