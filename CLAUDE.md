# Mahjong Module Guidelines

## Overview

`MahjongGame` is an `IGame` (name `Mahjong`, scene `MahjongScene`). Executable `BagiMahjong`, module `MahjongModule`, tests `MahjongTests`. Namespace `BECore::Mahjong`. Infrastructure and how a game is registered: `engine/src/Modules/Games/CLAUDE.md`; how this repository was set up around the engine: `engine/NEW_GAME.md` (on the engine's `main`).

**State: a skeleton with the rules in progress.** The game is registered, starts in Game mode (`mode="Game"` in `config/ApplicationConfig.xml`; set `mode="Editor"` to open it in the scene editor) and shows its scene with a title. The rules of classic Chinese mahjong for four seats are being written in `Game/` as plain C++ (the tiles and the tile set exist so far; branch `todo/mahjong-classic-rules`); `MahjongGame` does not use them yet. There are no components and no control API yet.

## Layout

```
src/Mahjong/
├── MahjongGame.{h,cpp}   IGame; GetActive() is how components will reach it
├── Game/                 plain C++ rules, no scene or renderer
│   ├── Tile.{h,cpp}      Tile (one id 0..41: suits, winds, dragons, flowers, seasons), predicates, notation (ToString / TryParse / ParseTiles: 1p 9s 5m Ew Rd F1 T4)
│   ├── TileSet.{h,cpp}   BuildTileSet(flowers): 136 or 144 tiles in a fixed order
│   └── MahjongSettings.h handsPerMatch, flowers, replayDrawnHand, limit
└── Components/           (to come) scene glue: components that read the model of the active game
config/
├── ApplicationConfig.xml the engine's file with mode="Game"
└── SceneConfig.xml       MahjongScene; it replaces the engine's file, so it holds every scene the game uses
assets/                   (empty) sprites, with a config/TextureLibrary.xml once there are any
tests/                    gtest sources, built as MahjongTests
```

## Rules for the code to come

- The rules live in `Game/` as plain C++ that a test drives without a scene; `MahjongGame` owns the model, components read it through `MahjongGame::GetActive()` and check that the game is the active one (`engine/src/Modules/Games/CLAUDE.md`, "Gotchas").
- Components subscribe with `SubscribeInScene`; there is no per-frame hook in `IGame`, per-frame work belongs in a component.
- No `ImGui::` in a game component without `ImGui::GetCurrentContext()`: the HUD and the menus are UI canvases of the engine (`engine/src/Modules/BECore/UI/CLAUDE.md`).
- A control API is an `IControllableGame` adapter next to the game, returned from `AsControllable()`; with it comes `be_add_game_control_tests(BagiMahjong ...)` in `CMakeLists.txt`.
- New or moved sources, and every change to a file with `BE_CLASS` / `BE_REFLECT_FIELD`, need `cmake --preset <p>` again (sources are globbed, reflection is generated at configure time).

## Tests

`tests/`, executable `MahjongTests`; see `engine/.claude/rules/tests-module.md`. New rule code gets a gtest case.

| File | Covers |
|------|--------|
| `TileTest` | tile ids and predicates, the notation round trip, the tile set (136 / 144, four of each kind), the settings defaults (pure logic, no engine services) |
| `MahjongGameTest` | the shipped `config/SceneConfig.xml` loads, `GameManager` activates `Mahjong` as the only registered game and its scene `MahjongScene` becomes active (needs the engine services) |
