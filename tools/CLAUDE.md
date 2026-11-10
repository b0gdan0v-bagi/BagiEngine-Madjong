# tools/

- `arena/main.cpp` — `BagiMahjongArena`, see `src/Mahjong/Arena/CLAUDE.md` and `docs/bot-arena.md`.
- `ui_shots.py` — seven UI scenes (`01-fresh-table` ... `07-options`) at 1920 x 1080 and scaled to 640, from `BagiMahjong --headless --control --manual-clock`. `--exe`, `--label`, `--out` (a folder outside the repository; the tool refuses one inside a git work tree). Pictures are never committed.
- `make_tiles.py` — the tile art (below).

## Tile art

Our own art (owner decision: no third-party tile set, no font file). The script is a small software rasterizer with a 5x7 bitmap font of our own and one function per face; the art *is* the script.

- `python3 tools/make_tiles.py` writes `assets/tiles/tiles.png` and `config/TextureLibrary.xml`. **Run `--check` after every change** (compares decoded pixels and the XML, and checks the cells: all different, opaque middle, transparent corners, an index on every face and no art under it).
- `--preview OUT.png` (outside the repository) writes a contact sheet at four sizes for judging readability.
- Atlas: 8 x 6 cells of 96 x 128, cell = tile id, cell 42 the back. `CELL_W`, `CELL_H`, `COLUMNS` are the one place of the geometry and must match `src/Mahjong/Table/TileAtlas.h` (`TileSpritesTest`).
- Every face but the back has a corner index (`INDEX_PIXEL`, `INDEX_X`, `INDEX_Y`, `INDEX_BOX`) so a tile reads at discard size (24 x 32); the art is drawn scaled by `ART_SCALE` to leave room for it. The White dragon's index is a hollow frame (a `W` would read as West).
