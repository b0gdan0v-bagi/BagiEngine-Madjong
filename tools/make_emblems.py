#!/usr/bin/env python3
"""The seat emblems of the Mahjong game: a round picture per seat and two rings, drawn by this script and written as one atlas PNG.

Like the tiles (tools/make_tiles.py, whose Canvas, 5x7 font and PNG encoder are imported here, not copied) the art is ours: circles,
rectangles and a bitmap digit, no third-party picture and no font file.

    python3 tools/make_emblems.py            writes assets/emblems/emblems.png
    python3 tools/make_emblems.py --check    exits 0 when the committed file is what the generator makes now
    python3 tools/make_emblems.py --preview OUT.png   writes a contact sheet at the real size and at four times (OUT outside the repository)

Atlas: COLUMNS x ROWS cells of CELL x CELL pixels, RGBA, transparent outside the pictures. Cell 0 is the person, cells 1..10 the computer of
level 1..10 (the level numeral is part of the picture), cell 11 the turn ring and cell 12 the calling ring. All cells share one centre.
The emblem is a disc of DISC_RADIUS; the rings lie outside it, the turn ring solid and the calling ring dotted, so both fit together
round one emblem. The rings are white: the table tints them with palette roles, so no colour of a ring is baked into the picture.

The check compares the decoded pixels, not the bytes of the PNG (zlib output differs between versions). The numbers below are the source;
src/Mahjong/Table/EmblemAtlas.h mirrors them and tests/EmblemSpritesTest.cpp compares the two.
"""

import argparse
import math
import struct
import sys
import zlib
from pathlib import Path
from typing import Optional

import make_tiles
from make_tiles import Canvas, Color

CELL = 64
COLUMNS = 4
ROWS = 4
ATLAS_W = CELL * COLUMNS
ATLAS_H = CELL * ROWS
LEVEL_COUNT = 10
PERSON_CELL = 0
TURN_RING_CELL = PERSON_CELL + 1 + LEVEL_COUNT
CALLING_RING_CELL = TURN_RING_CELL + 1
CELL_COUNT = CALLING_RING_CELL + 1
SUPERSAMPLE = 4

ROOT = Path(__file__).resolve().parent.parent
PNG_PATH = ROOT / "assets" / "emblems" / "emblems.png"
TEXTURE_PATH = "assets/emblems/emblems.png"

# ----- geometry (cell pixels) -----

CENTER = CELL / 2
DISC_RADIUS = 24.0  # the emblem, its outline included
DISC_OUTLINE = 1.2
TURN_RING_OUTER = 28.0  # the solid ring: 3 pixels wide, 1 pixel clear of the disc
TURN_RING_INNER = 25.0
CALLING_DOT_COUNT = 24
CALLING_DOT_RADIUS = 1.8  # the dotted ring lies outside the turn ring and inside the cell (CELL / 2 = 32)
CALLING_DOT_ORBIT = 30.2

# ----- colours -----

OUTLINE: Color = (22, 28, 26)
WHITE: Color = (255, 255, 255)
PERSON_BACK: Color = (214, 226, 220)
PERSON_INK: Color = (36, 92, 76)
COMPUTER_BACK: Color = (52, 78, 120)
SCREEN: Color = (16, 24, 36)
DIGIT: Color = (120, 230, 150)
STAND: Color = (180, 190, 205)

# The 5x7 font of make_tiles has no zero (no tile has one); level 10 needs it.
make_tiles.FONT.setdefault("0", (".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."))


class EmblemCanvas(Canvas):
    """The Canvas of the tiles at the size of an emblem cell: the shapes, the view and the fill are inherited."""

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


def computer_cell(level: int) -> int:
    return PERSON_CELL + level


# ----- the pictures -----

def draw_disc(canvas: Canvas, back: Color) -> None:
    canvas.circle(CENTER, CENTER, DISC_RADIUS, OUTLINE)
    canvas.circle(CENTER, CENTER, DISC_RADIUS - DISC_OUTLINE, back)


def draw_person(canvas: Canvas) -> None:
    """A head over the shoulders, the shoulders cut by the disc."""
    draw_disc(canvas, PERSON_BACK)
    canvas.circle(CENTER, 25.0, 8.2, PERSON_INK)
    inner = DISC_RADIUS - DISC_OUTLINE
    shoulders_y = 55.5
    shoulders = 16.0
    canvas.fill((CENTER - shoulders, shoulders_y - shoulders, CENTER + shoulders, CENTER + inner),
                lambda x, y: (x - CENTER) ** 2 + (y - shoulders_y) ** 2 <= shoulders ** 2
                and (x - CENTER) ** 2 + (y - CENTER) ** 2 <= inner ** 2, PERSON_INK)


def draw_computer(canvas: Canvas, level: int) -> None:
    """A monitor on a foot with the level numeral on its screen."""
    draw_disc(canvas, COMPUTER_BACK)
    canvas.rrect(14.0, 17.0, 50.0, 43.0, 5.0, SCREEN)
    canvas.rect(29.0, 43.0, 35.0, 47.0, STAND)
    canvas.rrect(23.0, 46.5, 41.0, 49.0, 1.2, STAND)
    text = str(level)
    canvas.text(text, CENTER, 30.0, 3.25 if len(text) == 1 else 2.75, DIGIT)


