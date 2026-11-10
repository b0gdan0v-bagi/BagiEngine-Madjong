# src/Mahjong — the game object, the control API, the match log

Subfolders have their own `CLAUDE.md`: `Game/` (the rules), `Save/` (save and Continue), `Table/` (pure scene logic), `Components/` (the scene), `Arena/` (bot measurement).

## MahjongGame

`IGame` named `Mahjong`, scene `MahjongScene`. Owns, in this order, the `Match`, the `TableSession` over it, the `MatchLogWriter` (fed through the `MatchRecorder`, the session's log sink), the save store and the control adapter. `FlushSave()`, `CanContinue()` / `ContinueMatch()`: `Save/CLAUDE.md`. Components reach it through `MahjongGame::GetActive()` and must check it is the active game. `Start()` applies the saved `Setup` choices and offers a saved match (`Save/CLAUDE.md`); `Start()` / `Reset()` begin a match from a random seed (`std::random_device`, the only place a seed is chosen); `StartMatch(seed)` replays one. Kept across `Reset()` and `StartMatch`: `GetSettings()`, `GetSeats()`, `GetLevel()`, `GetLanguage()` (applied to the string table at every start), `HintsOn()`. UI flags: `IsSetupOpen()` (`Start()` opens it; any start and any accepted control action close it), `IsOptionsOpen()` (the page `Options` over `Setup`: meaningful only with `Setup` open, closed by `CloseSetup()`, so by any start, `Continue` and any accepted control action), `IsRulesOpen()`, `IsTablePaused()` (any of the three is open).

## Control API

`MahjongGameControl` (`AsControllable()`): `get_state`, `list_actions`, `perform_action`, `reset` of the engine's game control (`engine/src/Modules/Games/CLAUDE.md`). `set_state` is unsupported; `no_active_model` before the first match. A client plays the **person seats**; after each action the adapter calls `RunComputers()`. With no person seat (`"seats": "CCCC"`) `reset` and `next_hand` play each hand to its end and the client is a spectator.

- **State** is the view of the person to act, else of the first person (`MakeTableView`), so a client never sees more than a computer could. Keys: `phase` (`playing` | `hand_over` | `match_over`), `seat`, `seatWind`, `roundWind`, `east`, `hand`, `turn` {`phase`, `current`, `pendingTile`, `yourTurn`}, `seats` (kind, wind, score, `concealedCount`, melds, `concealedKongs`, bonus, `discardCount`, `calling`), `discards`, `wall`, `level`, `handsPlayed`, `handsTotal`, `version`, and once a hand is over `result` (all four hands, breakdowns, scores, deltas). A client reads `yourTurn`.
- **Actions** (`ActionToJson` / `ActionFromJson`): `discard` {`tile`, `declareCalling`}, `win`, `concealed_kong` / `upgrade_kong` {`tile`}, `claim` {`claim`: `chow` (+ `first`) | `pung` | `kong` | `mahjong`}, `pass`, `next_hand` (only in `hand_over`). `illegal_action` = well-formed but not listed; `invalid_params` = malformed. Client data goes through the checked helpers of `BECore/Control/Json.h`.
- **`reset`** `{seed, seats "PCCC", hands 8|16, flowers, level 1..10}`, all optional; given values are kept for the next match; anything invalid changes nothing.
- Output is English in every language (ids, notation). `GetStateVersion()` is `TableSession::Version()`.
- Smoke test: `tests/control_smoke.py` (ctest `BagiMahjongControlSmoke`).

## Match log

- **JSON** (`MatchLogJson`): one object per line, key `event` (`start`, `hand`, `action`, `settlement`, `end`); tiles in notation, winds by name, seats 0..3; `action` is exactly `ActionToJson`. `kMatchLogFormatVersion` 3 (`level` in `start` is the number 1..10); version 2 wrote the names `easy` / `normal` (read as 1 / 5, and `hard`, `kMatchLogRetiredLevelName`, as 5); version 1 has no level and reads as Normal. Reading is checked and errors name the member and the line.
- **File** (`MatchLogWriter`): one file per match, `logs/matches/<local time>_seed<seed>.jsonl` under the working directory (`-2`, `-3` on a clash; `std::ios::noreplace`, never overwritten), flushed per line, opened at `Start`, closed at `End` / next `Start`. `SetMatchLogDirectory("")` switches it off. Off by default in the browser build (`Platform.h`). It never fails the game: a problem is one warning. The gtests point it at a temp folder for the whole process (`MatchLogFileTest`); the smoke test's game writes into the build folder.
