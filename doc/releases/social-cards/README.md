# Zephyr 4.4 Social Cards

This directory contains a small SVG-first tool for previewing and exporting the Zephyr 4.4 countdown cards.

The same generator is used for:

- the browser preview in `preview.html`
- standalone SVG export
- PNG export through `rsvg-convert`

## Requirements

- `node`
- `rsvg-convert`

The Clear Sans fonts used by the cards are vendored in `fonts/`, so exports do not depend on local font installation.

## Quick Start

Preview the cards in a browser:

```sh
cd doc/releases/social-cards
python3 -m http.server 8000
```

Then open:

```text
http://localhost:8000/preview.html
```

`file://` preview may fail because browsers often block local `fetch()` requests.

Export all cards to SVG and PNG:

```sh
cd doc/releases/social-cards
./export.sh
```

Export only selected cards by slug:

```sh
cd doc/releases/social-cards
./export.sh wireguard-vpn html-dashboard
```

SVG-only export is also available:

```sh
cd doc/releases/social-cards
node app.js export-svg wireguard-vpn html-dashboard
```

Generated files are written to:

- `out/svg/`
- `out/png/`

These outputs are ignored by git.

## Files

- `preview.html`: local preview page
- `styles.css`: preview-page styling
- `app.js`: shared SVG renderer and CLI export entrypoint
- `data/zephyr-4.4.json`: card copy and per-card metadata
- `icons/`: icon assets
- `fonts/`: vendored Clear Sans Regular and Bold
- `export.sh`: SVG + PNG export wrapper

## Editing Copy

Card content lives in `data/zephyr-4.4.json`.

Each card currently uses this shape:

```json
{
  "slug": "wireguard-vpn",
  "countdown": "T-11",
  "title": "WireGuard VPN",
  "tagline": "Adds WireGuard-based VPN tunneling to the networking stack.",
  "linkedin_copy": "WireGuard support is part of what’s coming in Zephyr 4.4.\n\nThis brings a modern VPN option to the networking stack, with a sample in tree to make it easier to understand how it fits into a Zephyr-based system.\n\n#ZephyrRTOS",
  "accent": "blue",
  "icon": "wireguard-vpn",
  "source_ref": [
    "doc/releases/release-notes-4.4.rst",
    "samples/net/wireguard/README.rst"
  ]
}
```

Guidelines used by the current set:

- keep titles short and technical
- keep taglines factual and compact
- keep LinkedIn copy casual, didactic, and grounded in the actual feature
- avoid slogans and promo language
- do not overdo hashtags
- keep `slug` stable because export filtering uses it

## Editing Design

Most card rendering happens in `app.js`.

The main shared template controls:

- canvas size
- card chrome
- logo placement
- text layout
- right-panel illustration area

Most cards use one SVG icon from `icons/`. The HTML dashboard card is a custom vector illustration rendered directly from `app.js`, based on the dashboard UI rather than the generic fallback icon.

## Notes on Assets

- Zephyr branding uses `doc/_static/images/logo.svg`
- Most icons are sourced from Phosphor and kept locally under `icons/`
- `icons/README.md` documents the icon mapping

## Typical Workflow

1. Edit copy in `data/zephyr-4.4.json`
2. Edit layout or illustration code in `app.js` if needed
3. Refresh `preview.html` in the browser
4. Run `./export.sh`
5. Review `out/png/` for clipping, wrapping, and spacing

## Troubleshooting

If preview fails in the browser:

- make sure you are serving the directory over `http://localhost`
- check the status panel in `preview.html`

If export fails:

- confirm `node` is installed
- confirm `rsvg-convert` is available in `PATH`
- check that the requested slugs exist in `data/zephyr-4.4.json`
