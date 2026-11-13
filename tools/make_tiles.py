#!/usr/bin/env python3
"""The tile art of the Mahjong game: a software rasterizer, one function per kind of face, the atlas PNG and the sprite library.

Everything is drawn here, by this script, from rectangles, circles, lines and a 5x7 bitmap font of our own: there is no third-party
tile set and no font file, so the art is ours and anyone can change it by editing the functions below and running the script again.

    python3 tools/make_tiles.py            writes assets/tiles/tiles.png and config/TextureLibrary.xml
    python3 tools/make_tiles.py --check    exits 0 when the committed files are what the generator makes now
    python3 tools/make_tiles.py --preview OUT.png   writes a contact sheet of every cell at four sizes (OUT outside the repository)

Every face has a corner index in its top left corner (a rank glyph over a small sign of the suit, or the letter of a wind, a dragon,
a flower or a season), so that a tile can be told at the small sizes of the table. The patterns are drawn under a view transform that
scales them toward the bottom right corner and leaves the corner free; the check holds that the index box of every face has index pixels
and no art. The back has no index.

The check compares the decoded pixels and the text of the XML, not the bytes of the PNG, because zlib output differs between versions.
Standard library only (zlib, struct). The geometry below (CELL_W, CELL_H, COLUMNS, the cell of an id) is the source of the numbers;
src/Mahjong/Table/TileAtlas.h mirrors them and tests/TileSpritesTest.cpp compares the two.

Atlas: COLUMNS x ROWS cells of CELL_W x CELL_H pixels, RGBA, transparent outside the tiles. Cell index = tile id of Tile.h
(0..8 dots, 9..17 bamboo, 18..26 characters, 27..30 winds, 31..33 dragons, 34..37 flowers, 38..41 seasons), cell 42 = the back,
cell 43 = a wall stack seen from above (a STACK_SIZE square in the corner of its cell, the rest transparent).
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path
from typing import Callable, Optional

CELL_W = 96
CELL_H = 128
COLUMNS = 8
ROWS = 6
ATLAS_W = CELL_W * COLUMNS
ATLAS_H = CELL_H * ROWS
TILE_ID_COUNT = 42
BACK_INDEX = TILE_ID_COUNT
STACK_INDEX = BACK_INDEX + 1
CELL_COUNT = TILE_ID_COUNT + 2
STACK_SIZE = 96  # the stack is a square of this many pixels at the top left corner of its cell
SUPERSAMPLE = 2

ROOT = Path(__file__).resolve().parent.parent
PNG_PATH = ROOT / "assets" / "tiles" / "tiles.png"
XML_PATH = ROOT / "config" / "TextureLibrary.xml"
TEXTURE_PATH = "assets/tiles/tiles.png"
SPRITE_PREFIX = "Tile_"
BACK_SPRITE = "Tile_Back"
STACK_SPRITE = "Tile_Stack"

Color = tuple[int, int, int]

# ----- palette -----
OUTLINE: Color = (38, 46, 42)
SIDE_LIGHT: Color = (226, 216, 186)  # the ivory side next to the face, lit from the top left
SIDE: Color = (200, 189, 158)
SIDE_SHADE: Color = (150, 138, 108)  # the outer edge of the bottom and right sides
FACE: Color = (248, 244, 228)
INK: Color = (28, 28, 36)
RED: Color = (196, 40, 40)
BLUE: Color = (30, 74, 160)
GREEN: Color = (28, 130, 62)
DARK_GREEN: Color = (16, 84, 40)
LIGHT_GREEN: Color = (120, 200, 120)
BACK_FACE: Color = (22, 120, 104)
BACK_RIM: Color = (88, 176, 156)
BACK_DARK: Color = (12, 80, 70)
BACK_LIGHT: Color = (44, 146, 126)

# The corner index: a glyph of the 5x7 font at INDEX_PIXEL cell units per font pixel with its top left corner at (INDEX_X, INDEX_Y), and
# below it a sign of INDEX_SIGN_HEIGHT units. The patterns are scaled by ART_SCALE toward the bottom right corner of the face so that they
# leave INDEX_BOX (x0, y0, x1, y1, in cell pixels) free; a bigger index reads better on a small tile but takes more of the face.
INDEX_PIXEL = 4.0
INDEX_X = 8.0  # a multiple of 4, so that at a quarter size (a discard) a font pixel is one whole pixel
INDEX_Y = 8.0
INDEX_GAP = 4.0
INDEX_SIGN_HEIGHT = 16.0
ART_SCALE = 0.68
INDEX_OUTLINE = 1.0

# The body of a tile: an outline, the ivory side and the face on top of it. The side shows at the bottom (SIDE_BOTTOM units) and at the
# right (SIDE_RIGHT units) as a bevel, lighter next to the face and darker at the outer edge: about 2.5 to 3 pixels at discard size
# (a quarter of the cell). The patterns are laid out in ART_FACE, the face of the first design, and mapped into BODY_FACE by art_view.
SIDE_BOTTOM = 11.0
SIDE_RIGHT = 10.0
BODY_OUTLINE = (1.0, 1.0, CELL_W - 1.0, CELL_H - 1.0, 11.0)
BODY_SIDE = (2.0, 2.0, CELL_W - 2.0, CELL_H - 2.0, 10.0)
BODY_FACE = (4.0, 4.0, CELL_W - 2.0 - SIDE_RIGHT, CELL_H - 2.0 - SIDE_BOTTOM, 8.0)
ART_FACE = (4.0, 4.0, CELL_W - 4.0, CELL_H - 10.0)
ART_EXTENT = (86.0, 112.0)  # the right and bottom edge of the art in art coordinates (outlines included)
ART_MARGIN = (2.0, 3.0)  # the room the art keeps to the right and to the bottom edge of the face
CENTER_X = CELL_W / 2
CENTER_Y = (ART_FACE[1] + ART_FACE[3]) / 2  # the centre of the art, in art coordinates
FACE_CENTER_X = (BODY_FACE[0] + BODY_FACE[2]) / 2  # the centre of the face, in cell coordinates
FACE_CENTER_Y = (BODY_FACE[1] + BODY_FACE[3]) / 2
INDEX_GLYPH_W = 5 * INDEX_PIXEL
INDEX_GLYPH_H = 7 * INDEX_PIXEL
INDEX_CENTER_X = INDEX_X + INDEX_GLYPH_W / 2
INDEX_SIGN_TOP = INDEX_Y + INDEX_GLYPH_H + INDEX_GAP
# the box has room for a glyph under the glyph (flowers and seasons), the art keeps out of all of it
INDEX_BOX = (4, 4, int(INDEX_X + INDEX_GLYPH_W + 2), int(INDEX_SIGN_TOP + INDEX_GLYPH_H + 2))


# ----- the rasterizer -----

class Canvas:
    """A supersampled RGBA canvas. Shapes are given in cell units (CELL_W x CELL_H) and painted opaque; `downsample` makes the cell."""

    def __init__(self) -> None:
        self.scale = SUPERSAMPLE
        self.width = CELL_W * self.scale
        self.height = CELL_H * self.scale
        self.pixels = bytearray(self.width * self.height * 4)
        self.view = (1.0, 0.0, 0.0)  # scale, offset x, offset y: art coordinates -> cell coordinates

    def set_view(self, scale: float, offset_x: float, offset_y: float) -> None:
        """From now on every shape is given in art coordinates and lands at `art * scale + offset` in the cell."""
        self.view = (scale, offset_x, offset_y)

    def reset_view(self) -> None:
        self.view = (1.0, 0.0, 0.0)

    def copy(self) -> "Canvas":
        other = Canvas()
        other.pixels[:] = self.pixels
        return other

    def fill(self, bounds: tuple[float, float, float, float], inside: Callable[[float, float], bool], color: Color) -> None:
        """Paints every pixel of the box `bounds` (x0, y0, x1, y1) whose centre is inside the shape (both in the units of the view)."""
        view_scale, view_x, view_y = self.view
        if self.view != (1.0, 0.0, 0.0):
            bounds = (bounds[0] * view_scale + view_x, bounds[1] * view_scale + view_y,
                      bounds[2] * view_scale + view_x, bounds[3] * view_scale + view_y)
            shape = inside
            inside = lambda x, y: shape((x - view_x) / view_scale, (y - view_y) / view_scale)
        scale = self.scale
        left = max(0, int(bounds[0] * scale) - 1)
        top = max(0, int(bounds[1] * scale) - 1)
        right = min(self.width, int(bounds[2] * scale) + 2)
        bottom = min(self.height, int(bounds[3] * scale) + 2)
        pixel = bytes((color[0], color[1], color[2], 255))
        pixels = self.pixels
        for py in range(top, bottom):
            y = (py + 0.5) / scale
            row = py * self.width
            for px in range(left, right):
                if inside((px + 0.5) / scale, y):
                    start = (row + px) * 4
                    pixels[start : start + 4] = pixel

    def rect(self, x0: float, y0: float, x1: float, y1: float, color: Color) -> None:
        self.fill((x0, y0, x1, y1), lambda x, y: x0 <= x <= x1 and y0 <= y <= y1, color)

    def rrect(self, x0: float, y0: float, x1: float, y1: float, radius: float, color: Color) -> None:
        self.fill((x0, y0, x1, y1), rounded_rect_test(x0, y0, x1, y1, radius), color)

    def rrect_frame(self, x0: float, y0: float, x1: float, y1: float, radius: float, thickness: float, color: Color) -> None:
        outer = rounded_rect_test(x0, y0, x1, y1, radius)
        inner = rounded_rect_test(x0 + thickness, y0 + thickness, x1 - thickness, y1 - thickness, max(0.0, radius - thickness))
        self.fill((x0, y0, x1, y1), lambda x, y: outer(x, y) and not inner(x, y), color)

    def circle(self, cx: float, cy: float, radius: float, color: Color) -> None:
        limit = radius * radius
        self.fill((cx - radius, cy - radius, cx + radius, cy + radius), lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= limit, color)

    def ring(self, cx: float, cy: float, outer: float, inner: float, color: Color) -> None:
        big = outer * outer
        small = inner * inner
        self.fill((cx - outer, cy - outer, cx + outer, cy + outer), lambda x, y: small < (x - cx) ** 2 + (y - cy) ** 2 <= big, color)

    def line(self, x0: float, y0: float, x1: float, y1: float, thickness: float, color: Color) -> None:
        """A thick line with round ends."""
        half = thickness / 2
        dx = x1 - x0
        dy = y1 - y0
        length_squared = dx * dx + dy * dy

        def inside(x: float, y: float) -> bool:
            t = 0.0 if length_squared == 0 else max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / length_squared))
            return (x - (x0 + t * dx)) ** 2 + (y - (y0 + t * dy)) ** 2 <= half * half

        self.fill((min(x0, x1) - half, min(y0, y1) - half, max(x0, x1) + half, max(y0, y1) + half), inside, color)

    def polygon(self, points: list[tuple[float, float]], color: Color) -> None:
        """An even-odd filled polygon."""

        def inside(x: float, y: float) -> bool:
            odd = False
            previous = points[-1]
            for current in points:
                if (current[1] > y) != (previous[1] > y):
                    cross = current[0] + (y - current[1]) * (previous[0] - current[0]) / (previous[1] - current[1])
                    if x < cross:
                        odd = not odd
                previous = current
            return odd

        xs = [point[0] for point in points]
        ys = [point[1] for point in points]
        self.fill((min(xs), min(ys), max(xs), max(ys)), inside, color)

    def glyph(self, character: str, x: float, y: float, pixel: float, color: Color, outline: Optional[Color] = None) -> None:
        """A character of the 5x7 font with its top left corner at (x, y); every font pixel is a square of `pixel` cell units.
        With `outline` the glyph gets a border of INDEX_OUTLINE units in that colour (for a light colour on the ivory face)."""
        if outline is not None:
            grow = INDEX_OUTLINE
            for row, bits in enumerate(FONT[character]):
                for column, bit in enumerate(bits):
                    if bit == "#":
                        self.rect(x + column * pixel - grow, y + row * pixel - grow,
                                  x + (column + 1) * pixel + grow, y + (row + 1) * pixel + grow, outline)
        for row, bits in enumerate(FONT[character]):
            for column, bit in enumerate(bits):
                if bit == "#":
                    self.rect(x + column * pixel, y + row * pixel, x + (column + 1) * pixel, y + (row + 1) * pixel, color)

    def text(self, text: str, center_x: float, center_y: float, pixel: float, color: Color, gap: float = 1.0) -> None:
        """Glyphs side by side, the block centred on (center_x, center_y)."""
        advance = (GLYPH_W + gap) * pixel
        width = len(text) * advance - gap * pixel
        left = center_x - width / 2
        top = center_y - GLYPH_H * pixel / 2
        for index, character in enumerate(text):
            self.glyph(character, left + index * advance, top, pixel, color)

    def downsample(self) -> bytearray:
        """The CELL_W x CELL_H RGBA picture: each output pixel is the mean of its block, the colours weighted by alpha."""
        scale = self.scale
        out = bytearray(CELL_W * CELL_H * 4)
        area = scale * scale
        for y in range(CELL_H):
            for x in range(CELL_W):
                red = green = blue = alpha = 0
                for sy in range(scale):
                    start = ((y * scale + sy) * self.width + x * scale) * 4
                    for sx in range(scale):
                        a = self.pixels[start + sx * 4 + 3]
                        red += self.pixels[start + sx * 4] * a
                        green += self.pixels[start + sx * 4 + 1] * a
                        blue += self.pixels[start + sx * 4 + 2] * a
                        alpha += a
                target = (y * CELL_W + x) * 4
                if alpha:
                    out[target] = (red + alpha // 2) // alpha
                    out[target + 1] = (green + alpha // 2) // alpha
                    out[target + 2] = (blue + alpha // 2) // alpha
                    out[target + 3] = (alpha + area // 2) // area
        return out


def rounded_rect_test(x0: float, y0: float, x1: float, y1: float, radius: float) -> Callable[[float, float], bool]:
    def inside(x: float, y: float) -> bool:
        if x < x0 or x > x1 or y < y0 or y > y1:
            return False
        dx = max(x0 + radius - x, 0.0, x - (x1 - radius))
        dy = max(y0 + radius - y, 0.0, y - (y1 - radius))
        return dx * dx + dy * dy <= radius * radius

    return inside


# ----- the font: 5x7 pixels, the digits and the letters the tiles use -----

GLYPH_W = 5
GLYPH_H = 7
FONT: dict[str, tuple[str, ...]] = {
    "1": ("..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."),
    "2": (".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"),
    "3": (".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."),
    "4": ("...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."),
    "5": ("#####", "#....", "####.", "....#", "....#", "#...#", ".###."),
    "6": ("..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."),
    "7": ("#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."),
    "8": (".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."),
    "9": (".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."),
    "E": ("#####", "#....", "#....", "####.", "#....", "#....", "#####"),
    "S": (".####", "#....", "#....", ".###.", "....#", "....#", "####."),
    "W": ("#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"),
    "N": ("#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"),
    "R": ("####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"),
    "G": (".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."),
    "F": ("#####", "#....", "#....", "####.", "#....", "#....", "#...."),
    "T": ("#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."),
}


# ----- the notation of Tile.h -----

SUIT_LETTERS = "psm"  # dots, bamboo, characters
WIND_LETTERS = "ESWN"
DRAGON_LETTERS = "RWG"


def notation(tile_id: int) -> str:
    """The same text as Tile.h's ToString: 5p, 3s, 9m, Ew, Rd, F1, T4."""
    if tile_id < 27:
        return f"{tile_id % 9 + 1}{SUIT_LETTERS[tile_id // 9]}"
    if tile_id < 31:
        return f"{WIND_LETTERS[tile_id - 27]}w"
    if tile_id < 34:
        return f"{DRAGON_LETTERS[tile_id - 31]}d"
    if tile_id < 38:
        return f"F{tile_id - 34 + 1}"
    return f"T{tile_id - 38 + 1}"


