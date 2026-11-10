#!/usr/bin/env python3
"""The icons of the web app (web/app.json): the Red Dragon tile of the atlas on the felt of the table.

    python3 tools/make_app_icons.py            writes web/icons/icon-192.png and icon-512.png
    python3 tools/make_app_icons.py --check    exits 0 when the committed icons are what this script makes now

The tile is cut out of assets/tiles/tiles.png (run tools/make_tiles.py first after a change of the art) and scaled with a box filter so that
it fills the middle of the square: a launcher that masks the icon to a circle (purpose "any maskable") keeps the whole tile. Standard library
only; the PNG reader is the engine's (engine/tools/control/tests/png_reader.py).
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "engine" / "tools" / "control" / "tests"))
from png_reader import read_png  # noqa: E402

ATLAS = ROOT / "assets" / "tiles" / "tiles.png"
OUT_DIR = ROOT / "web" / "icons"
SIZES = (192, 512)
# The geometry of tools/make_tiles.py: cells of CELL_W x CELL_H, COLUMNS to a row, cell index = tile id.
CELL_W = 96
CELL_H = 128
COLUMNS = 8
RED_DRAGON = 31
# kFelt of src/Mahjong/Table/UiPalette.h.
FELT = (22, 92, 62)
# The tile's height as a part of the icon: inside the circle of a maskable icon (the safe zone is 80 % of the side).
TILE_SHARE = 0.72


def tile_pixels() -> list[list[tuple[int, int, int, int]]]:
    image = read_png(ATLAS.read_bytes())
    if image.channels != 4:
        raise SystemExit(f"{ATLAS}: expected RGBA, got {image.channels} channels")
    left = (RED_DRAGON % COLUMNS) * CELL_W
    top = (RED_DRAGON // COLUMNS) * CELL_H
    return [[image.pixel(left + x, top + y) for x in range(CELL_W)] for y in range(CELL_H)]


def render(size: int, tile: list[list[tuple[int, int, int, int]]]) -> bytes:
    """The icon as RGB rows: the tile scaled by a box filter over the felt, centred."""
    height = round(size * TILE_SHARE)
    width = round(height * CELL_W / CELL_H)
    left = (size - width) // 2
    top = (size - height) // 2
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            r, g, b = FELT
            tx, ty = x - left, y - top
            if 0 <= tx < width and 0 <= ty < height:
                x0, x1 = tx * CELL_W // width, max(tx * CELL_W // width + 1, (tx + 1) * CELL_W // width)
                y0, y1 = ty * CELL_H // height, max(ty * CELL_H // height + 1, (ty + 1) * CELL_H // height)
                sums = [0, 0, 0, 0]
                count = 0
                for sy in range(y0, y1):
                    for sx in range(x0, x1):
                        pr, pg, pb, pa = tile[sy][sx]
                        sums[0] += pr * pa
                        sums[1] += pg * pa
                        sums[2] += pb * pa
                        sums[3] += pa
                        count += 1
                alpha = sums[3] / (255 * count)
                if sums[3]:
                    r = round(sums[0] / sums[3] * alpha + r * (1 - alpha))
                    g = round(sums[1] / sums[3] * alpha + g * (1 - alpha))
                    b = round(sums[2] / sums[3] * alpha + b * (1 - alpha))
            row += bytes((r, g, b))
        rows.append(bytes(row))
    return b"".join(b"\x00" + row for row in rows)


def png(size: int, raw: bytes) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="compare the pixels of the committed icons, write nothing")
    args = parser.parse_args()
    tile = tile_pixels()
    stale = []
    for size in SIZES:
        raw = render(size, tile)
        path = OUT_DIR / f"icon-{size}.png"
        if args.check:
            if not path.exists():
                stale.append(path)
                continue
            image = read_png(path.read_bytes())
            if (image.width, image.height, image.channels) != (size, size, 3) or b"".join(b"\x00" + row for row in image.rows) != raw:
                stale.append(path)
        else:
            OUT_DIR.mkdir(parents=True, exist_ok=True)
            path.write_bytes(png(size, raw))
            print(f"wrote {path.relative_to(ROOT)}")
    for path in stale:
        print(f"stale: {path.relative_to(ROOT)} (run python3 tools/make_app_icons.py)")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
