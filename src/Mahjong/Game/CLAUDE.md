# Game/ — the rules as plain C++

Classic Chinese mahjong for four seats. No scene, no renderer, no JSON: a test drives everything here directly. Every number of the rules is a named constant in its header; this file names the constant, the header holds the value.

## Files

| File | What |
|------|------|
| `Tile`, `TileSet` | tile ids 0..41 (dots 0..8, bamboo 9..17, characters 18..26, winds, dragons, flowers, seasons), predicates, notation `1p 9s 5m Ew Rd F1 T4` (`ToString` / `TryParse` / `ParseTiles`); `BuildTileSet(flowers)` 136 / 144 |
| `MahjongRng.h` | seeded mt19937 with `Below` / `RollDie` / `Roll2d6` / hand-written Fisher-Yates. **Never `std::shuffle` or `std::*_distribution`**: one seed must give one game on every platform |
| `Seats` | `Seat` 0..3 counter-clockwise, `NextSeat`, `Offset`, `MakeSeatWinds`, `EastOf`, `kNoSeat`, `DecideEast` / `RollSeatWinds` |
| `Wall`, `Deal`, `PlayerHand.h` | the ring, the break, loose reserve / live wall / dead wall; `DealHands`; `Meld`, `PlayerHand` |
| `HandShape`, `Shanten` | pure functions on tile counts: `Decompose`, `IsWinningShape`, `IsWaiting`, `WinningTiles`, seven pairs, thirteen orphans; `Shanten` (four sets and a pair only), `ImprovingTiles` |
| `HandModel` | one hand from the deal to a result (below) |
| `Score*`, `Payments`, `ScoreHand` | scoring and settlement (below) |
| `Match`, `MahjongSettings.h` | a game of 16 or 8 hands (below) |
| `ComputerView`, `ComputerTurn`, `ComputerClaim`, `ComputerPlayer`, `ComputerLevel.h` | the computer player (below) |
| `TableSession` | who acts next, the computers, hot seat, the version (below) |
| `MatchLog` | the log of a match as plain data and its replay (below) |

## Wall conventions

The break, the loose tiles and the dead wall are this repository's assumptions (`Wall.h`). Four sides of 17 stacks (18 with flowers). East rolls 2d6 `a`: side `(a - 1) % 4` counted from East's wall is broken; its owner rolls `b`; the break is after `a + b` stacks from the right end, running on into the clockwise neighbour's wall. Drawing starts at the break; the first 2 tiles are the loose reserve (refilled from the live tail), the last 14 are the dead wall and are never drawn.

## HandModel

`Begin(settings, wall, seatWinds, roundWind)` deals and gives East the first tile (`BadSeatWinds` unless one East). A model never begun is a finished drawn hand, so a result exists exactly when the phase is `Finished`.

- **Phases:** `Discard` (the current seat holds one tile too many), `Claim` (a discard pending; `CurrentSeat()` is the discarder, the tile is `Discards().back()`), `RobKong`, `Finished`. Draws are automatic (`Receive`), so a seat only sees a phase it can act in. A bonus tile is laid open and replaced from the loose reserve; no tile to draw ends the hand `Drawn`.
- **Actions** return `std::expected<void, HandError>`; a refused action changes nothing. Nothing is compulsory.
- **Calling hand:** declared with a discard (`declareCalling`); the 13 that stay must wait (`WinningTiles(PlayerHand)`, meld copies counted). `callingAtDiscardCount == 0` means "declared with the first discard". A self-draw `DeclareWin` and a mahjong claim need it.
- **Claims:** a window opens only if some seat has a legal claim; each seat in it decides once (`Claim` / `Pass`). Chow only for the next seat (`chowFirst` = lowest tile, no honours, no wrap), pung needs two, kong three. Priority mahjong > pung/kong > chow; two mahjong claims: the larger `Offset` from the discarder. The claimer discards without drawing (after a kong it draws a loose tile first); the turn then goes to the seat after the claimer.
- **Kongs:** `DeclareConcealedKong` (four concealed equal tiles), `UpgradeKong` (only with the tile drawn this turn, not a kept copy, not right after a claim). Both draw a loose tile. An upgrade can be robbed (`RobKong`, mahjong only, by a declared seat the tile completes).
- **Result:** `HandResult { kind, winner, winningTile, source (Discard | SelfDraw | RobbedKong), from, loose, lastLiveTile }`. `loose` = the tile came from the reserve; `lastLiveTile` only for a self-draw on the last live-wall tile.
- `PendingTile()` is valid only while a window is open. `DrawnTile(seat)` only in `Discard` for the current seat that really drew (invalid after a claimed pung/chow; the loose tile after a kong).
- `LastClaimWindow()` is how the last `Claim` window was settled (`ClaimWindowResult`: winner, the `beaten` claims, a `serial` per window; `Begin` clears it, the robbing window leaves it): public data, so the table may show it to anyone.
- `Moves()` is the append-only public log of the hand (`HandMove`: discards, claims, kongs; a concealed kong carries no tile; no passes, wins, bonus tiles).
- **Actions as data** (the surface of the computer player and the control API): `HandAction` with factories `MakeDiscard`, `MakeDeclareWin`, `MakeConcealedKong`, `MakeUpgradeKong`, `MakeClaim`, `MakePass`. `LegalActions(seat)` lists exactly what `Apply(seat, action)` accepts (tests hold "accepted iff listed").