def cell_origin(index: int) -> tuple[int, int]:
    return (index % COLUMNS) * CELL_W, (index // COLUMNS) * CELL_H


# ----- the tile body -----

def draw_body(canvas: Canvas, face: Color = FACE) -> None:
    right = BODY_SIDE[2]
    bottom = BODY_SIDE[3]
    canvas.rrect(*BODY_OUTLINE, OUTLINE)
    # the side in three bands that narrow toward the top left: dark at the outer edge, the ivory, a light one next to the face
    canvas.rrect(*BODY_SIDE, SIDE_SHADE)
    canvas.rrect(BODY_SIDE[0], BODY_SIDE[1], right - 2.0, bottom - 4.0, 10.0, SIDE)
    canvas.rrect(BODY_SIDE[0], BODY_SIDE[1], BODY_FACE[2] + 5.0, BODY_FACE[3] + 3.0, 10.0, SIDE_LIGHT)
    canvas.rrect(*BODY_FACE, face)
    # a thin light edge along the top of the face gives the bevel
    canvas.line(BODY_FACE[0] + 7, BODY_FACE[1] + 1.5, BODY_FACE[2] - 7, BODY_FACE[1] + 1.5, 1.6, (255, 253, 244))


def body_template() -> Canvas:
    canvas = Canvas()
    draw_body(canvas)
    return canvas


# ----- dots -----

# Each dot: x, y, radius, colour. Coordinates are in cell units; the face is about x 4..92, y 4..118.
DOT_LAYOUTS: dict[int, list[tuple[float, float, float, Color]]] = {
    1: [(CENTER_X, CENTER_Y, 29, RED)],
    2: [(CENTER_X, 34, 17, GREEN), (CENTER_X, 88, 17, RED)],
    3: [(27, 27, 14, BLUE), (CENTER_X, CENTER_Y, 14, RED), (69, 95, 14, GREEN)],
    4: [(29, 35, 15, BLUE), (67, 35, 15, GREEN), (29, 87, 15, GREEN), (67, 87, 15, BLUE)],
    5: [(27, 28, 13, BLUE), (69, 28, 13, GREEN), (CENTER_X, CENTER_Y, 13, RED), (27, 94, 13, GREEN), (69, 94, 13, BLUE)],
    6: [(30, 27, 13, GREEN), (66, 27, 13, GREEN), (30, 61, 13, RED), (66, 61, 13, RED), (30, 95, 13, RED), (66, 95, 13, RED)],
    7: [(22, 23, 10, GREEN), (CENTER_X, 33, 10, GREEN), (74, 43, 10, GREEN),
        (30, 76, 11, RED), (66, 76, 11, RED), (30, 101, 11, RED), (66, 101, 11, RED)],
    8: [(31, 22, 11, BLUE), (65, 22, 11, BLUE), (31, 47, 11, BLUE), (65, 47, 11, BLUE),
        (31, 75, 11, BLUE), (65, 75, 11, BLUE), (31, 100, 11, BLUE), (65, 100, 11, BLUE)],
    9: [(24, 24, 11, GREEN), (CENTER_X, 24, 11, RED), (72, 24, 11, BLUE),
        (24, CENTER_Y, 11, GREEN), (CENTER_X, CENTER_Y, 11, RED), (72, CENTER_Y, 11, BLUE),
        (24, 98, 11, GREEN), (CENTER_X, 98, 11, RED), (72, 98, 11, BLUE)],
}


def draw_dot(canvas: Canvas, x: float, y: float, radius: float, color: Color) -> None:
    canvas.circle(x, y, radius + 1.2, INK)
    canvas.circle(x, y, radius, color)
    canvas.ring(x, y, radius * 0.72, radius * 0.5, FACE)
    canvas.circle(x, y, radius * 0.34, color)
    if radius > 20:  # the one dot is a flower of rings
        canvas.ring(x, y, radius * 0.92, radius * 0.82, FACE)
        canvas.circle(x, y, radius * 0.34, BLUE)
        canvas.circle(x, y, radius * 0.16, FACE)


def draw_dots(canvas: Canvas, rank: int) -> None:
    for x, y, radius, color in DOT_LAYOUTS[rank]:
        draw_dot(canvas, x, y, radius, color)


# ----- bamboo -----

# Each stick: centre x, centre y, length, body colour, band colour.
def bamboo_sticks(rank: int) -> list[tuple[float, float, float, Color, Color]]:
    columns3 = (24.0, CENTER_X, 72.0)
    if rank == 2:
        return [(CENTER_X, 34, 44, GREEN, RED), (CENTER_X, 88, 44, GREEN, BLUE)]
    if rank == 3:
        return [(CENTER_X, 34, 44, GREEN, RED), (33, 88, 44, GREEN, BLUE), (63, 88, 44, GREEN, BLUE)]
    if rank == 4:
        return [(33, 34, 44, GREEN, RED), (63, 34, 44, GREEN, BLUE), (33, 88, 44, GREEN, BLUE), (63, 88, 44, GREEN, RED)]
    if rank == 5:
        return [(27, 32, 40, GREEN, RED), (69, 32, 40, GREEN, RED), (CENTER_X, CENTER_Y, 40, RED, INK),
                (27, 90, 40, GREEN, BLUE), (69, 90, 40, GREEN, BLUE)]
    if rank == 6:
        return [(x, y, 44, GREEN, RED if y < 60 else BLUE) for x in columns3 for y in (34.0, 88.0)]
    if rank == 7:
        return [(CENTER_X, 22, 32, RED, INK)] + [(x, y, 34, GREEN, RED if y < 80 else BLUE) for x in columns3 for y in (60.0, 100.0)]
    if rank == 8:
        return [(x, y, 44, GREEN, RED if y < 60 else BLUE) for x in (18.0, 38.0, 58.0, 78.0) for y in (34.0, 88.0)]
    return [(x, y, 32, RED if x == CENTER_X else GREEN, INK if x == CENTER_X else (RED, BLUE, RED)[index])
            for x in columns3 for index, y in enumerate((22.0, 61.0, 100.0))]


def draw_stick(canvas: Canvas, x: float, y: float, length: float, body: Color, band: Color) -> None:
    half_width = 7.0
    top = y - length / 2
    bottom = y + length / 2
    canvas.rrect(x - half_width - 1.2, top - 1.2, x + half_width + 1.2, bottom + 1.2, 7.5, INK)
    canvas.rrect(x - half_width, top, x + half_width, bottom, 6.5, body)
    canvas.line(x - 2.8, top + 5, x - 2.8, bottom - 5, 2.2, tuple(min(255, c + 70) for c in body))  # the highlight
    canvas.rect(x - half_width, y - 2.2, x + half_width, y + 2.2, band)
    canvas.rect(x - half_width, y - 3.0, x + half_width, y - 2.2, INK)
    canvas.rect(x - half_width, y + 2.2, x + half_width, y + 3.0, INK)


def draw_bamboo_one(canvas: Canvas) -> None:
    """One tall stalk with knots, a leaf on each side and a red flower on top."""
    x = CENTER_X
    top, bottom = 26.0, 106.0
    canvas.rrect(x - 9.2, top - 1.2, x + 9.2, bottom + 1.2, 9.2, INK)
    canvas.rrect(x - 8, top, x + 8, bottom, 8, GREEN)
    canvas.line(x - 3.5, top + 8, x - 3.5, bottom - 8, 2.6, LIGHT_GREEN)
    for knot in (top + 27, top + 53):
        canvas.rect(x - 8, knot - 3, x + 8, knot + 3, RED)
        canvas.rect(x - 8, knot - 4, x + 8, knot - 3, INK)
        canvas.rect(x - 8, knot + 3, x + 8, knot + 4, INK)
    canvas.polygon([(x + 6, top + 36), (x + 22, top + 20), (x + 38, top + 14), (x + 30, top + 30), (x + 14, top + 42)], DARK_GREEN)
    canvas.polygon([(x - 6, top + 62), (x - 22, top + 46), (x - 38, top + 40), (x - 30, top + 56), (x - 14, top + 68)], DARK_GREEN)
    canvas.circle(x, top - 4, 9, INK)
    canvas.circle(x, top - 4, 7.6, RED)
    canvas.circle(x, top - 4, 3, FACE)


def draw_bamboo(canvas: Canvas, rank: int) -> None:
    if rank == 1:
        draw_bamboo_one(canvas)
        return
    for stick in bamboo_sticks(rank):
        draw_stick(canvas, *stick)


# ----- characters: a big red digit over a black mark of our own design -----

def draw_characters(canvas: Canvas, rank: int) -> None:
    canvas.text(str(rank), CENTER_X, 40, 8.0, RED)
    # The mark: a heavy horizontal stroke, a vertical stroke through it, a hook and two dots. Not a real glyph.
    canvas.line(26, 82, 70, 82, 7, INK)
    canvas.line(CENTER_X, 74, CENTER_X, 108, 6, INK)
    canvas.line(CENTER_X, 108, 38, 104, 5, INK)
    canvas.circle(30, 98, 3.4, INK)
    canvas.circle(66, 98, 3.4, INK)
    canvas.line(60, 92, 72, 104, 4, INK)


# ----- winds, dragons -----

WIND_COLORS: tuple[Color, ...] = ((176, 36, 36), (30, 100, 56), (30, 66, 150), (60, 44, 110))


def draw_wind(canvas: Canvas, index: int) -> None:
    color = WIND_COLORS[index]
    canvas.ring(CENTER_X, CENTER_Y, 39, 34, color)
    canvas.ring(CENTER_X, CENTER_Y, 31, 30, color)
    canvas.text(WIND_LETTERS[index], CENTER_X, CENTER_Y, 6.4, INK)


def draw_dragon(canvas: Canvas, index: int) -> None:
    x, y = CENTER_X, CENTER_Y
    if index == 0:  # Red: a filled red emblem with an ivory cross cut into it
        canvas.rrect(x - 30, y - 38, x + 30, y + 38, 12, INK)
        canvas.rrect(x - 28.5, y - 36.5, x + 28.5, y + 36.5, 11, RED)
        canvas.rect(x - 4, y - 28, x + 4, y + 28, FACE)
        canvas.rect(x - 20, y - 12, x + 20, y - 4, FACE)
        canvas.rect(x - 20, y + 4, x + 20, y + 12, FACE)
    elif index == 1:  # White: an empty blue frame, a thin one inside
        canvas.rrect_frame(x - 31, y - 41, x + 31, y + 41, 12, 6, BLUE)
        canvas.rrect_frame(x - 22, y - 32, x + 22, y + 32, 7, 2, BLUE)
    else:  # Green: a green diamond with an ivory sprout
        canvas.polygon([(x, y - 44), (x + 36, y), (x, y + 44), (x - 36, y)], INK)
        canvas.polygon([(x, y - 41), (x + 33, y), (x, y + 41), (x - 33, y)], GREEN)
        canvas.line(x, y + 24, x, y - 4, 4, FACE)
        canvas.polygon([(x, y - 4), (x + 18, y - 22), (x + 20, y - 2)], FACE)
        canvas.polygon([(x, y + 6), (x - 18, y - 10), (x - 19, y + 10)], FACE)


# ----- flowers and seasons -----

FLOWER_COLORS: tuple[Color, ...] = ((200, 50, 90), (222, 128, 30), (170, 60, 160), (196, 160, 20))
SEASON_COLORS: tuple[Color, ...] = ((60, 160, 70), (210, 90, 40), (150, 110, 60), (60, 120, 190))


def draw_bonus(canvas: Canvas, letter: str, number: int, color: Color) -> None:
    cy = 50.0
    canvas.circle(CENTER_X, cy, 36, INK)
    canvas.circle(CENTER_X, cy, 34.6, color)
    canvas.ring(CENTER_X, cy, 30, 28.6, FACE)
    canvas.text(letter + str(number), CENTER_X, cy, 4.8, FACE, 1.6)
    for index in range(number):  # as many dots as the number: readable at a glance
        canvas.circle(CENTER_X + (index - (number - 1) / 2) * 15, 102, 5.2, INK)
        canvas.circle(CENTER_X + (index - (number - 1) / 2) * 15, 102, 3.8, color)


# ----- the back -----

def draw_back(canvas: Canvas) -> None:
    draw_body(canvas, BACK_FACE)
    canvas.rrect_frame(BODY_FACE[0] + 5, BODY_FACE[1] + 5, BODY_FACE[2] - 5, BODY_FACE[3] - 5, 5, 2, BACK_RIM)
    # a lattice of diamonds in the middle
    for x in (FACE_CENTER_X - 22, FACE_CENTER_X, FACE_CENTER_X + 22):
        for y in (FACE_CENTER_Y - 33, FACE_CENTER_Y - 11, FACE_CENTER_Y + 11, FACE_CENTER_Y + 33):
            canvas.polygon([(x, y - 8), (x + 8, y), (x, y + 8), (x - 8, y)], BACK_DARK)


# ----- the wall stack, seen from above -----

# Two tiles side by side, their teal backs up: an ivory edge shows on the right and at the bottom, a soft shade line lies between them.
STACK_EDGE = 12.0
STACK_GAP = 4.0


def draw_stack(canvas: Canvas) -> None:
    size = float(STACK_SIZE)
    canvas.rrect(0.0, 0.0, size, size, 16.0, OUTLINE)
    canvas.rrect(1.5, 1.5, size - 1.5, size - 1.5, 14.5, SIDE_SHADE)
    canvas.rrect(1.5, 1.5, size - 1.5 - 3.0, size - 1.5 - 3.0, 14.5, SIDE)
    canvas.rrect(1.5, 1.5, size - STACK_EDGE + 4.0, size - STACK_EDGE + 4.0, 14.5, SIDE_LIGHT)
    top = (4.0, 4.0, size - STACK_EDGE, size - STACK_EDGE)
    middle = (top[0] + top[2]) / 2
    canvas.rrect(top[0], top[1], middle - STACK_GAP / 2, top[3], 9.0, BACK_FACE)
    canvas.rrect(middle + STACK_GAP / 2, top[1], top[2], top[3], 9.0, BACK_FACE)
    canvas.line(middle, top[1] + 2.0, middle, top[3] - 2.0, STACK_GAP, BACK_DARK)
    for x0, x1 in ((top[0], middle - STACK_GAP / 2), (middle + STACK_GAP / 2, top[2])):
        canvas.rrect_frame(x0 + 4.0, top[1] + 4.0, x1 - 4.0, top[3] - 4.0, 5.0, 2.0, BACK_RIM)
        canvas.line(x0 + 9.0, top[1] + 1.8, x1 - 9.0, top[1] + 1.8, 1.6, BACK_LIGHT)


# ----- the atlas -----

def draw_pattern(canvas: Canvas, tile_id: int) -> None:
    if tile_id < 9:
        draw_dots(canvas, tile_id + 1)
    elif tile_id < 18:
        draw_bamboo(canvas, tile_id - 9 + 1)
    elif tile_id < 27:
        draw_characters(canvas, tile_id - 18 + 1)
    elif tile_id < 31:
        draw_wind(canvas, tile_id - 27)
    elif tile_id < 34:
        draw_dragon(canvas, tile_id - 31)
    elif tile_id < 38:
        draw_bonus(canvas, "F", tile_id - 34 + 1, FLOWER_COLORS[tile_id - 34])
    else:
        draw_bonus(canvas, "T", tile_id - 38 + 1, SEASON_COLORS[tile_id - 38])


# ----- the corner index -----

def is_light(color: Color) -> bool:
    return 0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2] > 120