def draw_turn_ring(canvas: Canvas) -> None:
    canvas.ring(CENTER, CENTER, TURN_RING_OUTER, TURN_RING_INNER, WHITE)


def draw_calling_ring(canvas: Canvas) -> None:
    for index in range(CALLING_DOT_COUNT):
        angle = 2 * math.pi * index / CALLING_DOT_COUNT
        canvas.circle(CENTER + CALLING_DOT_ORBIT * math.cos(angle), CENTER + CALLING_DOT_ORBIT * math.sin(angle), CALLING_DOT_RADIUS, WHITE)


def render_cells() -> list[bytearray]:
    cells = []
    for index in range(CELL_COUNT):
        canvas = EmblemCanvas()
        if index == PERSON_CELL:
            draw_person(canvas)
        elif index == TURN_RING_CELL:
            draw_turn_ring(canvas)
        elif index == CALLING_RING_CELL:
            draw_calling_ring(canvas)
        else:
            draw_computer(canvas, index - PERSON_CELL)
        cells.append(canvas.downsample())
    return cells


def cell_name(index: int) -> str:
    if index == PERSON_CELL:
        return "person"
    if index == TURN_RING_CELL:
        return "turn ring"
    if index == CALLING_RING_CELL:
        return "calling ring"
    return f"computer {index - PERSON_CELL}"


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


def alpha_in(cell: bytes, x: int, y: int) -> int:
    return cell[(y * CELL + x) * 4 + 3]


def distance(x: int, y: int) -> float:
    """From the centre of the cell to the centre of the pixel."""
    return math.hypot(x + 0.5 - CENTER, y + 0.5 - CENTER)


