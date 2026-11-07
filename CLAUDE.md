# Mahjong Module Guidelines

## Overview

`MahjongGame` is an `IGame` (name `Mahjong`, scene `MahjongScene`). Executable `BagiMahjong`, module `MahjongModule`, tests `MahjongTests`. Namespace `BECore::Mahjong`. Infrastructure and how a game is registered: `engine/src/Modules/Games/CLAUDE.md`; how this repository was set up around the engine: `engine/NEW_GAME.md` (on the engine's `main`).

**State: a skeleton.** The game is registered, starts in Game mode (`mode="Game"` in `config/ApplicationConfig.xml`; set `mode="Editor"` to open it in the scene editor) and shows its scene with a title. There are no rules, no components and no control API yet; which mahjong it is (solitaire or a table game) is still the owner's decision.

## Layout

```
src/Mahjong/
├── MahjongGame.{h,cpp}   IGame; GetActive() is how components will reach it
├── Game/                 (to come) plain C++ rules, no scene or renderer
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
| `MahjongGameTest` | the shipped `config/SceneConfig.xml` loads, `GameManager` activates `Mahjong` as the only registered game and its scene `MahjongScene` becomes active (needs the engine services) |
