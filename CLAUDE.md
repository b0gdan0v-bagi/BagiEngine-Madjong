# Mahjong Module Guidelines

## Overview

`MahjongGame` is an `IGame` (name `Mahjong`, scene `MahjongScene`). Executable `BagiMahjong`, module `MahjongModule`, tests `MahjongTests`. Namespace `BECore::Mahjong`. Infrastructure and how a game is registered: `engine/src/Modules/Games/CLAUDE.md`; how this repository was set up around the engine: `engine/NEW_GAME.md` (on the engine's `main`).

**State: a skeleton with the rules in progress.** The game is registered, starts in Game mode (`mode="Game"` in `config/ApplicationConfig.xml`; set `mode="Editor"` to open it in the scene editor) and shows its scene with a title. The rules of classic Chinese mahjong for four seats are being written in `Game/` as plain C++ (the tiles, the tile set, the seeded RNG, the seats, the wall and the deal exist so far; branch `todo/mahjong-classic-rules`); `MahjongGame` does not use them yet. There are no components and no control API yet.

## Layout

```
src/Mahjong/
├── MahjongGame.{h,cpp}   IGame; GetActive() is how components will reach it
├── Game/                 plain C++ rules, no scene or renderer
│   ├── Tile.{h,cpp}      Tile (one id 0..41: suits, winds, dragons, flowers, seasons), predicates, notation (ToString / TryParse / ParseTiles: 1p 9s 5m Ew Rd F1 T4)
│   ├── TileSet.{h,cpp}   BuildTileSet(flowers): 136 or 144 tiles in a fixed order
│   ├── MahjongRng.h      seeded mt19937 with Below / RollDie / Roll2d6 and a hand-written Fisher-Yates Shuffle (never std::shuffle or std::*_distribution: a seed must give the same game on every platform)
│   ├── Seats.{h,cpp}     Seat (0..3, counter-clockwise), NextSeat, Offset, Wind, MakeSeatWinds, DecideEast (the draw for East, with a roll seam for tests), RollSeatWinds
│   ├── Wall.{h,cpp}      Wall (loose reserve, live wall, dead wall), MakeWall(rng, settings, east) (shuffle, break), BreakIndex / BreakSide
│   ├── PlayerHand.h      Meld (chow / pung / kong, concealed or open, where it came from), PlayerHand (sorted concealed tiles, melds, bonus tiles, the calling-hand declaration)
│   ├── Deal.{h,cpp}      DealHands(wall, east, flowers): 13 tiles per seat, bonus tiles laid open and replaced from the loose tiles
│   └── MahjongSettings.h handsPerMatch, flowers, replayDrawnHand, limit
└── Components/           (to come) scene glue: components that read the model of the active game
config/
├── ApplicationConfig.xml the engine's file with mode="Game"
└── SceneConfig.xml       MahjongScene; it replaces the engine's file, so it holds every scene the game uses
assets/                   (empty) sprites, with a config/TextureLibrary.xml once there are any
tests/                    gtest sources, built as MahjongTests
```

## Wall conventions

The break, the loose tiles and the dead wall are assumptions of this repository (`Wall.h` has the details; the numbers are named constants). The ring is four sides of 17 stacks of two tiles (18 stacks with flowers). East rolls 2d6 `a`: side `(a - 1) % 4` counted from East's own wall is broken; the owner of that side rolls `b`; the break is after `a + b` stacks from the right end of that side. The sides lie clockwise in the ring, so a count that runs past the left end of a side goes on into the wall of the clockwise neighbour, as on a real table, and `GetBreak().index` is the physical position of the break. Drawing starts at the break. The first 2 tiles are the loose reserve (a loose draw is refilled from the live tail), the last 14 are the dead wall and are never drawn.

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
| `MahjongRngTest` | same seed same sequence, dice 1..6 and every face, shuffle keeps the multiset, golden values of seed 1 (the `mt19937` stream and the shuffled set are pinned) |
| `SeatsTest` | `NextSeat` / `Offset`, seat winds counter-clockwise from East, the draw for East (scripted ties re-roll only the tied seats) |
| `WallTest` | the ring (136 / 144), `BreakIndex` for hand-computed dice, the order of the rolls, same seed same wall, loose and live draws, refill from the live tail, the dead wall never reached |
| `DealTest` | the deal: 13 tiles per seat for many seeds and every East, the deal order (four at a time from East, then one each), bonus tiles laid open and replaced from the loose tiles (also two in a row); `PlayerHand` helpers |
| `FixedWallTest` | the helper `FixedWall.h` itself: layout, padding never over four copies, refills at the live tail, `liveAfterDeal`, a bad spec fails the test |
| `MahjongGameTest` | the shipped `config/SceneConfig.xml` loads, `GameManager` activates `Mahjong` as the only registered game and its scene `MahjongScene` becomes active (needs the engine services) |

`tests/FixedWall.h` (header only) lays out a wall tile by tile for the tests of the hand: `FixedWallSpec { hands[4] (13 tiles per seat in deal order), live, loose, liveAfterDeal }` and `MakeFixedWall(spec, flowers, east = 0)`; whatever the spec leaves out is padded from the set (never more than four of a kind; a spec that asks for more fails the test). Use it for every test that needs to know what a seat holds or draws.