def problems_of(rows: list[bytes]) -> list[str]:
    """What is wrong with an atlas: equal cells, a picture that is not opaque in the middle or touches a corner, a ring that is not clear in
    the middle or not white, a picture on the ring's ground or a ring on the picture's, stray pixels."""
    problems = []
    cells = [cell_bytes(rows, index) for index in range(CELL_COUNT)]
    for first in range(CELL_COUNT):
        for second in range(first + 1, CELL_COUNT):
            if cells[first] == cells[second]:
                problems.append(f"{cell_name(first)} and {cell_name(second)} are the same picture")
    ring_cells = (TURN_RING_CELL, CALLING_RING_CELL)
    for index in range(CELL_COUNT):
        cell = cells[index]
        name = cell_name(index)
        if any(alpha_in(cell, x, y) for x, y in ((0, 0), (CELL - 1, 0), (0, CELL - 1), (CELL - 1, CELL - 1))):
            problems.append(f"{name} has no transparent corners")
        if not any(alpha_in(cell, x, y) for y in range(CELL) for x in range(CELL)):
            problems.append(f"{name} is empty")
        if index in ring_cells:
            if any(alpha_in(cell, x, y) for y in range(CELL) for x in range(CELL) if distance(x, y) < TURN_RING_INNER - 1.0):
                problems.append(f"{name} is not transparent inside")
            if any(cell[(y * CELL + x) * 4 : (y * CELL + x) * 4 + 3] != bytes(WHITE) for y in range(CELL) for x in range(CELL) if alpha_in(cell, x, y)):
                problems.append(f"{name} is not white (the table tints it)")
        else:
            if min(alpha_in(cell, CELL // 2 + dx, CELL // 2 + dy) for dx in range(-2, 2) for dy in range(-2, 2)) != 255:
                problems.append(f"{name} is not opaque in the middle")
            if any(alpha_in(cell, x, y) for y in range(CELL) for x in range(CELL) if distance(x, y) > DISC_RADIUS + 1.0):
                problems.append(f"{name} paints outside its disc, on the ground of the rings")
    for index in range(CELL_COUNT, COLUMNS * ROWS):
        if any(cell_bytes(rows, index)[3::4]):
            problems.append(f"the unused cell {index} is not transparent")
    return problems


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
    for failure in failures:
        print("make_emblems: " + failure, file=sys.stderr)
    if not failures:
        print(f"make_emblems: {CELL_COUNT} cells, {PNG_PATH.relative_to(ROOT)} is up to date")
    return 1 if failures else 0


def write() -> int:
    rows = assemble(render_cells())
    problems = problems_of(rows)
    if problems:
        for problem in problems:
            print("make_emblems: " + problem, file=sys.stderr)
        return 1
    PNG_PATH.parent.mkdir(parents=True, exist_ok=True)
    PNG_PATH.write_bytes(make_tiles.encode_png(rows))
    print(f"atlas {ATLAS_W}x{ATLAS_H}, {COLUMNS} columns x {ROWS} rows of cells {CELL}x{CELL}")
    for index in range(CELL_COUNT):
        x, y = cell_origin(index)
        print(f"  cell {index:2d}  x={x:3d} y={y:3d}  {cell_name(index)}")
    print(f"wrote {PNG_PATH.relative_to(ROOT)} ({PNG_PATH.stat().st_size} bytes)")
    return 0


# ----- the preview -----

PREVIEW_FELT: Color = (30, 90, 60)
PREVIEW_SIZES = (36, 144)  # the real size of an emblem on the table (the disc plus its ring box is 44 view units), and four times
PREVIEW_GAP = 8
PREVIEW_TURN_TINT: Color = (255, 214, 64)
PREVIEW_CALLING_TINT: Color = (96, 208, 255)
PREVIEW_COMBINATIONS = ((PERSON_CELL, True, False), (computer_cell(5), True, True), (computer_cell(10), False, True))


def tinted(cell: bytes, color: Color) -> bytes:
    out = bytearray(cell)
    for start in range(0, len(out), 4):
        for channel in range(3):
            out[start + channel] = out[start + channel] * color[channel] // 255
    return bytes(out)


def over(base: bytes, top: bytes) -> bytes:
    """`top` laid over `base`, straight alpha."""
    out = bytearray(len(base))
    for start in range(0, len(base), 4):
        top_alpha = top[start + 3] / 255
        base_alpha = base[start + 3] / 255
        alpha = top_alpha + base_alpha * (1 - top_alpha)
        if alpha > 0:
            for channel in range(3):
                out[start + channel] = round((top[start + channel] * top_alpha + base[start + channel] * base_alpha * (1 - top_alpha)) / alpha)
        out[start + 3] = round(alpha * 255)
    return bytes(out)


def resample(cell: bytes, size: int) -> list[bytes]:
    """The cell at size x size pixels by averaging the covered source area (the cell is only enlarged, not redrawn), over the felt; RGB rows."""
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
                    a = cell[(sy * CELL + sx) * 4 + 3] / 255
                    red += cell[(sy * CELL + sx) * 4] * a * weight
                    green += cell[(sy * CELL + sx) * 4 + 1] * a * weight
                    blue += cell[(sy * CELL + sx) * 4 + 2] * a * weight
                    alpha += a * weight
                    total += weight
            coverage = alpha / total
            for value, felt in ((red, PREVIEW_FELT[0]), (green, PREVIEW_FELT[1]), (blue, PREVIEW_FELT[2])):
                row.append(min(255, round(value / total + felt * (1 - coverage))))
        rows.append(bytes(row))
    return rows


def preview(out: Path) -> int:
    """A contact sheet on the felt: every cell, then a person with the turn ring, a computer with both rings and one with the calling ring
    (the tints are those of the table's gold family and a blue), at each size of PREVIEW_SIZES, COLUMNS cells to a row."""
    out = out.expanduser().resolve()
    if out.is_relative_to(ROOT):
        print(f"make_emblems: the preview must not be written inside the repository ({ROOT}); it is never committed", file=sys.stderr)
        return 1
    rows_atlas = assemble(render_cells())
    cells = [cell_bytes(rows_atlas, index) for index in range(CELL_COUNT)]
    turn = tinted(cells[TURN_RING_CELL], PREVIEW_TURN_TINT)
    calling = tinted(cells[CALLING_RING_CELL], PREVIEW_CALLING_TINT)
    for index, with_turn, with_calling in PREVIEW_COMBINATIONS:
        picture = cells[index]
        if with_turn:
            picture = over(picture, turn)
        if with_calling:
            picture = over(picture, calling)
        cells.append(picture)
    block_rows = (len(cells) + COLUMNS - 1) // COLUMNS
    sheet: list[bytearray] = []
    sheet_width = COLUMNS * (max(PREVIEW_SIZES) + PREVIEW_GAP) + PREVIEW_GAP
    for size in PREVIEW_SIZES:
        top = len(sheet)
        sheet.extend(bytearray(PREVIEW_FELT * sheet_width) for _ in range(PREVIEW_GAP + block_rows * (size + PREVIEW_GAP)))
        for index, cell in enumerate(cells):
            x = PREVIEW_GAP + (index % COLUMNS) * (max(PREVIEW_SIZES) + PREVIEW_GAP) + (max(PREVIEW_SIZES) - size) // 2
            y = top + PREVIEW_GAP + (index // COLUMNS) * (size + PREVIEW_GAP)
            for dy, line in enumerate(resample(cell, size)):
                sheet[y + dy][x * 3 : (x + size) * 3] = line
    rgba = [bytes(b for i in range(0, len(row), 3) for b in (row[i], row[i + 1], row[i + 2], 255)) for row in sheet]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(make_tiles.encode_png(rgba))
    print(f"wrote {out} ({sheet_width}x{len(sheet)}, sizes {PREVIEW_SIZES})")
    return 0


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Draws the Mahjong seat emblem atlas.")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--check", action="store_true", help="exit 0 when the committed file is what the generator makes now")
    modes.add_argument("--preview", metavar="OUT.png", help="write a contact sheet at the real size and at four times (OUT must be outside the repository)")
    arguments = parser.parse_args(argv)
    if arguments.preview:
        return preview(Path(arguments.preview))
    return check() if arguments.check else write()


if __name__ == "__main__":
    sys.exit(main())