## Scoring

`ScoreSeat(const ScoreInput&)` is pure, so a test builds a hand from notation. `ScoreBreakdown { won, lines, points, doubles, raw, limitHand, score }`: `raw = points << doubles` (saturating), `score = RoundUpToTen(min(raw, LimitCap(limit)))`.

- **Readings** (`ScoreReading`): the hand is read as sets and a pair; the reading with the largest `raw` wins, all lines come from one reading. A winning discard or robbed kong opens the set that holds the tile (each placement is a candidate); a self-draw keeps it concealed; the pair opens nothing. Four undeclared equal tiles are a concealed pung. A seat that does not win: melds, concealed pungs, its best pair; no chows looked for.
- **Points:** pung/kong 2, x2 terminal/honour, x2 concealed, x4 kong; pair of dragons / own wind / round wind 2 each; bonus tile 4; chow 0.
- **Doubles for everyone:** dragon / own wind / round wind set; one suit with honours 1, without 3; own / round flower.
- **Winner** (`ScoreWinner`): `Win` 20, `WinTileFromWall`, `OnlyWinningTile`; doubles `OnlyPungs`, `NoPoints`, `LittleFourWinds` (2), `LittleThreeDragons`, `TerminalsOrHonoursEverywhere`, `FullyConcealed`, `SeveralKongs`, `LooseTile`, `LastTile`, `RobbedKong`, `ObviouslyLast`, `CallingFirstHand`.
- **Limit hands** (`ScoreLimit`, `FindLimitHand`, winner only): score `LimitCap(limit)` as a single line, no other line read; first match wins in the enum order. Seven pairs (one suit or honours), concealed pungs of one suit, honours only, terminals only, all green, big three dragons, big four winds, four kongs, thirteen orphans, last tile 1 dot, loose tile 5 dots (the last two need a complete reading too).
- **Payments** (`Settle`): every loser pays the winner the winner's score, losers settle pairwise by the difference, any payment involving East is x`kEastPaymentFactor`; zero-sum; a drawn hand pays nothing. `SettleHand(model)` scores all four seats (also in a drawn hand, for the score table). `CountVisibleCopies`: copies in the pile and in open melds, not the winning tile, not concealed kongs.

### Scoring readings decided by the owner (2026-11-08)

Each is one place to change: no points = nothing but the 20 (`AddNoPoints`); obviously last = three other copies open (`CountVisibleCopies`, `kObviouslyLastCopies`); calling in the first hand = with the first discard; terminals everywhere counts the pair and chows with 1/9; a winning discard opens its set, a self-draw does not (`AddDecompositionReadings`); a flower both own and round doubles twice; order is points x 2^doubles, then the limit, then round up; only winning tile = the 13 wait on that one kind; limit hands: one suit means no honours, all green is pungs and a pair, big dragons' fourth set and pair one suit, four kongs open or concealed; a non-winner scores standing sets and best pair; every hand counts toward the 16/8 (`kEastRepeatCountsAsHand`); starting score and limit are settings. Flowers are never offered to a player (`flowers = false`; only the control `reset` turns them on).

## Match

`Start(settings, seed)` (`BadSettings` unless `handsPerMatch` is a positive multiple of 4): one seed is one match (the draw for East, then one `MakeWall` per hand). 2000 each (`startingScore`). `EndHand()` settles, moves the deal, deals the next hand or finishes (`MatchPhase::Finished`, the last hand stays in `Hand()`). `ApplySettlement` feeds made-up settlements (tests). Bookkeeping is pure: `NextProgress`, `RoundWindOf`, `IsMatchOver`. East wins: East deals again; another seat wins: the deal moves on. Every hand counts, an East repeat too. Round wind `Wind(handsPlayed / (handsPerMatch / 4))`. A drawn hand with `replayDrawnHand` (default) is replayed by the same East and does not count. After the match, `East()` / `Winds()` describe the hand that would come next.

