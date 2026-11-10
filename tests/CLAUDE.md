# tests/ — MahjongTests and the control smoke test

Rules of the engine: `engine/.claude/rules/tests-module.md`. New rule code gets a gtest case. A new feature gets a new test file when the existing one is big and shared. **This file lists helpers and what each file is for, not what each test checks** — the test names say that.

## Helpers (header only)

| Header | Use it for |
|--------|-----------|
| `FixedWall.h` | `FixedWallSpec { hands[4], live, loose, liveAfterDeal }`, `MakeFixedWall`, `MakeModel`: **every test that needs to know what a seat holds or draws** (padding never exceeds four copies) |
| `HandTestSupport.h` | `kSeat0..3`, `T("5p")`, `Error`, `Has`, `Snapshot(model)`, `Lap`, the "accepted iff listed" checks, the random-play chooser (`Mode::SeekWin` ...) |
| `ComputerTestSupport.h` | quiet hands `kQuiet1..3`, `SpecOf`, `OpenPungModel`, `UpgradeModel` |
| `ScoreTestSupport.h` | `MakeInput` / `MakeWinnerInput` from notation, meld builders, line lookups, `ExpectConsistent`, the expected rule numbers |
| `MatchTestSupport.h` | `SettingsOf(index)`, `ExpectWellFormed`, `MatchAudit` (invariants around every hand) |
| `StringsTestSupport.h` | `LoadShippedStrings`, `ReadShippedEntries`, `PlaceholderNames`, `ForEachLanguage`, `LongestText` |
| `ShippedScene.h` | the one place that knows where the scene is and expands the node refs (`LoadShippedSceneDocument`, `ShippedSceneXml`) |
| `MahjongTableTestSupport.h` | `MahjongTableTestBase`: the shipped scene, the game through `GameManager`, a `TurnRecordingRenderer` (a `RecordingRenderer` that records a turned draw as a `Texture` / `Rect` call over the screen rectangle, the unturned rectangle and the transform in `turned`) as big as the view (a view point is a pixel), `Lay(model)`, `Draw`, `Click`, `ClickHandTile`, `Element` / `ClickElement`, `ExpectPassesTheMatrix`, `ExpectWordsFit`; free `EmitAction`, `Var`. Loads `en`; flights off |
| `MahjongTouchTestSupport.h` | the size matrix: `ForEachMatrixWindow`, `LayOutInWindow`, `ToView`, `ExpectClearOfTable`, `MakeWorstCaseTableLayout`, `IsCompactWindow`, `ActionsMayBeSmall`, `SetOutputSize`, `ExpectMatrixExceptTouchWhere`, `WordsThatDoNotFitOnTheMatrix` |
| `UiStyleTestSupport.h` | `SameColor`, `PaletteColor(role)`, `GapBelow`, `Apart`, `Grown`, `ZonePlates` |

## Files

- **Rules (pure logic):** `Tile`, `MahjongRng` (golden values), `Seats`, `Wall`, `Deal`, `FixedWall`, `HandShape`, `HandWaiting` (pruned vs reference), `Shanten`, `HandModel{Turn,Claim,Kong,Moves,Actions,Playout,Window,Restore}`, `Score{Points,Doubles,Winner,Limit}`, `Payments`, `ScoreHand`, `Match`, `MatchPlayout`.
- **Computer players:** `ComputerView` (no peeking), `ComputerTurn`, `ComputerClaim`, `ComputerPlayer`, `ComputerLevel` (**golden scores of Normal, seeds 700..703**), `ComputerMatch` (whole matches, ~8 s Debug), `ComputerStrength` (vs random seats, the numbers in the file, ~3 s), `ComputerWorld` (a sampled world gives back the same view), `ComputerSearch` (~7 s); `ComputerMatch` also plays one search seat among three Normal ones.
- **Arena:** `ArenaPlayer`, `ArenaRun`, `ArenaStats`, `ArenaCli`.
- **Session, log, control:** `TableSession`, `MatchLog`, `MatchLogJson`, `MatchLogFile` (its global environment points the log folder at a temp dir for the whole binary), `MahjongSave` (resume from a log, ~20 s Debug), `MahjongSaveStore`, `MahjongSaveGame` (its global environment switches saving off for the whole binary), `MahjongGameControl`, `MahjongGame`.
- **Pure scene logic:** `TableLayout`, `SeatPlates`, `CenterPanel`, `ActionButtons`, `MoveText`, `RulesText` (the drift guard: no digit outside a placeholder), `ScoreText`, `Hints`, `CallingMarks`, `TileFlights`, `AnimationSpeed`, `TouchRegime`, `TileSprites`, `Strings`.
- **The scene (engine services, the table fixture):** `MahjongSceneFiles` (`kSceneNodes`: the one table of nodes), `MahjongUiStyle`, `MahjongTableUi` (faces per slot), `MahjongTableInput`, `MahjongTableActions`, `MahjongTableWindow`, `MahjongTableMarks`, `MahjongTableLostClaim`, `MahjongTableFlight`, `MahjongTableSpeed`, `MahjongTableScreen`, `MahjongHud`, `MahjongOptions`, `MahjongHints`, `MahjongRules`, `MahjongEndScreen`, `MahjongTouchSize`.
- **`control_smoke.py`** (ctest `BagiMahjongControlSmoke`): `BagiMahjong --headless --control` on the real and the manual clock, over WebSocket with the engine's `tools/control/tests/smoke.py` (`BAGI_ENGINE_DIR`). Sets `BAGI_MAHJONG_SAVE_DIR` to a temp folder and checks a real restart (Continue offered, equal state, garbage save refused). Does not build the executable; a missing one is a skip (77). By hand: `BAGI_ENGINE_DIR=engine python3 tests/control_smoke.py --exe <path to BagiMahjong>`.
