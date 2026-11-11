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

The computer players are measured by the bot arena, a program with no window (Release, `docs/bot-arena.md`): `cmake --preset macos-release && cmake --build --preset macos-release --target BagiMahjongArena`, then `build/ClangmacOS/BagiMahjongArena --focus normal --others random --seeds 1000` (it overwrites the Debug executables in `build/ClangmacOS/`; build `macos-debug` again before `ctest`).

One decision is measured by the EV analyzer (Release, `docs/ev-analyzer.md`): `cmake --build --preset macos-release --target BagiMahjongAnalyzer`, then `build/macos-release/bin/BagiMahjongAnalyzer --log logs/matches/<file>.jsonl --hand 1 --seat 0 --jobs 0` prints, for every legal action of a seat, the expected points with intervals.

The tile art is drawn by a script (standard library only): `python3 tools/make_tiles.py` regenerates `assets/tiles/tiles.png` and `config/TextureLibrary.xml`, `python3 tools/make_tiles.py --check` tells whether the committed files are up to date.

## In a browser

The game also builds as a web page (Emscripten, WebGL). The SDK is installed once into the submodule (about 2 GB); use Python 3.14 for it, the Homebrew 3.15 and 3.12 give an empty `platform.mac_ver()` on this Mac:

```bash
python3.14 engine/CI/scripts/setup_emsdk.py
cmake --preset web-release                    # threaded; output in build/web-release/bin
cmake --build --preset web-release --target BagiMahjong
python3 engine/tools/web/package.py build/web-release/bin dist --game BagiMahjong --app web/app.json   # dist/ is git-ignored
python3 engine/tools/web/serve.py --root dist                                  # http://localhost:8080
```

Open `localhost` or an https address: the threaded page needs cross-origin isolation, which a plain `http://` host never has. The single-thread page needs no headers: the preset `web-release-single-thread`, `package.py build/web-release-single-thread/bin dist-single --game BagiMahjong --app web/app.json --single-thread`, `serve.py --root dist-single`. In a worker folder, which has no SDK of its own, the same presets exist as `-local` ones in the git-ignored `CMakeUserPresets.json`.