## Computer player

A computer never gets the `HandModel`: it gets `MakeTableView(model, seat)` (own hand, the public table, `visible` copies, `legal`), so **no peeking is true by construction**; the hints and the control API reuse the view. It chooses only from `view.legal`; its own `MahjongRng` only breaks ties.

- **Turn** (`ChooseTurnAction`), first rule that applies: (1) `DeclareWin`; (2) declare the calling hand with the discard that leaves the most unseen winning copies; (3) a kong when the hand without it is no worse; (4) a declared hand keeps the wait with the most unseen copies; (5) `BestDiscard`: lowest `Shanten`, then highest `AcceptanceOf`, then lowest `KeepValue`, then the rng. Seven pairs / orphans / limit hands are never aimed at.
- **Claims** (`ChooseClaimAction`): (1) mahjong whenever listed; (2) a waiting hand passes everything else; (3) a claim is acceptable when it lowers the shanten, or keeps it for a value pung/kong (`IsValueTile`); best by shanten, acceptance, `ClaimRank`, rng; (4) else `Pass`. No defence (a discard that wins costs the discarder nothing extra).
- **Levels** (`ComputerLevel.h`): `Easy` | `Normal` (default). A level is a `LevelProfile` of knobs (`skipAcceptancePercent`, `skipDeclarationPercent`, `skipClaimPercent`, `takeValueClaimsAtEqualShanten`); **a knob that is off draws nothing from the rng**, so Normal is exactly the player above and `ComputerLevelTest` pins it with golden scores (seeds 700..703). Easy's numbers were found by measuring (`ComputerStrengthTest`). Hard was removed by the owner (2026-11-09); it returns as `mahjong-bot-search` if that beats Normal. A player change is judged by the arena's paired row (`../Arena/CLAUDE.md`), not by a gtest of a few hundred hands.
- `ComputerTable`: four players, seat `i` seeded `SeatSeed(tableSeed, i)`; `NextActor`, `Step` (a refusal is a defect, returned as the error), `PlayHand`, `PlayMatch`. It knows no seat setup; that is `TableSession`.

## Table session

The one object every front end drives (control API, table component, HUD), so "who acts next" exists once. `StartMatch(settings, seed, seats, level)`; `SeatSetup` of `Person | Computer` (`kDefaultSeatSetup`: seat 0 a person). `GetPhase()`: `Playing`, `HandOver` (waits for `NextHand()`), `MatchOver`. `Actor()` = `NextActor` while playing. `StepComputer()` (one action; the table component paces it), `RunComputers()` (until a person must act; the control API), `PerformPerson(action)`, `NextHand()`. `Version()` grows on every visible change and never on a refused call. A match started through the non-const `GetMatch()` has no computers.

**Hot seat:** one person is always the `Viewer()`; none: `kNoSeat` (spectator); two or more: the person who acted last, reset when another person must act. `HandoffPending()` while that person is not the viewer; `Reveal()` fixes it.

**Resume:** `ResumeMatch(events, seats, level)` (declared in `MatchLog.h`) puts the session where a log left it: the log is checked on a scratch `Match` first, so a log that does not replay returns its `ReplayError` and changes nothing; otherwise it is replayed with the session's sink as the echo, a fresh `ComputerTable` is seeded from the last `Start`, and seats, level and viewer are set as `StartMatch` sets them. The computers' random state is not restored (the log has their actions, not their draws).

## Match log

`MatchLogEvent` (`Start` with seed, settings, seats, level; `Hand` with the whole deal; `Action` with seat, `by` and the `HandAction`; `Settlement`; `End`). **The hook is `TableSession::SetLogSink`**, so every front end is logged. Refused calls, `Reveal` and bad starts are not logged. `ReplayMatchLog(events, match, echo)` replays on a fresh `Match` and reports the first event that disagrees; a log may lack its `Hand` and `End` events. The optional `echo` sink gets every applied event as a live session emits them (its own `Hand` / `End`, not the log's), and sees events before a later one is found wrong, so a caller that must leave no trace checks on a scratch match first. The JSON form and the file are `../CLAUDE.md` ("Match log").
