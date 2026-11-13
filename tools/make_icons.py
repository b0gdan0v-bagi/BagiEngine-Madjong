#!/usr/bin/env python3
"""The icons of the Mahjong UI: round-button pictures drawn by this script and written as one atlas PNG.

Like the tiles (tools/make_tiles.py, whose Canvas and PNG encoder are imported here, not copied) the art is ours: lines, circles, rings,
rectangles and polygons, no third-party picture and no font file. Every icon is white on transparent, so the scene's `tint` colours it.

    python3 tools/make_icons.py            writes assets/icons/icons.png
    python3 tools/make_icons.py --check    exits 0 when the committed PNG and config/TextureLibrary.xml are what the generators make now
    python3 tools/make_icons.py --preview OUT.png   writes a contact sheet at 24, 56 and 112 pixels (OUT outside the repository)

Atlas: COLUMNS x ROWS cells of CELL x CELL pixels, RGBA. The cell of ICON_NAMES[i] is cell i (row by row); the sprite of a cell is named
`Icon_<name>` and is in config/TextureLibrary.xml, which tools/make_tiles.py writes (it takes the sprites from `sprite_entries()` here; that
file is never edited by hand). An icon keeps ICON_MARGIN pixels clear of the cell's edge so that a tinted icon never touches the edge of a
button's picture box.

The check compares the decoded pixels, not the bytes of the PNG (zlib output differs between versions).
"""

import argparse
import math
import struct
import sys
import zlib
from pathlib import Path
from typing import Callable, Optional

import make_tiles
from make_tiles import Canvas, Color

CELL = 64
COLUMNS = 4
ROWS = 2
ATLAS_W = CELL * COLUMNS
ATLAS_H = CELL * ROWS
SUPERSAMPLE = 4
ICON_NAMES = ("Rules", "Options", "NewGame", "Leave", "Close", "Back", "Next", "Hint")
CELL_COUNT = len(ICON_NAMES)
SPRITE_PREFIX = "Icon_"

ROOT = Path(__file__).resolve().parent.parent
PNG_PATH = ROOT / "assets" / "icons" / "icons.png"
TEXTURE_PATH = "assets/icons/icons.png"

WHITE: Color = (255, 255, 255)
ICON_MARGIN = 4  # pixels of the cell's edge that stay transparent
MIN_OPAQUE_PIXELS = 150  # an icon is a solid shape, not a few stray pixels

# ----- geometry (cell pixels) -----

CENTER = CELL / 2
STROKE = 6.0  # the thickness of a drawn line
THIN_STROKE = 4.0
GEAR_TEETH = 8
GEAR_BODY_RADIUS = 18.0
GEAR_HOLE_RADIUS = 8.0
GEAR_TOOTH_INNER = 15.0  # a tooth starts inside the body so the two merge
GEAR_TOOTH_OUTER = 25.0
GEAR_TOOTH_BASE = 5.0  # half the width of a tooth at its root
GEAR_TOOTH_TIP = 3.4
TURN_RADIUS = 17.0
TURN_WIDTH = 6.0
TURN_START = math.radians(-20.0)  # the arc runs clockwise (the picture's y points down) from here ...
TURN_END = math.radians(280.0)  # ... to here (the top), the arrowhead takes the gap
TURN_HEAD_LENGTH = 9.0
TURN_HEAD_HALF = 8.0


