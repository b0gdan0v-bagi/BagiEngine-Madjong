# Save/ — the save of a match and of the Setup choices

Plain C++ with reflected states: `MatchSave.*` has no scene and no disk, `MatchSaveStore.*` is the disk. The game's hook is `MahjongGame` (`../CLAUDE.md`). Both slots are `SaveGameService` slots of the game id `Mahjong`, version `kMahjongSaveVersion` (min `kMahjongSaveMinVersion`), in the engine's binary envelope (`engine/src/Modules/BECore/Save/CLAUDE.md`): a new field needs no version, a field whose meaning changes does.

## What is stored: the log of the match, not the table

- **`match`** (`kMatchSlot`): the `Start`, `Action` and `Settlement` events of the match in progress (`MatchSaveText`, one `EventToJson` line each, in the one string `MahjongMatchSave::log`; no `Hand` / `End`, no breakdowns). Resuming replays them (`TableSession::ResumeMatch`, `../Game/CLAUDE.md`). The settlements stay in on purpose: the replay checks them, so a save that a change of the rules or of the wall would make differ is **refused, not guessed at**.
- **The computers' random state is not saved**: the computers are made again from the seed of the match, so play after Continue is legal and of the same level, but not bit for bit the uninterrupted game. The price of not tying a save to the computer players of the build that wrote it.
- **`settings`** (`kPrefsSlot`, `MahjongPrefsSave`): the five choices of `Setup` and `Options` (`language`, `hintsOn`, `seats` "PCCC", `hands` 16 | 8, `level`); never `flowers`, `limit`, `replayDrawnHand`, `startingScore`.

## Where

`MatchSaveDirectory()`: default `saves/` under the working directory (git-ignored), or `BAGI_MAHJONG_SAVE_DIR` when set; an empty path switches saving off (`SetMatchSaveDirectory`). The gtests switch it off for the whole process (the global environment of `MahjongSaveGameTest.cpp`) and the saving tests use a temporary folder; `control_smoke.py` sets the variable to a temporary folder, so no test touches the owner's `saves/`.

## When it is written

- The session's sink is a `MatchRecorder`: it forwards every event to the `MatchLogWriter` and keeps the events of the current match. **No file is written from that hook.**
- `MahjongGame::FlushSave()` writes, called by `Stop()` and once per frame by `MahjongHudComponent` (`SceneUpdateEvent`). A burst of computer steps is one write; "after every action" means "at the end of the frame that made it". A flush is cheap even for a 16-hand match, so there is no throttle.
- It writes only a match that **is being recorded** (`IsRecording()`, not the counter alone: an unrecorded background match moves the counter too); a finished match deletes the slot, so it is never offered. The prefs are written only when they differ from the last copy written or loaded. A failed write is one warning, not retried until something changes.

## Start and Continue

- `Start()` opens the store, applies the prefs (a prefs slot that does not check is one warning and none of it applies), then tries `match`: parse, replay on a scratch `Match`, `ResumeMatch` with the recorder on. **On success the saved match is the table behind the open `Setup` and `CanContinue()` is true**; nothing is dealt and the slot is not rewritten.
- Otherwise (no slot: silent; a broken, foreign, newer or non-replaying slot: one warning, the file stays) a random match is dealt **unrecorded** (`BeginMatch(seed, false)`), so a match behind the panel can never overwrite the saved one.
- Any match begun by `StartMatch` (`Start` of the panel, `Reset()`, a control `reset`) is recorded, clears `CanContinue()` and replaces the slot at its first flush. `ContinueMatch()` closes `Setup` and the rules; closing the panel any other way also drops the offer.
- After Continue the state is where the match was left (mid-hand, a claim window, the score table). With two people or none the viewer is nobody, so the hot-seat cover is up. The game's seats, level and hands become those of the resumed `Start`, so `New game` shows the choices of the match that was played.
- A folder that cannot be used is one warning; the game plays on unsaved.