def index_glyph(canvas: Canvas, character: str, top: float, color: Color) -> None:
    canvas.glyph(character, INDEX_X, top, INDEX_PIXEL, color, INK if is_light(color) else None)


def sign_ring(canvas: Canvas) -> None:
    """The sign of the dots: a ring with an ivory hole."""
    cy = INDEX_SIGN_TOP + INDEX_SIGN_HEIGHT / 2
    radius = INDEX_SIGN_HEIGHT / 2
    canvas.circle(INDEX_CENTER_X, cy, radius + INDEX_OUTLINE, INK)
    canvas.circle(INDEX_CENTER_X, cy, radius, BLUE)
    canvas.circle(INDEX_CENTER_X, cy, radius * 0.42, FACE)


def sign_stick(canvas: Canvas) -> None:
    """The sign of the bamboo: a short green stick with a red band."""
    top = INDEX_SIGN_TOP
    bottom = INDEX_SIGN_TOP + INDEX_SIGN_HEIGHT
    half = 4.6
    x = INDEX_CENTER_X
    canvas.rrect(x - half - INDEX_OUTLINE, top - INDEX_OUTLINE, x + half + INDEX_OUTLINE, bottom + INDEX_OUTLINE, half + 1.0, INK)
    canvas.rrect(x - half, top, x + half, bottom, half, GREEN)
    middle = (top + bottom) / 2
    canvas.rect(x - half, middle - 2.0, x + half, middle + 2.0, RED)


