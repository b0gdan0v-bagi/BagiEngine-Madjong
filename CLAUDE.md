# BagiMahjong

Classic Chinese mahjong for four seats on BagiEngine (the submodule `engine/`, branch `games/mahjong`; see `.claude/rules/engine-and-todo.md`). `MahjongGame` is an `IGame` (name `Mahjong`, scene `MahjongScene`); executable `BagiMahjong`, module `MahjongModule`, tests `MahjongTests`, namespace `BECore::Mahjong`. How a game plugs into the engine: `engine/src/Modules/Games/CLAUDE.md`. Build commands: `README.md` and `.claude/rules/build-project.md`.

## State

Every stage of the plan is built: rules, match, computer players (levels 1..10), table session, control API, match log, the scene, save and Continue, English and Russian. The server side of online play exists (`BagiMahjongServer`, `Online/`; build and run: `README.md`) and so do the client logic (`Online/`: the model over `RoomClient`) the screens to host, join and wait in a room (the `Online` button of `Setup`) and the online table (the room's view shown by the scene's own components, with the ready check and `Leave room`; hints and the coach work there when the host allowed them), in the browser when `config/MahjongOnlineConfig.xml` names the server (hosting: `docs/web.md`). It also builds for the browser (`BagiMahjong.html`; saves and `Continue` are kept in the browser's storage, match log and control API are off there: `docs/web.md`). Open work and the owner's pending looks are in the todo store (area `mahjong`), not here.

## Where to read

A nested `CLAUDE.md` loads when you work in its folder.

| Folder | Covers |
|--------|--------|
| `src/Mahjong/` | `MahjongGame`, the control API, the match log JSON and file |
| `src/Mahjong/Online/` | `MahjongSeatedGame`: the game in a room (server side, the ready check, the views); `OnlineClient`: the client model over `RoomClient` (stage, lobby, table, notice, session to keep); `OnlineScreens`, `TextEntry`: the screens' pure logic |
| `src/Mahjong/Save/` | the save of a match and of the `Setup` choices, `Continue` |
| `src/Mahjong/Game/` | the rules: wall, `HandModel`, scoring (and the owner's readings), `Match`, the computer player, `TableSession`, `MatchLog` |
| `src/Mahjong/Table/` | pure scene logic: layout, words, seat plates, buttons, hints, flights, touch regime |
| `src/Mahjong/Components/` | the scene: table, marks, flights, action buttons, HUD, setup, rules and end screens, hints |
| `src/Mahjong/Arena/` | the bot arena (`BagiMahjongArena`, `docs/bot-arena.md`) |
| `src/Mahjong/Analyzer/` | the EV of every legal action of one position (`BagiMahjongAnalyzer`, `docs/ev-analyzer.md`) |
| `config/` | node files, the string table, **the UI style** (palette by role, type scale, grid, touch size) |
| `tools/` | tile art (`make_tiles.py`), UI screenshots (`ui_shots.py`) |
| `tests/` | the test helpers and what each test file is for |

## Rules for the code

- The rules live in `Game/` as plain C++ that a test drives without a scene. `Game/` and `Table/` know no JSON, no scene and no renderer.
- Components reach the game through `MahjongGame::GetActive()`, check it is the active game, and change it only through `GetSession()` (`TableSession`). A match is started only through the session.
- The seed of a match is chosen in `MahjongGame` (`std::random_device`), never in the rules; rules randomness only through `MahjongRng`.
- **No peeking:** a computer player, the hints, the marks and the control state see only `MakeTableView(model, seat)` or public data.
- Every visible word is a key of `config/UIStrings.xml` in both languages.
- No `ImGui::` in a game component without `ImGui::GetCurrentContext()`; the UI is the engine's canvases.
- Per-frame work belongs in a component (`SubscribeInScene`); `IGame` has no per-frame hook.
- **Zero allocations in a steady frame:** a frame in which nothing changed allocates nothing, the whole scene in every state (a person's turn, hints, modal screens, either language). Make a thing again only when its key moved (`TableSession::Version()`, `StartCount()`, the `UIStrings` version, the window), keep lists as members so they keep their capacity. The reason: a phone and the browser build pay for every allocation in frame time and battery, and an allocation per frame hides until a profile shows it. `MahjongTableAllocTest` holds it in `macos-profile` (`ENGINE_PROFILER_ALLOC`); a new screen or state gets a case there. **Build and run it only when the owner asks** (or `/perf`), never as a routine check after a change: the profile build is a second full build (owner, 2026-11-12).
- New or moved sources, and any change to a file with `BE_CLASS` / `BE_REFLECT_FIELD`, need `cmake --preset <p>` again.
- The `LSP` tool (clangd plugin) reads `build/macos-debug/compile_commands.json` through `.clangd`: a new source has no flags there until `cmake --preset macos-debug`. Its diagnostics are a hint (PCH-reliant headers can show false errors); the build and the tests decide.

## Docs budget

This file is loaded into every session and every agent: keep it under 12k characters, the rules without `paths:` under 8k together, every nested `CLAUDE.md` under 20k. Write the contract and the reasons a reader could not get from the code; do not restate what a test checks, a constant's value, or the history of a decision (that goes to the entry's plan in the todo store). `engine/CI/scripts/check_doc_budget.py --root .` checks the limits.
