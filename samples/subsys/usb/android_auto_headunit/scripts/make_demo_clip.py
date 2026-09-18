#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Render a navigation animation and encode it the way a phone would.

The head unit sample decodes what a phone projects. Where there is no phone --
the browser emulator, or a board on a desk -- it plays this clip instead, so
the stream is built to be decoded by the same baseline decoder at a cost the
emulator can carry: no deblocking, one reference picture, full pixel motion
only, and a map that scrolls by a whole number of pixels per frame so every
macroblock of it is a plain copy.

Writes an Annex B elementary stream and the C header the sample embeds.
"""

import argparse
import pathlib
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

WIDTH = 800
HEIGHT = 480
FPS = 30
FRAMES = 120
# Pixels the map moves per frame. The pattern repeats over the whole loop, so
# the last frame leads back into the first one.
SCROLL = 2
TILE = FRAMES * SCROLL

MAP_BG = (21, 23, 28)
BLOCK = (29, 32, 38)
STREET = (44, 48, 56)
AVENUE = (51, 57, 65)
ROUTE = (66, 133, 244)
ROUTE_EDGE = (28, 78, 160)
CARD = (32, 35, 41)
TEXT = (232, 236, 242)
DIM = (150, 158, 170)

FONT_DIR = "/System/Library/Fonts/Supplemental"


def font(name, size):
    try:
        return ImageFont.truetype(f"{FONT_DIR}/{name}", size)
    except OSError:
        return ImageFont.load_default(size)


def build_world():
    """One screen plus one tile of map, so any crop of it is a valid frame."""
    world = Image.new("RGB", (WIDTH, HEIGHT + TILE), MAP_BG)
    d = ImageDraw.Draw(world)

    # Blocks between the streets, laid out per tile so the pattern repeats
    for base in range(0, HEIGHT + TILE + TILE, TILE):
        for bx, bw in ((0, 100), (150, 210), (460, 180), (690, 110)):
            for by, bh in ((8, 96), (128, 84)):
                d.rounded_rectangle([bx, base + by, bx + bw, base + by + bh], radius=6, fill=BLOCK)

    # Cross streets, also on the tile period
    for base in range(0, HEIGHT + TILE + TILE, TILE):
        for by in (112, 224):
            d.rectangle([0, base + by, WIDTH, base + by + 20], fill=STREET)

    # Side streets and the avenue the route follows, constant down the frame
    for sx in (118, 668):
        d.rectangle([sx, 0, sx + 16, HEIGHT + TILE], fill=STREET)
    d.rectangle([372, 0, 440, HEIGHT + TILE], fill=AVENUE)

    # The route itself, with a lane divider on the avenue
    d.line([(406, 0), (406, HEIGHT + TILE)], fill=ROUTE_EDGE, width=22)
    d.line([(406, 0), (406, HEIGHT + TILE)], fill=ROUTE, width=16)
    for base in range(0, HEIGHT + TILE + TILE, 40):
        d.rectangle([460, base, 462, base + 18], fill=(70, 76, 86))

    return world


def draw_chevron(d, cx, cy):
    """The car, pointing up the avenue."""
    d.polygon(
        [(cx, cy - 20), (cx + 15, cy + 16), (cx, cy + 7), (cx - 15, cy + 16)],
        fill=(255, 255, 255),
    )
    d.polygon(
        [(cx, cy - 14), (cx + 11, cy + 12), (cx, cy + 5), (cx - 11, cy + 12)],
        fill=ROUTE,
    )


def draw_turn_arrow(d, x, y):
    d.line([(x, y + 30), (x, y + 10)], fill=TEXT, width=7)
    d.line([(x - 3, y + 13), (x + 22, y + 13)], fill=TEXT, width=7)
    d.polygon([(x + 18, y), (x + 38, y + 13), (x + 18, y + 26)], fill=TEXT)


def draw_overlay(frame, index, fonts):
    d = ImageDraw.Draw(frame)

    # Turn card
    d.rounded_rectangle([16, 16, 312, 112], radius=14, fill=CARD)
    draw_turn_arrow(d, 44, 40)
    metres = 400 - index * 2
    d.text((104, 30), f"{metres // 10 * 10} m", font=fonts["big"], fill=TEXT)
    d.text((106, 74), "Zephyr Avenue", font=fonts["small"], fill=DIM)

    # Status bar along the bottom
    d.rectangle([0, HEIGHT - 56, WIDTH, HEIGHT], fill=(23, 25, 30))
    d.text((24, HEIGHT - 42), "12 min", font=fonts["mid"], fill=TEXT)
    d.text((124, HEIGHT - 38), "4.2 km", font=fonts["small"], fill=DIM)
    d.text((224, HEIGHT - 38), "arrive 14:32", font=fonts["small"], fill=DIM)
    d.ellipse([WIDTH - 86, HEIGHT - 48, WIDTH - 22, HEIGHT - 8], fill=CARD)
    d.text((WIDTH - 72, HEIGHT - 42), "50", font=fonts["mid"], fill=TEXT)

    draw_chevron(d, 406, 330)


def render(stream):
    world = build_world()
    fonts = {
        "big": font("Arial Bold.ttf", 34),
        "mid": font("Arial Bold.ttf", 22),
        "small": font("Arial.ttf", 18),
    }

    for i in range(FRAMES):
        top = TILE - (i * SCROLL) % TILE
        frame = world.crop((0, top, WIDTH, top + HEIGHT)).copy()
        draw_overlay(frame, i, fonts)
        stream.write(frame.tobytes())


def encode(raw_path, out_path, crf):
    """Baseline, one reference, no deblocking, full pixel motion only."""
    params = ":".join(
        [
            "no-deblock=1",
            "ref=1",
            f"keyint={FRAMES}",
            f"min-keyint={FRAMES}",
            "scenecut=0",
            "subme=0",
            "me=hex",
            "trellis=0",
            "aq-mode=0",
            "weightp=0",
        ]
    )
    subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-s",
            f"{WIDTH}x{HEIGHT}",
            "-r",
            str(FPS),
            "-i",
            str(raw_path),
            "-c:v",
            "libx264",
            "-profile:v",
            "baseline",
            "-preset",
            "veryslow",
            "-crf",
            str(crf),
            "-pix_fmt",
            "yuv420p",
            "-x264-params",
            params,
            "-bsf:v",
            "h264_mp4toannexb",
            "-f",
            "h264",
            str(out_path),
        ],
        check=True,
    )


def start_code(data, at):
    if at + 3 > len(data) or data[at] != 0 or data[at + 1] != 0:
        return 0
    if data[at + 2] == 1:
        return 3
    if at + 4 <= len(data) and data[at + 2] == 0 and data[at + 3] == 1:
        return 4
    return 0


def access_units(data):
    """Split as the sample does: a picture ends with the coded slice in it."""
    out = []
    start = 0
    at = 0
    while at < len(data):
        sc = start_code(data, at)
        if sc == 0:
            at += 1
            continue
        kind = data[at + sc] & 0x1F
        at += sc + 1
        if kind in (1, 5):
            while at < len(data) and start_code(data, at) == 0:
                at += 1
            out.append(at - start)
            start = at
    if start < len(data):
        out.append(len(data) - start)
    return out


def write_header(stream_path, header_path):
    data = stream_path.read_bytes()
    units = access_units(data)
    lines = []
    for off in range(0, len(data), 12):
        chunk = data[off : off + 12]
        lines.append("\t" + " ".join(f"0x{b:02x}," for b in chunk))

    header_path.write_text(
        "/*\n"
        " * Copyright The Zephyr Project Contributors\n"
        " * SPDX-License-Identifier: Apache-2.0\n"
        " */\n"
        "\n"
        "/*\n"
        f" * Generated by scripts/make_demo_clip.py: {WIDTH}x{HEIGHT}, {FPS} fps,\n"
        f" * {FRAMES} pictures, baseline H.264 with deblocking off and full pixel\n"
        " * motion only. Do not edit.\n"
        " */\n"
        "\n"
        "#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_DEMO_CLIP_H_\n"
        "#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_DEMO_CLIP_H_\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        f"#define AA_DEMO_CLIP_WIDTH  {WIDTH}\n"
        f"#define AA_DEMO_CLIP_HEIGHT {HEIGHT}\n"
        f"#define AA_DEMO_CLIP_FPS    {FPS}\n"
        "\n"
        "/*\n"
        " * The largest picture in the clip, which is the key frame it opens with.\n"
        " * The decoder rewrites the bytes it is given, so a picture is copied out\n"
        " * of here before it is decoded.\n"
        " */\n"
        f"#define AA_DEMO_CLIP_MAX_AU {max(units)}\n"
        "\n"
        "static const uint8_t aa_demo_clip[] = {\n" + "\n".join(lines) + "\n};\n"
        "\n"
        "#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_DEMO_CLIP_H_ */\n"
    )
    return len(data), units


def main():
    ap = argparse.ArgumentParser(allow_abbrev=False)
    ap.add_argument("--out", default="aa_demo_clip.h")
    ap.add_argument("--stream", default="demo.h264")
    ap.add_argument("--crf", type=int, default=28)
    ap.add_argument("--preview", default=None)
    args = ap.parse_args()

    raw = pathlib.Path("demo.rgb")
    with raw.open("wb") as f:
        render(f)

    stream = pathlib.Path(args.stream)
    encode(raw, stream, args.crf)
    raw.unlink()

    size, units = write_header(stream, pathlib.Path(args.out))
    print(
        f"{size} bytes of H.264 in {len(units)} pictures, "
        f"{size / len(units):.0f} per picture, largest {max(units)}"
    )

    if args.preview:
        subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-i",
                str(stream),
                "-frames:v",
                "1",
                args.preview,
            ],
            check=True,
        )


if __name__ == "__main__":
    sys.exit(main())
