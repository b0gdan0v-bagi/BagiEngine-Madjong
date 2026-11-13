# BagiEngine-Madjong

Classic Chinese mahjong for four seats on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine) (the submodule `engine/`, branch `games/mahjong`). Play alone against three computers (levels 1..10) or hot seat at one screen, on desktop or in a browser; external clients play through the control API (`engine/tools/control`). The tile art is drawn by a script of this repository.

## Principles

- **Zero allocations in a steady frame:** a frame in which nothing changed allocates nothing.
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
build/macos-debug/bin/BagiMahjong
```

- **One command per product:** `python3 engine/tools/dev/dev.py run game` (also `tests`, `server`, `web`, `arena`, `analyzer`; `chain online-local` starts the room server and two games; `info` lists all), described in `dev.json`; the VS Code panel **Bagi Dev** (`engine/tools/vscode/bagi-dev`) has the same buttons and switches build flags as `<preset>-dev` presets in `CMakeUserPresets.json` (`engine/tools/dev/CLAUDE.md`).
- **Bot arena** (Release, `docs/bot-arena.md`): measures the computer players over thousands of seeds.
- **EV analyzer** (Release, `docs/ev-analyzer.md`): the expected points of every legal action of one position.
- **Browser** (Emscripten, `docs/web.md`): build, smoke, deploy. Saves (choices, `Continue`, the room session) are kept in the site's IndexedDB, none in a private window; no match log or control API there.
- **Tile art:** `python3 tools/make_tiles.py` (`--check` tells whether the committed atlas is up to date).

## Online play

The first screen (`Setup`) has an `Online` button (on the desktop; in the browser when `config/MahjongOnlineConfig.xml` names a server, hosting in `docs/web.md`): **Host a room** (name, 8 or 16 hands, the level of the computer seats, hints, and whether the room is joined `By code` or is an `Open` room that others see in the list, then the code to give to friends), **Join a room** (name and the five-character code), **Open rooms** (the rooms of others that wait for players, three to a page with their host, free seats, hands, level and hints; Refresh asks again, a tap joins), then the lobby (take a seat, the host starts) and, after a drop or a restart, **Return to room** while the game goes on. The name is typed on a pad of keys or on the keyboard. When the host starts, the panel closes and the room's table is the game's own scene: your tiles face up, the others' as backs, the plates carry the players' names, a claim window offers its buttons, and at a hand's end the score table is a ready check (**Next hand** says how many of four have read it). **Leave room** takes the place of **New game**; a lost connection shows on the line under the discards while the last table stays, and the game goes on when the link is back. Hints, advice and the coach are off in a room, and so are the marks of a claim that lost and the level emblems of the computer seats (`src/Mahjong/Online/CLAUDE.md`).

`BagiMahjongServer` is a headless program (no window, no renderer) that keeps rooms of the game `mahjong` in memory; it is not built for the browser. The desktop game connects to `ws://localhost:8080` unless `BAGI_MAHJONG_SERVER` names another address; the browser page connects to the `wss://` address of that config file.

```bash
cmake --build --preset macos-debug --target BagiMahjongServer
build/macos-debug/bin/BagiMahjongServer --port 8080                 # --help lists the flags; PORT and BAGI_ROOMS_* are read too
build/macos-debug/bin/BagiMahjong                                   # twice: Host in one window, Join with its code in the other
ctest --preset macos-debug -R BagiMahjongRoomSmoke                  # starts it, plays hands with four clients, a reconnect
docker build -f engine/tools/rooms/Dockerfile --build-arg SERVER_TARGET=BagiMahjongServer -t bagi-mahjong-server .
```

Hosting (Railway, the settings, the proxy): the engine README, "Hosting the room server". The server runs the rules, so a change in `src/Mahjong/Game/` needs a new image together with the client. The kept room and the name are saved in `saves/` (the token is a secret in a plain file).

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