def sign_character(canvas: Canvas) -> None:
    """The sign of the characters: a small mark of our own (heavy stroke, vertical stroke, a dot), not a real glyph."""
    top = INDEX_SIGN_TOP
    bottom = INDEX_SIGN_TOP + INDEX_SIGN_HEIGHT
    x = INDEX_CENTER_X
    y = top + 5.0
    canvas.line(x - 7.5, y, x + 7.5, y, 4.0, RED)
    canvas.line(x, top, x, bottom - 1.5, 3.4, RED)
    canvas.line(x, bottom - 1.5, x - 5.5, bottom - 4.0, 2.8, RED)
    canvas.circle(x + 5.0, bottom - 3.4, 2.4, RED)


def sign_frame(canvas: Canvas) -> None:
    """The White dragon: a hollow blue frame in the place of a letter (a W would be taken for the West wind)."""
    top = INDEX_Y + 3.0
    canvas.rrect_frame(INDEX_X, top, INDEX_X + INDEX_GLYPH_W, top + INDEX_GLYPH_W, 2.0, 3.6, BLUE)


def draw_index(canvas: Canvas, tile_id: int) -> None:
    """The corner index of a face, in cell coordinates (the view is the identity)."""
    name = notation(tile_id)
    if tile_id < 27:
        index_glyph(canvas, name[0], INDEX_Y, INK)
        (sign_ring, sign_stick, sign_character)[tile_id // 9](canvas)
    elif tile_id < 31:
        index_glyph(canvas, name[0], INDEX_Y, WIND_COLORS[tile_id - 27])
    elif tile_id == 32:
        sign_frame(canvas)
    elif tile_id < 34:
        index_glyph(canvas, name[0], INDEX_Y, (RED, BLUE, GREEN)[tile_id - 31])
    else:
        color = FLOWER_COLORS[tile_id - 34] if tile_id < 38 else SEASON_COLORS[tile_id - 38]
        index_glyph(canvas, name[1], INDEX_Y, INK)
        index_glyph(canvas, name[0], INDEX_SIGN_TOP, color)


def art_view() -> tuple[float, float, float]:
    """The view of the patterns: scaled toward the bottom right corner of the face, which leaves the top left corner free."""
    return (ART_SCALE, BODY_FACE[2] - ART_MARGIN[0] - ART_SCALE * ART_EXTENT[0],
            BODY_FACE[3] - ART_MARGIN[1] - ART_SCALE * ART_EXTENT[1])


def draw_face(tile_id: int, template: Canvas, with_index: bool = True) -> Canvas:
    canvas = template.copy()
    canvas.set_view(*art_view())
    draw_pattern(canvas, tile_id)
    canvas.reset_view()
    if with_index:
        draw_index(canvas, tile_id)
    return canvas


def render_cells() -> list[bytearray]:
    template = body_template()
    cells = [draw_face(tile_id, template).downsample() for tile_id in range(TILE_ID_COUNT)]
    back = Canvas()
    draw_back(back)
    cells.append(back.downsample())
    stack = Canvas()
    draw_stack(stack)
    cells.append(stack.downsample())
    return cells


def assemble(cells: list[bytearray]) -> list[bytes]:
    """The atlas as rows of RGBA bytes; the cells that are not used stay transparent."""
    rows = [bytearray(ATLAS_W * 4) for _ in range(ATLAS_H)]
    for index, cell in enumerate(cells):
        x0, y0 = cell_origin(index)
        for y in range(CELL_H):
            rows[y0 + y][x0 * 4 : (x0 + CELL_W) * 4] = cell[y * CELL_W * 4 : (y + 1) * CELL_W * 4]
    return [bytes(row) for row in rows]


# ----- PNG -----

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def encode_png(rows: list[bytes]) -> bytes:
    raw = b"".join(b"\x00" + row for row in rows)  # filter type 0 on every row
    header = struct.pack(">IIBBBBB", len(rows[0]) // 4, len(rows), 8, 6, 0, 0, 0)
    return PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def decode_png(data: bytes) -> list[bytes]:
    """Reads a PNG written by `encode_png` (8 bit RGBA, no interlace, filter 0); anything else is an error."""
    if data[:8] != PNG_SIGNATURE:
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


# ----- TextureLibrary.xml -----

def sprite_names() -> list[str]:
    return [SPRITE_PREFIX + notation(tile_id) for tile_id in range(TILE_ID_COUNT)] + [BACK_SPRITE, STACK_SPRITE]


def library_xml() -> str:
    """The whole library: the tile sprites, then the icon sprites of tools/make_icons.py (which draws them; this file is the one writer)."""
    import make_icons  # not at the top: make_icons imports this module for its Canvas

    lines = [
        '<?xml version="1.0"?>',
        "<!-- Generated by tools/make_tiles.py (the tiles) and tools/make_icons.py (the Icon_ sprites): change the scripts and run make_tiles.py again, not this file. -->",
        "<TextureLibrary>",
        "  <sprites>",
    ]
    entries = []
    for index, name in enumerate(sprite_names()):
        x, y = cell_origin(index)
        width, height = (STACK_SIZE, STACK_SIZE) if index == STACK_INDEX else (CELL_W, CELL_H)
        entries.append((name, TEXTURE_PATH, x, y, width, height))
    for name, texture_path, x, y, width, height in entries + make_icons.sprite_entries():
        lines.append(f'    <sprite name="{name}" texturePath="{texture_path}">')
        lines.append(f'      <srcRect x="{x}" y="{y}" w="{width}" h="{height}" />')
        lines.append("    </sprite>")
    lines.append("  </sprites>")
    lines.append("</TextureLibrary>")
    return "\n".join(lines) + "\n"


# ----- the check -----

def cell_bytes(rows: list[bytes], index: int) -> bytes:
    x0, y0 = cell_origin(index)
    return b"".join(rows[y0 + y][x0 * 4 : (x0 + CELL_W) * 4] for y in range(CELL_H))


def alpha_at(rows: list[bytes], x: int, y: int) -> int:
    return rows[y][x * 4 + 3]


def problems_of(rows: list[bytes]) -> list[str]:
    """What is wrong with an atlas: equal cells, a face that is not opaque in the middle, a tile or the stack that touches a corner of its
    cell, a stack that is not opaque inside its square or paints outside it, stray pixels."""
    problems = []
    cells = [cell_bytes(rows, index) for index in range(CELL_COUNT)]
    names = sprite_names()
    for first in range(CELL_COUNT):
        for second in range(first + 1, CELL_COUNT):
            if cells[first] == cells[second]:
                problems.append(f"{names[first]} and {names[second]} are the same picture")
    for index in range(CELL_COUNT):
        x0, y0 = cell_origin(index)
        middle = [alpha_at(rows, x0 + CELL_W // 2 + dx, y0 + CELL_H // 2 + dy) for dx in range(-2, 2) for dy in range(-2, 2)]
        if min(middle) != 255:
            problems.append(f"{names[index]} is not opaque in the middle")
        if alpha_at(rows, x0, y0) != 0 or alpha_at(rows, x0 + CELL_W - 1, y0 + CELL_H - 1) != 0:
            problems.append(f"{names[index]} has no transparent corners")
    x0, y0 = cell_origin(STACK_INDEX)
    inset = 6  # the corners of the square are rounded; this far in from each corner the stack is solid
    for dx, dy in ((inset, inset), (STACK_SIZE - 1 - inset, inset), (inset, STACK_SIZE - 1 - inset), (STACK_SIZE - 1 - inset, STACK_SIZE - 1 - inset)):
        if alpha_at(rows, x0 + dx, y0 + dy) != 255:
            problems.append(f"{names[STACK_INDEX]} is not opaque inside its square at ({dx}, {dy})")
    if any(alpha_at(rows, x0 + x, y0 + y) for y in range(CELL_H) for x in range(CELL_W) if x >= STACK_SIZE or y >= STACK_SIZE):
        problems.append(f"{names[STACK_INDEX]} paints outside its {STACK_SIZE} x {STACK_SIZE} square")
    for index in range(CELL_COUNT, COLUMNS * ROWS):
        if any(cell_bytes(rows, index)[3::4]):
            problems.append(f"the unused cell {index} is not transparent")
    return problems


def box_of(cell: bytes) -> bytes:
    """The pixels of INDEX_BOX in a cell of CELL_W x CELL_H RGBA pixels."""
    x0, y0, x1, y1 = INDEX_BOX
    return b"".join(bytes(cell[(y * CELL_W + x0) * 4 : (y * CELL_W + x1) * 4]) for y in range(y0, y1))


def index_problems(rows: list[bytes]) -> list[str]:
    """Every face has index pixels in INDEX_BOX, and the art alone (rendered with its view, without the index) leaves the box untouched."""
    problems = []
    template = body_template()
    plain = box_of(template.downsample())
    names = sprite_names()
    for tile_id in range(TILE_ID_COUNT):
        if box_of(cell_bytes(rows, tile_id)) == plain:
            problems.append(f"{names[tile_id]} has no index")
        if box_of(draw_face(tile_id, template, with_index=False).downsample()) != plain:
            problems.append(f"{names[tile_id]} has art under the index")
    return problems


def check() -> int:
    expected_rows = assemble(render_cells())
    failures = problems_of(expected_rows) + index_problems(expected_rows)
    if not PNG_PATH.is_file():
        failures.append(f"{PNG_PATH} is missing")
    else:
        try:
            if decode_png(PNG_PATH.read_bytes()) != expected_rows:
                failures.append(f"{PNG_PATH.relative_to(ROOT)} differs from what the generator makes now")
        except (ValueError, zlib.error, struct.error) as error:
            failures.append(f"{PNG_PATH.relative_to(ROOT)}: {error}")
    if not XML_PATH.is_file():
        failures.append(f"{XML_PATH} is missing")
    elif XML_PATH.read_text(encoding="utf-8") != library_xml():
        failures.append(f"{XML_PATH.relative_to(ROOT)} differs from what the generator makes now")
    for failure in failures:
        print("make_tiles: " + failure, file=sys.stderr)
    if not failures:
        print(f"make_tiles: {CELL_COUNT} cells, {PNG_PATH.relative_to(ROOT)} and {XML_PATH.relative_to(ROOT)} are up to date")
    return 1 if failures else 0


def write() -> int:
    rows = assemble(render_cells())
    problems = problems_of(rows) + index_problems(rows)
    if problems:
        for problem in problems:
            print("make_tiles: " + problem, file=sys.stderr)
        return 1
    PNG_PATH.parent.mkdir(parents=True, exist_ok=True)
    PNG_PATH.write_bytes(encode_png(rows))
    XML_PATH.write_text(library_xml(), encoding="utf-8", newline="\n")
    print(f"atlas {ATLAS_W}x{ATLAS_H}, {COLUMNS} columns x {ROWS} rows of cells {CELL_W}x{CELL_H}")
    for index, name in enumerate(sprite_names()):
        x, y = cell_origin(index)
        print(f"  cell {index:2d}  x={x:3d} y={y:3d}  {name}")
    print(f"wrote {PNG_PATH.relative_to(ROOT)} ({PNG_PATH.stat().st_size} bytes) and {XML_PATH.relative_to(ROOT)}")
    return 0


# ----- the preview -----

PREVIEW_FELT: Color = (30, 90, 60)
PREVIEW_SIZES = ((CELL_W, CELL_H), (45, 60), (30, 40), (24, 32))  # native, the viewer's hand, the viewer's meld, a discard
PREVIEW_GAP = 8


def box_filter(cell: bytes, width: int, height: int) -> list[bytes]:
    """The cell at width x height pixels by averaging the covered source area, over the felt colour; rows of RGB bytes."""

    def weights(count: int, size: int) -> list[list[tuple[int, float]]]:
        step = size / count
        result = []
        for out in range(count):
            low, high = out * step, (out + 1) * step
            covered = [(i, min(high, i + 1) - max(low, i)) for i in range(int(low), min(size, int(high) + 1))]
            result.append([(i, weight) for i, weight in covered if weight > 0])
        return result

    columns = weights(width, CELL_W)
    lines = weights(height, CELL_H)
    rows = []
    for out_y in range(height):
        row = bytearray()
        for out_x in range(width):
            red = green = blue = alpha = total = 0.0
            for sy, wy in lines[out_y]:
                for sx, wx in columns[out_x]:
                    weight = wx * wy
                    a = cell[(sy * CELL_W + sx) * 4 + 3] / 255
                    red += cell[(sy * CELL_W + sx) * 4] * a * weight
                    green += cell[(sy * CELL_W + sx) * 4 + 1] * a * weight
                    blue += cell[(sy * CELL_W + sx) * 4 + 2] * a * weight
                    alpha += a * weight
                    total += weight
            coverage = alpha / total
            for value, felt in ((red, PREVIEW_FELT[0]), (green, PREVIEW_FELT[1]), (blue, PREVIEW_FELT[2])):
                row.append(min(255, round(value / total + felt * (1 - coverage))))
        rows.append(bytes(row))
    return rows


def preview(out: Path) -> int:
    """A contact sheet: all cells at the four sizes of PREVIEW_SIZES, one block of COLUMNS columns per size, on the felt."""
    out = out.expanduser().resolve()
    if out.is_relative_to(ROOT):
        print(f"make_tiles: the preview must not be written inside the repository ({ROOT}); it is never committed", file=sys.stderr)
        return 1
    rows_atlas = assemble(render_cells())
    cells = [cell_bytes(rows_atlas, index) for index in range(CELL_COUNT)]
    block_rows = (CELL_COUNT + COLUMNS - 1) // COLUMNS
    sheet_width = COLUMNS * (CELL_W + PREVIEW_GAP) + PREVIEW_GAP
    sheet: list[bytearray] = []
    for width, height in PREVIEW_SIZES:
        top = len(sheet)
        sheet.extend(bytearray(PREVIEW_FELT * sheet_width) for _ in range(PREVIEW_GAP + block_rows * (height + PREVIEW_GAP)))
        for index, cell in enumerate(cells):
            x = PREVIEW_GAP + (index % COLUMNS) * (CELL_W + PREVIEW_GAP) + (CELL_W - width) // 2
            y = top + PREVIEW_GAP + (index // COLUMNS) * (height + PREVIEW_GAP)
            for dy, line in enumerate(box_filter(cell, width, height)):
                sheet[y + dy][x * 3 : (x + width) * 3] = line
    rgba = [bytes(b for i in range(0, len(row), 3) for b in (row[i], row[i + 1], row[i + 2], 255)) for row in sheet]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(encode_png(rgba))
    print(f"wrote {out} ({sheet_width}x{len(sheet)}, sizes {PREVIEW_SIZES})")
    return 0


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Draws the Mahjong tile atlas and writes the sprite library.")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--check", action="store_true", help="exit 0 when the committed files are what the generator makes now")
    modes.add_argument("--preview", metavar="OUT.png",
                        help="write a contact sheet of every cell at four sizes (OUT must be outside the repository)")
    arguments = parser.parse_args(argv)
    if arguments.preview:
        return preview(Path(arguments.preview))
    return check() if arguments.check else write()


if __name__ == "__main__":
    sys.exit(main())