class IconCanvas(Canvas):
    """The Canvas of the tiles at the size of an icon cell: the shapes, the view and the fill are inherited."""

    def __init__(self) -> None:  # the tile canvas is sized in its __init__ from the tile cell, so it is not called
        self.scale = SUPERSAMPLE
        self.width = CELL * self.scale
        self.height = CELL * self.scale
        self.pixels = bytearray(self.width * self.height * 4)
        self.view = (1.0, 0.0, 0.0)

    def downsample(self) -> bytearray:  # type: ignore[override]
        """The CELL x CELL RGBA picture: each output pixel is the mean of its block, the colours weighted by alpha."""
        scale = self.scale
        out = bytearray(CELL * CELL * 4)
        area = scale * scale
        for y in range(CELL):
            for x in range(CELL):
                red = green = blue = alpha = 0
                for sy in range(scale):
                    start = ((y * scale + sy) * self.width + x * scale) * 4
                    for sx in range(scale):
                        a = self.pixels[start + sx * 4 + 3]
                        red += self.pixels[start + sx * 4] * a
                        green += self.pixels[start + sx * 4 + 1] * a
                        blue += self.pixels[start + sx * 4 + 2] * a
                        alpha += a
                target = (y * CELL + x) * 4
                if alpha:
                    out[target] = (red + alpha // 2) // alpha
                    out[target + 1] = (green + alpha // 2) // alpha
                    out[target + 2] = (blue + alpha // 2) // alpha
                    out[target + 3] = (alpha + area // 2) // area
        return out


def cell_origin(index: int) -> tuple[int, int]:
    return (index % COLUMNS) * CELL, (index // COLUMNS) * CELL


def sprite_name(index: int) -> str:
    return SPRITE_PREFIX + ICON_NAMES[index]


def sprite_entries() -> list[tuple[str, str, int, int, int, int]]:
    """What config/TextureLibrary.xml holds for the icons: (sprite name, texture path, x, y, w, h), in the order of the cells."""
    return [(sprite_name(index), TEXTURE_PATH, *cell_origin(index), CELL, CELL) for index in range(CELL_COUNT)]


# ----- the pictures -----

def draw_close(canvas: Canvas) -> None:
    """A cross."""
    low, high = 17.0, CELL - 17.0
    canvas.line(low, low, high, high, STROKE, WHITE)
    canvas.line(low, high, high, low, STROKE, WHITE)


def draw_back(canvas: Canvas) -> None:
    """An arrow to the left."""
    canvas.line(46.0, CENTER, 17.0, CENTER, STROKE, WHITE)
    canvas.line(17.0, CENTER, 30.0, 19.0, STROKE, WHITE)
    canvas.line(17.0, CENTER, 30.0, CELL - 19.0, STROKE, WHITE)


def draw_next(canvas: Canvas) -> None:
    """An arrow to the right."""
    canvas.line(CELL - 46.0, CENTER, CELL - 17.0, CENTER, STROKE, WHITE)
    canvas.line(CELL - 17.0, CENTER, CELL - 30.0, 19.0, STROKE, WHITE)
    canvas.line(CELL - 17.0, CENTER, CELL - 30.0, CELL - 19.0, STROKE, WHITE)


def draw_rules(canvas: Canvas) -> None:
    """A page with lines of text, the last one short."""
    canvas.rrect_frame(14.0, 8.0, 50.0, 56.0, 5.0, THIN_STROKE, WHITE)
    for y, right in ((20.0, 42.0), (29.0, 42.0), (38.0, 42.0), (47.0, 33.0)):
        canvas.line(22.0, y, right, y, THIN_STROKE, WHITE)


def draw_options(canvas: Canvas) -> None:
    """A gear: a body with a hole and eight teeth."""
    canvas.ring(CENTER, CENTER, GEAR_BODY_RADIUS, GEAR_HOLE_RADIUS, WHITE)
    for tooth in range(GEAR_TEETH):
        angle = 2 * math.pi * tooth / GEAR_TEETH
        along = (math.cos(angle), math.sin(angle))
        across = (-along[1], along[0])

        def point(distance: float, half: float, side: float) -> tuple[float, float]:
            return (CENTER + along[0] * distance + across[0] * half * side, CENTER + along[1] * distance + across[1] * half * side)

        canvas.polygon([point(GEAR_TOOTH_INNER, GEAR_TOOTH_BASE, -1.0), point(GEAR_TOOTH_INNER, GEAR_TOOTH_BASE, 1.0),
                        point(GEAR_TOOTH_OUTER, GEAR_TOOTH_TIP, 1.0), point(GEAR_TOOTH_OUTER, GEAR_TOOTH_TIP, -1.0)], WHITE)


def turn_arc_test() -> Callable[[float, float], bool]:
    inner = (TURN_RADIUS - TURN_WIDTH / 2) ** 2
    outer = (TURN_RADIUS + TURN_WIDTH / 2) ** 2

    def inside(x: float, y: float) -> bool:
        squared = (x - CENTER) ** 2 + (y - CENTER) ** 2
        if not inner <= squared <= outer:
            return False
        angle = math.atan2(y - CENTER, x - CENTER)
        if angle < TURN_START:
            angle += 2 * math.pi
        return angle <= TURN_END

    return inside


def draw_new_game(canvas: Canvas) -> None:
    """An arrow that runs round in a circle and points on at its end: a game begun again."""
    reach = TURN_RADIUS + TURN_WIDTH / 2
    canvas.fill((CENTER - reach, CENTER - reach, CENTER + reach, CENTER + reach), turn_arc_test(), WHITE)
    end = TURN_END - 2 * math.pi
    base = (CENTER + TURN_RADIUS * math.cos(end), CENTER + TURN_RADIUS * math.sin(end))
    along = (-math.sin(end), math.cos(end))  # clockwise
    across = (math.cos(end), math.sin(end))
    canvas.polygon([(base[0] + across[0] * TURN_HEAD_HALF, base[1] + across[1] * TURN_HEAD_HALF),
                    (base[0] - across[0] * TURN_HEAD_HALF, base[1] - across[1] * TURN_HEAD_HALF),
                    (base[0] + along[0] * TURN_HEAD_LENGTH, base[1] + along[1] * TURN_HEAD_LENGTH)], WHITE)


def draw_leave(canvas: Canvas) -> None:
    """A door frame and an arrow that goes out through it."""
    canvas.rrect_frame(10.0, 8.0, 36.0, 56.0, 4.0, THIN_STROKE + 1.0, WHITE)
    canvas.line(26.0, CENTER, 53.0, CENTER, THIN_STROKE + 1.0, WHITE)
    canvas.line(53.0, CENTER, 43.0, 22.0, THIN_STROKE + 1.0, WHITE)
    canvas.line(53.0, CENTER, 43.0, CELL - 22.0, THIN_STROKE + 1.0, WHITE)


def draw_hint(canvas: Canvas) -> None:
    """A light bulb: the glass, its neck and two bands of the base."""
    canvas.circle(CENTER, 26.0, 16.0, WHITE)
    canvas.polygon([(23.0, 36.0), (41.0, 36.0), (38.0, 44.0), (26.0, 44.0)], WHITE)
    canvas.rrect(25.0, 46.5, 39.0, 50.5, 2.0, WHITE)
    canvas.rrect(27.0, 52.5, 37.0, 56.5, 2.0, WHITE)


DRAWERS: dict[str, Callable[[Canvas], None]] = {
    "Rules": draw_rules,
    "Options": draw_options,
    "NewGame": draw_new_game,
    "Leave": draw_leave,
    "Close": draw_close,
    "Back": draw_back,
    "Next": draw_next,
    "Hint": draw_hint,
}


def render_cells() -> list[bytearray]:
    cells = []
    for name in ICON_NAMES:
        canvas = IconCanvas()
        DRAWERS[name](canvas)
        cells.append(canvas.downsample())
    return cells


def assemble(cells: list[bytearray]) -> list[bytes]:
    """The atlas as rows of RGBA bytes; the cells that are not used stay transparent."""
    rows = [bytearray(ATLAS_W * 4) for _ in range(ATLAS_H)]
    for index, cell in enumerate(cells):
        x0, y0 = cell_origin(index)
        for y in range(CELL):
            rows[y0 + y][x0 * 4 : (x0 + CELL) * 4] = cell[y * CELL * 4 : (y + 1) * CELL * 4]
    return [bytes(row) for row in rows]


def decode_png(data: bytes) -> list[bytes]:
    """Reads a PNG written by `make_tiles.encode_png` (8 bit RGBA, no interlace, filter 0) of the size of the atlas; anything else is an error."""
    if data[:8] != make_tiles.PNG_SIGNATURE:
        raise ValueError("not a PNG file")
    position = 8
    header = b""
    compressed = b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position : position + 8])
        body = data[position + 8 : position + 8 + length]
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            compressed += body
        position += 12 + length
    width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", header)
    if (width, height, depth, color_type, interlace) != (ATLAS_W, ATLAS_H, 8, 6, 0):
        raise ValueError(f"unexpected PNG header {width}x{height}, depth {depth}, colour type {color_type}, interlace {interlace}")
    raw = zlib.decompress(compressed)
    stride = ATLAS_W * 4 + 1
    rows = []
    for y in range(height):
        line = raw[y * stride : (y + 1) * stride]
        if line[0] != 0:
            raise ValueError(f"row {y} uses filter {line[0]}; the generator writes filter 0 only")
        rows.append(line[1:])
    return rows


# ----- the check -----

def cell_bytes(rows: list[bytes], index: int) -> bytes:
    x0, y0 = cell_origin(index)
    return b"".join(rows[y0 + y][x0 * 4 : (x0 + CELL) * 4] for y in range(CELL))


def pixel_at(cell: bytes, x: int, y: int) -> bytes:
    return cell[(y * CELL + x) * 4 : (y * CELL + x) * 4 + 4]


def problems_of(rows: list[bytes]) -> list[str]:
    """What is wrong with an atlas: equal cells, an icon that is too thin, one that is not white or touches the edge of its cell (the
    corners included), a cell that is not transparent where no icon is."""
    problems = []
    cells = [cell_bytes(rows, index) for index in range(CELL_COUNT)]
    for first in range(CELL_COUNT):
        for second in range(first + 1, CELL_COUNT):
            if cells[first] == cells[second]:
                problems.append(f"{ICON_NAMES[first]} and {ICON_NAMES[second]} are the same picture")
    for index, cell in enumerate(cells):
        name = ICON_NAMES[index]
        opaque = 0
        for y in range(CELL):
            for x in range(CELL):
                red, green, blue, alpha = pixel_at(cell, x, y)
                if alpha == 0:
                    continue
                if (red, green, blue) != WHITE:
                    problems.append(f"{name} has a pixel that is not white at {x},{y} (the scene tints it)")
                    break
                if alpha == 255:
                    opaque += 1
                if min(x, y, CELL - 1 - x, CELL - 1 - y) < ICON_MARGIN:
                    problems.append(f"{name} paints within {ICON_MARGIN} pixels of the edge of its cell at {x},{y}")
                    break
            else:
                continue
            break
        if opaque < MIN_OPAQUE_PIXELS:
            problems.append(f"{name} has {opaque} opaque pixels, fewer than {MIN_OPAQUE_PIXELS}")
        if any(pixel_at(cell, x, y)[3] for x, y in ((0, 0), (CELL - 1, 0), (0, CELL - 1), (CELL - 1, CELL - 1))):
            problems.append(f"{name} has no transparent corners")
    for index in range(CELL_COUNT, COLUMNS * ROWS):
        if any(cell_bytes(rows, index)[3::4]):
            problems.append(f"the unused cell {index} is not transparent")
    return problems


def xml_problems() -> list[str]:
    path = make_tiles.XML_PATH
    if not path.is_file():
        return [f"{path} is missing"]
    if path.read_text(encoding="utf-8") != make_tiles.library_xml():
        return [f"{path.relative_to(ROOT)} differs from what tools/make_tiles.py makes now (run it)"]
    return []


def check() -> int:
    expected_rows = assemble(render_cells())
    failures = problems_of(expected_rows)
    if not PNG_PATH.is_file():
        failures.append(f"{PNG_PATH} is missing")
    else:
        try:
            if decode_png(PNG_PATH.read_bytes()) != expected_rows:
                failures.append(f"{PNG_PATH.relative_to(ROOT)} differs from what the generator makes now")
        except (ValueError, zlib.error, struct.error) as error:
            failures.append(f"{PNG_PATH.relative_to(ROOT)}: {error}")
    failures += xml_problems()
    for failure in failures:
        print("make_icons: " + failure, file=sys.stderr)
    if not failures:
        print(f"make_icons: {CELL_COUNT} icons, {PNG_PATH.relative_to(ROOT)} and the sprites in config/TextureLibrary.xml are up to date")
    return 1 if failures else 0


def write() -> int:
    rows = assemble(render_cells())
    problems = problems_of(rows)
    if problems:
        for problem in problems:
            print("make_icons: " + problem, file=sys.stderr)
        return 1
    PNG_PATH.parent.mkdir(parents=True, exist_ok=True)
    PNG_PATH.write_bytes(make_tiles.encode_png(rows))
    print(f"atlas {ATLAS_W}x{ATLAS_H}, {COLUMNS} columns x {ROWS} rows of cells {CELL}x{CELL}")
    for index in range(CELL_COUNT):
        x, y = cell_origin(index)
        print(f"  cell {index:2d}  x={x:3d} y={y:3d}  {sprite_name(index)}")
    print(f"wrote {PNG_PATH.relative_to(ROOT)} ({PNG_PATH.stat().st_size} bytes); run tools/make_tiles.py for config/TextureLibrary.xml")
    return 0


# ----- the preview -----

PREVIEW_GROUND: Color = (52, 78, 120)  # a button's blue: the icon is white over it
PREVIEW_SIZES = (24, 56, 112)
PREVIEW_GAP = 8


def resample(cell: bytes, size: int) -> list[bytes]:
    """The cell at size x size pixels by averaging the covered source area (a bigger size only enlarges it), over the ground; RGB rows."""
    step = CELL / size
    rows = []
    for out_y in range(size):
        row = bytearray()
        for out_x in range(size):
            red = green = blue = alpha = total = 0.0
            for sy in range(int(out_y * step), min(CELL, int((out_y + 1) * step - 1e-9) + 1)):
                weight_y = min((out_y + 1) * step, sy + 1) - max(out_y * step, sy)
                for sx in range(int(out_x * step), min(CELL, int((out_x + 1) * step - 1e-9) + 1)):
                    weight = weight_y * (min((out_x + 1) * step, sx + 1) - max(out_x * step, sx))
                    pixel = pixel_at(cell, sx, sy)
                    a = pixel[3] / 255
                    red += pixel[0] * a * weight
                    green += pixel[1] * a * weight
                    blue += pixel[2] * a * weight
                    alpha += a * weight
                    total += weight
            coverage = alpha / total
            for value, ground in ((red, PREVIEW_GROUND[0]), (green, PREVIEW_GROUND[1]), (blue, PREVIEW_GROUND[2])):
                row.append(min(255, round(value / total + ground * (1 - coverage))))
        rows.append(bytes(row))
    return rows


def preview(out: Path) -> int:
    """A contact sheet on a button's colour: one row of cells per size of PREVIEW_SIZES, COLUMNS cells to a row."""
    out = out.expanduser().resolve()
    if out.is_relative_to(ROOT):
        print(f"make_icons: the preview must not be written inside the repository ({ROOT}); it is never committed", file=sys.stderr)
        return 1
    rows_atlas = assemble(render_cells())
    cells = [cell_bytes(rows_atlas, index) for index in range(CELL_COUNT)]
    widest = max(PREVIEW_SIZES)
    block_rows = (CELL_COUNT + COLUMNS - 1) // COLUMNS
    sheet_width = COLUMNS * (widest + PREVIEW_GAP) + PREVIEW_GAP
    sheet: list[bytearray] = []
    for size in PREVIEW_SIZES:
        top = len(sheet)
        sheet.extend(bytearray(PREVIEW_GROUND * sheet_width) for _ in range(PREVIEW_GAP + block_rows * (size + PREVIEW_GAP)))
        for index, cell in enumerate(cells):
            x = PREVIEW_GAP + (index % COLUMNS) * (widest + PREVIEW_GAP) + (widest - size) // 2
            y = top + PREVIEW_GAP + (index // COLUMNS) * (size + PREVIEW_GAP)
            for dy, line in enumerate(resample(cell, size)):
                sheet[y + dy][x * 3 : (x + size) * 3] = line
    rgba = [bytes(b for i in range(0, len(row), 3) for b in (row[i], row[i + 1], row[i + 2], 255)) for row in sheet]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(make_tiles.encode_png(rgba))
    print(f"wrote {out} ({sheet_width}x{len(sheet)}, sizes {PREVIEW_SIZES})")
    return 0


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Draws the Mahjong icon atlas.")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--check", action="store_true", help="exit 0 when the committed files are what the generators make now")
    modes.add_argument("--preview", metavar="OUT.png", help="write a contact sheet at 24, 56 and 112 pixels (OUT must be outside the repository)")
    arguments = parser.parse_args(argv)
    if arguments.preview:
        return preview(Path(arguments.preview))
    return check() if arguments.check else write()


if __name__ == "__main__":
    sys.exit(main())
