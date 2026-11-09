# BagiEngine-Madjong

Mahjong on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine). The engine is the git submodule `engine/`, on the permanent branch `games/mahjong` of the engine repository. It is classic Chinese mahjong for four seats: the rules (the hand, the three kongs, robbing a kong, scoring and payments, a match of 16 or 8 hands), computer players, a control API for external clients (`engine/tools/control`) and a table on screen that you play with the mouse, alone against three computers or hot seat at one screen. The tile art is drawn by a script in this repository.

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
- **Everything else is a button** in the lower right of the table, with a plain word and the tiles it is about drawn on it: `Mahjong!` (a complete hand, or the tile that completes it), `Pung`, `Kong`, `Chow` (one button per sequence you can make, with its three tiles), `Kong` (a concealed kong, with its tile), `Add to pung` (a fourth tile to an open pung) and `Pass`. Only the buttons that are legal now are shown. Hold the mouse over a button and a line above the buttons says what it does (a touch screen has no hover, so no line there). `Ready hand` is a switch: turn it on and the next tile you click is discarded as the declaration of the calling hand (the tiles that cannot declare are dimmed).
- **Hints** (on by default, switch `Hints` in the seat choice): inside the ring of the wall a row shows the tiles your waiting hand needs and, under each, how many you cannot see; on your turn a gold bar under a tile marks the discards that declare the ready hand; the `Hint` button asks what a computer would do in your place (a discard, a claim or a pass) and frames the tile it would discard. A hint only tells, it never plays for you.
- **Computers** play one step every 0.6 seconds (`kComputerStepSeconds`); nothing moves while the seat choice is open.
- **Hot seat:** with two or more people at one screen the table is covered between turns ("Pass the screen to Seat 2 (South)"); the next person presses `Show`.
- **The end of a hand:** a score table covers the table: who won and how, the points of every seat in the hand, what each seat paid or got and its total, the winner's lines (`Pung of 5 Dots: 8`, `One suit, no honours: x8`) and the sum. All four hands are face up. `Next hand` settles it and deals on (`Finish match` on the last one); after the last hand the standings show the seats by points and `New game` opens the seat choice.
- The HUD (top left of the table) shows the round and the hand number and the tiles left in the wall. Each seat has a plate at its side of the table with its number, its wind (a `*` and a red bar on the plate of East, the dealer) and its score; the plate of the seat whose turn it is is gold.

## Layout

| Path | What |
|------|------|
| `src/Mahjong/` | the game: `MahjongGame` and its control adapter, the rules and the computer players (`Game/`), the pure side of the scene (`Table/`: layout, tile atlas cells, action buttons, the words of the score table) and the scene components (`Components/`: the table, the HUD). `CLAUDE.md` describes it |
| `config/`, `assets/` | scene (`SceneConfig.xml` with the references and one file per node in `config/nodes/`), application config, the tile sprite library; the tile atlas (`assets/tiles/tiles.png`) |
| `tools/` | `make_tiles.py`, the generator of our own tile art (`config/TextureLibrary.xml` and the atlas are its output) |
| `tests/` | gtest cases and `control_smoke.py`, the smoke test of the control API (plays a hand and a match over WebSocket) |
| `todo/`, `TODO.md` | known problems and ideas live in the todo store (area `mahjong`), not in git (`todo/README.md`) |
| `CI/` | environment setup (`setup_venv.*`) and the VS Code workspace `BagiMahjong.code-workspace` (CMake Tools on the engine's presets, CodeLLDB / MSVC launch, build tasks) |
| `engine/` | the engine (submodule); not edited from here |

## The engine

A change the game needs from the engine is made in the engine's own checkout, on `games/mahjong`, and then the pointer here is moved in a commit of its own: `git submodule update --remote engine`. Details: `.claude/rules/engine-and-todo.md`.
