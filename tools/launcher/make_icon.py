#!/usr/bin/env python3
"""Generate the launcher icons.

The icon is drawn here rather than captured, so that its provenance is
self-evident: it contains no artwork from the game. Regenerate with

    python3 tools/launcher/make_icon.py

This writes the desktop launcher icon and the Android mipmaps. The Android
app previously shipped SDL's own logo, straight out of the project template
-- third-party branding standing in for the app's identity, and an image the
no-game-data audit had to treat as suspect because nothing said where it came
from. Generating them here answers that for every size at once.

Design: a dark rounded tile with a light ring, cut by a diagonal, over a
GameCube-purple field -- an abstract mark for "a port of a disc-based game",
using no trademarked shape, logo, or character.
"""

import math
import struct
import zlib
from pathlib import Path

SIZE = 256
OUT = Path(__file__).resolve().parent / "melee-pc.png"

# Android launcher densities, in the res/ directory each one belongs to.
ANDROID_ROOT = (Path(__file__).resolve().parents[2] / "tools" / "android" /
                "app" / "app" / "src" / "main" / "res")
ANDROID_SIZES = {
    "mipmap-mdpi": 48,
    "mipmap-hdpi": 72,
    "mipmap-xhdpi": 96,
    "mipmap-xxhdpi": 144,
    "mipmap-xxxhdpi": 192,
}

BG = (0x14, 0x16, 0x22)
TILE = (0x23, 0x1E, 0x3A)
RING = (0xE8, 0xE6, 0xF2)
ACCENT = (0x6C, 0x4C, 0xD6)


def blend(dst, src, a):
    return tuple(round(d + (s - d) * a) for d, s in zip(dst, src))


def coverage(d, edge, soft=1.2):
    """Antialiased inside-ness for a signed distance (negative = inside)."""
    return max(0.0, min(1.0, (edge - d) / soft + 0.5))


def rounded_rect(x, y, half, radius):
    dx = abs(x) - (half - radius)
    dy = abs(y) - (half - radius)
    dx = max(dx, 0.0)
    dy = max(dy, 0.0)
    return math.hypot(dx, dy) - radius


def build(size=SIZE):
    """Render at `size`. All geometry below is expressed in the 256 design
    and scaled, so build(256) is bit-for-bit what it always produced (the
    scale factor is exactly 1.0 there)."""
    k = size / 256.0
    rows = []
    c = (size - 1) / 2.0
    for py in range(size):
        row = bytearray()
        for px in range(size):
            x, y = px - c, py - c
            col = BG

            # tile
            d = rounded_rect(x, y, 112.0 * k, 34.0 * k)
            col = blend(col, TILE, coverage(d, 0.0, 1.2 * k))

            # accent field in the lower-right, clipped to the tile
            diag = (x + y) / math.sqrt(2.0)
            field = (coverage(diag, 18.0 * k, 1.2 * k) *
                     coverage(d, 0.0, 1.2 * k))
            col = blend(col, ACCENT, field * 0.85)

            # ring
            r = math.hypot(x, y)
            ring = coverage(abs(r - 62.0 * k) - 9.0 * k, 0.0, 1.2 * k)
            col = blend(col, RING, ring)

            # the diagonal cuts the ring, reading as a disc with a slice out
            cut = coverage(abs(diag - 4.0 * k) - 9.0 * k, 0.0, 1.2 * k)
            col = blend(col, TILE, ring * cut)

            row += bytes(col)
        rows.append(bytes(row))

    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(
            ">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    return png


if __name__ == "__main__":
    OUT.write_bytes(build())
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")
    for folder, px in ANDROID_SIZES.items():
        out = ANDROID_ROOT / folder / "ic_launcher.png"
        if not out.parent.is_dir():
            print(f"skipped {out} (no such directory)")
            continue
        out.write_bytes(build(px))
        print(f"wrote {out} ({px}x{px}, {out.stat().st_size} bytes)")
