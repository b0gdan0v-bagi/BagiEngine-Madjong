# BagiEngine-Madjong

Mahjong on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine). The engine is the git submodule `engine/`, on the permanent branch `games/mahjong` of the engine repository. So far it is a skeleton: the game is registered and opens its scene, the rules are still to come.

## Get it

```bash
git clone --recurse-submodules <url> BagiEngine-Madjong
cd BagiEngine-Madjong
CI/setup_venv.sh          # Windows: CI\setup_venv.bat or .ps1; creates engine/.venv, needs uv
```

An existing clone: `git submodule update --init --recursive`.

**The identity of the commits is local and not versioned.** Every new clone sets it again, in the game and in the submodule (engine work is committed from inside `engine/`, which has its own config), with the email and the name this repository is committed with:

```bash
git config user.email "<email>"
git config user.name  "<name>"
git -C engine config user.email "<email>"
git -C engine config user.name  "<name>"

git config --show-origin user.email             # must show file:.git/config
git -C engine config --show-origin user.email   # must show file:.../.git/modules/engine/config
```

## Build and test

The presets are the engine's (`CMakePresets.json` includes `engine/CMakePresets.json`):

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"     # the reflection generator needs jinja2
cmake --preset macos-debug                    # list: cmake --list-presets
cmake --build --preset macos-debug
ctest --preset macos-debug                    # MahjongTests and BagiMahjongControlSmoke (needs BagiMahjong built; headless, no window)
build/ClangmacOS/BagiMahjong                  # the game; the executable folder depends on compiler and OS
```

The tile art is drawn by a script (standard library only): `python3 tools/make_tiles.py` regenerates `assets/tiles/tiles.png` and `config/TextureLibrary.xml`, `python3 tools/make_tiles.py --check` tells whether the committed files are up to date.

## How to play

The game opens on **New game: who plays?**: for each of the four seats pick `Person` or `Computer` (the default is you at seat 1 against three computers), then `Start`. The `New game` button in the top right corner of the table opens the question again and starts a new match.

- **Discard:** click a tile of your hand (the bottom row). Your hand is the only one face up; the others show backs until the hand is over.
- **Everything else is a button** in the lower right of the table: `Win`, `Mahjong`, `Pung`, `Kong`, `Chow 4-6p` (one button per sequence you can make), `Kong 5p` (a concealed kong), `Add 5p` (a fourth tile to an open pung) and `Pass`. Only the buttons that are legal now are shown. `Declare` is a switch: turn it on and the next tile you click is discarded as the declaration of the calling hand (the tiles that cannot declare are dimmed).
- **Computers** play one step every 0.6 seconds (`kComputerStepSeconds`); nothing moves while the seat choice is open.
- **Hot seat:** with two or more people at one screen the table is covered between turns ("Pass the screen to Seat 2 (South)"); the next person presses `Show`.
- The HUD (top left and left of the table) shows the round and the hand number, the tiles left in the wall, whose turn it is and the four scores (`>` the seat whose turn it is, `*` East).

## Layout

| Path | What |
|------|------|
| `src/Mahjong/` | the game: `MahjongGame` now, the rules (`Game/`) and the scene components (`Components/`) to come. `CLAUDE.md` describes it |
| `config/`, `assets/` | scene, application config, the tile sprite library; the tile atlas (`assets/tiles/tiles.png`) |
| `tools/` | `make_tiles.py`, the generator of our own tile art (`config/TextureLibrary.xml` and the atlas are its output) |
| `tests/` | gtest cases and `control_smoke.py`, the smoke test of the control API (plays a hand and a match over WebSocket) |
| `todo/`, `TODO.md` | known problems and ideas live in the todo store (area `mahjong`), not in git (`todo/README.md`) |
| `CI/` | environment setup (`setup_venv.*`) and the VS Code workspace `BagiMahjong.code-workspace` (CMake Tools on the engine's presets, CodeLLDB / MSVC launch, build tasks) |
| `engine/` | the engine (submodule); not edited from here |

## The engine

A change the game needs from the engine is made in the engine's own checkout, on `games/mahjong`, and then the pointer here is moved in a commit of its own: `git submodule update --remote engine`. Details: `.claude/rules/engine-and-todo.md`.
