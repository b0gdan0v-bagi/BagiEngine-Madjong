# tools/

- `arena/main.cpp` — `BagiMahjongArena`, see `src/Mahjong/Arena/CLAUDE.md` and `docs/bot-arena.md`.
- `ui_shots.py` — six UI scenes (`01-fresh-table` ... `06-handoff`) at 1920 x 1080 and scaled to 640, from `BagiMahjong --headless --control --manual-clock`. `--exe`, `--label`, `--out` (a folder outside the repository; the tool refuses one inside a git work tree). Pictures are never committed.
- `make_tiles.py` — the tile art (below).
- `web_smoke.py` — the browser smoke of the packaged page (`engine/tools/web/package.py --game BagiMahjong`). Needs Playwright (venv `build/web-venv`, `engine/tools/web/CLAUDE.md`, "The browser smoke") and a browser (`--browser-channel chrome` needs no download). `python tools/web_smoke.py --dist dist --browser-channel chrome --expect-console "Initialized with \d+ worker threads"` (`dist-single` with `"single-thread mode"`); exit 0 / 1 / 77 / 2 as the engine's. It runs the engine's checks 1 to 5 with the click on `Start`, then a second page load: Start, wait until the table stands still, click the first tile of the hand, the picture must change. East is a dice draw, so the person may face a claim window and not be first to act: the scenario presses the places of the action buttons (`CLAIM_ANSWER_CLICKS`) when a tile click does nothing and clicks the tile again; no outcome of the first move is assumed. The coordinates are for a 1280 x 720 window (`--start-click`, `--tile-click` replace them); **when Setup changes, re-check `START_CLICK`** (`mahjong-setup-options` moves it, whoever merges second). `--check` runs the browser-free helpers; `--tile-click 5,5` must exit 1.

## Tile art

Our own art (owner decision: no third-party tile set, no font file). The script is a small software rasterizer with a 5x7 bitmap font of our own and one function per face; the art *is* the script.

- `python3 tools/make_tiles.py` writes `assets/tiles/tiles.png` and `config/TextureLibrary.xml`. **Run `--check` after every change** (compares decoded pixels and the XML, and checks the cells: all different, opaque middle, transparent corners, an index on every face and no art under it).
- `--preview OUT.png` (outside the repository) writes a contact sheet at four sizes for judging readability.
- Atlas: 8 x 6 cells of 96 x 128, cell = tile id, cell 42 the back. `CELL_W`, `CELL_H`, `COLUMNS` are the one place of the geometry and must match `src/Mahjong/Table/TileAtlas.h` (`TileSpritesTest`).
- Every face but the back has a corner index (`INDEX_PIXEL`, `INDEX_X`, `INDEX_Y`, `INDEX_BOX`) so a tile reads at discard size (24 x 32); the art is drawn scaled by `ART_SCALE` to leave room for it. The White dragon's index is a hollow frame (a `W` would read as West).
