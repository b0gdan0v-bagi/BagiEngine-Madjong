# BagiMahjong

Classic Chinese mahjong for four seats on BagiEngine (the submodule `engine/`, branch `games/mahjong`; see `.claude/rules/engine-and-todo.md`). `MahjongGame` is an `IGame` (name `Mahjong`, scene `MahjongScene`); executable `BagiMahjong`, module `MahjongModule`, tests `MahjongTests`, namespace `BECore::Mahjong`. How a game plugs into the engine: `engine/src/Modules/Games/CLAUDE.md`. Build commands: `README.md` and `.claude/rules/build-project.md`.

## State

Every stage of the plan is built: the rules, the match, the computer players (Easy | Normal; a search player exists as knobs, off, since it did not beat Normal: `docs/bot-search.md`), the table session, the control API, the match log, the scene (table, HUD, seat plates, hints, marks, flights, setup, end screens, 13 rules pages), save and Continue, English and Russian. Open work and the owner's pending looks are in the todo store (area `mahjong`), not here.

## Where to read

Nested `CLAUDE.md` files load when you work in their folder; read the one for the place you change.

| Folder | Covers |
|--------|--------|
| `src/Mahjong/` | `MahjongGame`, the control API, the match log JSON and file |
| `src/Mahjong/Save/` | the save of a match and of the `Setup` choices, `Continue` |
| `src/Mahjong/Game/` | the rules: wall, `HandModel`, scoring and the owner's scoring readings, `Match`, the computer player and levels, `TableSession`, `MatchLog` |
| `src/Mahjong/Table/` | pure scene logic: layout, words, seat plates, buttons, hints, flights, touch regime |
| `src/Mahjong/Components/` | the scene: table drawing and play, marks, flights, HUD, setup, rules screen, end screens, hints, action buttons |
| `src/Mahjong/Arena/` | the bot arena (`BagiMahjongArena`, `docs/bot-arena.md`) |
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
- New or moved sources, and any change to a file with `BE_CLASS` / `BE_REFLECT_FIELD`, need `cmake --preset <p>` again.

## Docs budget

This file is loaded into every session and every agent: keep it under 12k characters, and every nested `CLAUDE.md` under 20k. Write the contract and the reasons a reader could not get from the code; do not restate what a test checks, a constant's value, or the history of a decision (that goes to the entry's plan in the todo store). `engine/CI/scripts/check_doc_budget.py --root .` checks the limits.
