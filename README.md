# BagiEngine-Madjong

Classic Chinese mahjong for four seats on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine) (the submodule `engine/`, branch `games/mahjong`). Play alone against three computers (levels 1..10) or hot seat at one screen, on desktop or in a browser; external clients play through the control API (`engine/tools/control`). The tile art is drawn by a script of this repository.

## Principles

- **Zero allocations in a steady frame:** a frame in which nothing changed allocates nothing (`MahjongTableAllocTest`).
- **No peeking:** computer players, hints and the control API see only what their seat could see.
- **One seed, one match on every platform:** the rules draw only from their own seeded generator.

## Get it

```bash
git clone --recurse-submodules <url> BagiEngine-Madjong
cd BagiEngine-Madjong
CI/setup_venv.sh          # Windows: CI\setup_venv.bat or .ps1; creates engine/.venv, needs uv
```

The commit identity is local: set `user.email` / `user.name` in every new clone, both here and in `engine/` (`git -C engine config ...`), which has its own config.

## Build and test

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"     # the reflection generator needs jinja2
cmake --preset macos-debug                    # the engine's presets; list: cmake --list-presets
cmake --build --preset macos-debug
ctest --preset macos-debug                    # MahjongTests and the headless control API smoke
build/ClangmacOS/BagiMahjong
```

- **Bot arena** (Release, `docs/bot-arena.md`): measures the computer players over thousands of seeds.
- **EV analyzer** (Release, `docs/ev-analyzer.md`): the expected points of every legal action of one position.
- **Browser** (Emscripten, `docs/web.md`): build, smoke, deploy. No saves, match log or control API there.
- **Tile art:** `python3 tools/make_tiles.py` (`--check` tells whether the committed atlas is up to date).

## How to play

Pick `Person` or `Computer` for each seat, then `Start`. Click a tile of your hand to discard it; everything else (`Mahjong!`, `Pung`, `Kong`, `Chow`, `Add to pung`, `Pass`, the `Ready hand` switch) is a button that shows only when it is legal. **Hints** show the tiles your waiting hand needs and what a computer would do in your place; they never play for you. The match and the seat choice are saved as you play (`saves/`), so the game offers `Continue`.

## Layout

| Path | What |
|------|------|
| `src/Mahjong/` | the game: rules and computer players (`Game/`), pure scene logic (`Table/`), scene components (`Components/`), saves, arena, analyzer |
| `config/`, `assets/` | the scene, one file per node, the strings in two languages, the tile atlas |
| `tests/` | gtest cases and the control API smoke |
| `tools/`, `docs/`, `web/`, `CI/` | tile art and screenshots, the docs above, the web app, setup and the VS Code workspace |
| `engine/` | the engine; changes go to its branch `games/mahjong`, then the pointer moves here |

Known problems and ideas live in the todo store, not in git. The contracts of the code: `CLAUDE.md` and the nested ones.
