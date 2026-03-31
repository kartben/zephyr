#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$ROOT_DIR/out"
SVG_DIR="$OUT_DIR/svg"
PNG_DIR="$OUT_DIR/png"
CACHE_DIR="$OUT_DIR/font-cache"

mkdir -p "$SVG_DIR" "$PNG_DIR" "$CACHE_DIR"

if ! command -v node >/dev/null 2>&1; then
  echo "error: node is required" >&2
  exit 1
fi

if ! command -v rsvg-convert >/dev/null 2>&1; then
  echo "error: rsvg-convert is required" >&2
  exit 1
fi

SYSTEM_FONTCONF=""
for candidate in /etc/fonts/fonts.conf /opt/homebrew/etc/fonts/fonts.conf /usr/local/etc/fonts/fonts.conf; do
  if [[ -f "$candidate" ]]; then
    SYSTEM_FONTCONF="$candidate"
    break
  fi
done

FONTCONF_FILE="$(mktemp "${TMPDIR:-/tmp}/zephyr-social-fonts.XXXXXX")"
cleanup() {
  rm -f "$FONTCONF_FILE"
}
trap cleanup EXIT

cat >"$FONTCONF_FILE" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>${ROOT_DIR}/fonts</dir>
  <cachedir>${CACHE_DIR}</cachedir>
EOF

if [[ -n "$SYSTEM_FONTCONF" ]]; then
  cat >>"$FONTCONF_FILE" <<EOF
  <include ignore_missing="yes">${SYSTEM_FONTCONF}</include>
EOF
fi

cat >>"$FONTCONF_FILE" <<EOF
</fontconfig>
EOF

SVG_PATHS=()
while IFS= read -r line; do
  [[ -n "$line" ]] && SVG_PATHS+=("$line")
done < <(FONTCONFIG_FILE="$FONTCONF_FILE" node "$ROOT_DIR/app.js" export-svg "$@")

if [[ ${#SVG_PATHS[@]} -eq 0 ]]; then
  echo "No cards were exported." >&2
  exit 1
fi

for svg_path in "${SVG_PATHS[@]}"; do
  png_path="$PNG_DIR/$(basename "${svg_path%.svg}.png")"
  FONTCONFIG_FILE="$FONTCONF_FILE" \
    rsvg-convert \
    --width 1200 \
    --height 675 \
    --format png \
    --output "$png_path" \
    "$svg_path"
  printf 'exported %s\n' "$png_path"
done