The browser smoke (`tools/web_smoke.py`: the engine's smoke, then `Start` and a tile click on a 1280x720 page) needs Playwright in a venv:

```bash
python3.14 -m venv build/web-venv && build/web-venv/bin/python -m pip install playwright
build/web-venv/bin/python -m playwright install chromium                       # or --browser-channel chrome with the installed Chrome
build/web-venv/bin/python tools/web_smoke.py --dist dist --expect-console "Initialized with \d+ worker threads"
build/web-venv/bin/python tools/web_smoke.py --dist dist-single --expect-console "single-thread mode"
```

Without a browser, the web-like desktop build tests the same code paths: `cmake -S . -B build/webcheck -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DENGINE_WITH_VULKAN=OFF -DENGINE_WITH_REMOTE_CONTROL=OFF -DENGINE_STATIC_SDL=ON -DENGINE_SINGLE_THREAD=ON` (and `OFF`), then build `BagiMahjong MahjongTests` and `ctest --test-dir build/webcheck`. The output lands in `build/ClangmacOS/` whatever the folder: delete `build/ClangmacOS/lib/*.a` when switching and build `macos-debug` again afterwards.

**The web app:** `web/app.json` names the page's home-screen app (name, any orientation, the felt colour, the icons of `web/icons/`, made by `python3 tools/make_app_icons.py`); `build_and_deploy.py` picks it up by itself. On a phone the first tap asks for fullscreen and locks no orientation: a phone held upright gets the portrait table (`engine/tools/web/CLAUDE.md`).

**What differs in the browser:** no saves and no `Continue` (the page's file system is memory, lost on reload; the seat choice is not remembered either), no match log, no control API, no bot arena. Persistence needs IDBFS, which is engine work and not done.

**Deploy to Railway (written down, not run; the owner runs it).** After `railway login` and `railway link` in a service made once (`railway add --service <name>`):

```bash
python3 engine/tools/web/build_and_deploy.py --source-dir . --game BagiMahjong --service <name> --deploy
```

`--dry-run` prints the commands without running them. It builds with the SDK in `engine/external/emsdk`, so it is not possible in a worker folder. The service `bagi-mahjong` takes its Railway settings (builder Dockerfile, health check `/healthz`, restart on failure, limits) from `web/.railway/railway.ts`, an IaC partial that owns only it: after a change, `cd web && npm install && railway config plan`, then `railway config apply`.

## How to play

The game opens on **New game: who plays?**: for each of the four seats pick `Person` or `Computer` (the default is you at seat 1 against three computers), then `Start`. The `New game` button in the top right corner of the table opens the question again and starts a new match.

- **Discard:** click a tile of your hand (the bottom row). Your hand is the only one face up; the others show backs until the hand is over (with four computers and no person at the table all four hands are face up).
- **Everything else is a button** in the lower right of the table, with a plain word and the tiles it is about drawn on it: `Mahjong!` (a complete hand, or the tile that completes it), `Pung`, `Kong`, `Chow` (one button per sequence you can make, with its three tiles), `Kong` (a concealed kong, with its tile), `Add to pung` (a fourth tile to an open pung) and `Pass`. Only the buttons that are legal now are shown. Hold the mouse over a button and a line above the buttons says what it does (a touch screen has no hover, so no line there). `Ready hand` is a switch: turn it on and the next tile you click is discarded as the declaration of the calling hand (the tiles that cannot declare are dimmed).
- **Hints** (on by default, switch `Hints` in the seat choice): in the top left corner of the table a row shows the tiles your waiting hand needs and, under each, how many you cannot see; on your turn a gold bar under a tile marks the discards that declare the ready hand; the `Hint` button asks what a computer would do in your place (a discard, a claim or a pass) and frames the tile it would discard. A hint only tells, it never plays for you.
- **Computers** play one step every second at `Normal` (`kComputerStepSeconds`); `Options` -> `Speed` (Slow / Normal / Fast) scales that pace, the flights of the tiles and the marks of a claim together; nothing moves while the seat choice is open.
- **Hot seat:** with two or more people at one screen the table is covered between turns ("Pass the screen to Seat 2 (South)"); the next person presses `Show`.
- **The end of a hand:** a score table covers the table: who won and how, the points of every seat in the hand, what each seat paid or got and its total, the winner's lines (`Pung of 5 Dots: 8`, `One suit, no honours: x8`) and the sum. All four hands are face up. `Next hand` settles it and deals on (`Finish match` on the last one); after the last hand the standings show the seats by points and `New game` opens the seat choice.
- **Saves:** the match in progress and the choices of the seat choice (language, seats, a level for each computer seat, hands, speed, hints) are saved as you play, in `saves/` beside the working directory (git-ignored; `BAGI_MAHJONG_SAVE_DIR` names another folder, empty switches saving off; off in the browser). When the game starts again with a match to go on, the seat choice shows `Continue` beside `Start`: it brings back the table as it was; `Start` begins a new match and replaces the saved one. The match logs for reading are in `logs/matches/`. Since format version 5 a log's `start` line also says where the match was played (`platform`, `build`, `gameVersion`); how such logs are collected and read is in `docs/tester-match-logs.md` (nothing is sent anywhere yet).
- A panel in the middle of the table, inside the wall, shows the round and the hand number (`East 1`) and the tiles left in the wall; on the side of each seat it shows that seat's wind letter and its points, the dealer's wind letter is marked, and a light on the side of the seat whose turn it is. Each seat has a plate at its side of the table with its name only; the plate of the seat whose turn it is is gold.

## Layout

| Path | What |
|------|------|
| `src/Mahjong/` | the game: `MahjongGame` and its control adapter, the rules and the computer players (`Game/`), the pure side of the scene (`Table/`: layout, tile atlas cells, action buttons, the words of the score table) and the scene components (`Components/`: the table, the HUD). `CLAUDE.md` describes it |
| `config/`, `assets/` | scene (`SceneConfig.xml` with the references and one file per node in `config/nodes/`), application config, the tile sprite library; the tile atlas (`assets/tiles/tiles.png`) |
| `tools/` | `make_tiles.py`, the generator of our own tile art (`config/TextureLibrary.xml` and the atlas are its output); `arena/main.cpp`, the bot arena program; `analyzer/main.cpp`, the EV analyzer program |
| `docs/` | `bot-arena.md`: how to run the arena and read its intervals; `ev-analyzer.md`: the analyzer of one position |
| `tests/` | gtest cases and `control_smoke.py`, the smoke test of the control API (plays a hand and a match over WebSocket) |
| `todo/`, `TODO.md` | known problems and ideas live in the todo store (area `mahjong`), not in git (`todo/README.md`) |
| `CI/` | environment setup (`setup_venv.*`) and the VS Code workspace `BagiMahjong.code-workspace` (CMake Tools on the engine's presets, CodeLLDB / MSVC launch, build tasks) |
| `engine/` | the engine (submodule); not edited from here |

## The engine

A change the game needs from the engine is made in the engine's own checkout, on `games/mahjong`, and then the pointer here is moved in a commit of its own: `git submodule update --remote engine`. Details: `.claude/rules/engine-and-todo.md`.
