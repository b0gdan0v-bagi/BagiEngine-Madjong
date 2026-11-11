# The bot arena

`BagiMahjongArena` is a command-line program that plays many hands of the computer players with no window and prints a table with
confidence intervals. It is the ruler for every change of a computer player (a level, a knob of `LevelProfile`, a stronger level, a
coach): "is it better, and by how much" is answered by a number with an interval, not by a feeling.

## What it is for

`ComputerStrengthTest` plays 240 hands, and a few hundred hands are what a gtest can afford in Debug. The noise of such a count is
large: the arena reads about 16 points a hand of half-width (the 95 percent interval) for Normal against three random seats on those
60 seeds, and about 4.7 on 1000 seeds, while the effects a lever of a computer player has are +1 to +4 points a hand (the Hard-lever
measurements of the entry `mahjong-computer-difficulty`: even an oracle that cheats gains only about that much against three Normal
seats). A Release build plays 1000 seeds (4000 hands for a focus player) in about 4 seconds, and the paired rows (below) make a
difference of a few points visible.

The program only measures. It reads `src/Mahjong/Game/` and changes nothing there; the numbers of a level are the owner's decision.

## Build and run

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"          # the reflection generator needs jinja2
cmake --preset macos-release
cmake --build --preset macos-release --target BagiMahjongArena
build/ClangmacOS/BagiMahjongArena --help
```

**A Release build overwrites the Debug executables and libraries in `build/ClangmacOS/`** (the same folder for every preset). Before the
next `MahjongTests` run: delete `build/ClangmacOS/lib/*.a` and build again with `macos-debug`. Run the arena in Release: a Debug
build is much slower. The program is not part of `ctest`; the library it is made of (`src/Mahjong/Arena/`) is tested by
`ArenaPlayerTest`, `ArenaRunTest`, `ArenaStatsTest`, `ArenaTableTest`, `ArenaCliTest` and, for the log mode, `ArenaLogsTest`, `ArenaLogsReportTest` and `ArenaTesterTest` in `MahjongTests`.

Commands (`A` is `build/ClangmacOS/BagiMahjongArena`; every run prints the same bytes twice, `--time` apart):

```bash
A --focus normal --others random --first 4000 --seeds 1000         # one player against three random seats
A --focus easy --others normal --seeds 1000                        # head to head: easy against three normal seats
A --focus normal,easy --others random --seeds 1000                 # several players on the same walls, with the paired rows
A --focus normal,normal:skipClaimPercent=20 --others normal        # a profile with knobs against the plain Normal
A --focus normal --seeds 100 --time                                # add the time per decision
A --focus normal,normal:searchWorlds=8 --others normal --seeds 1000 --jobs 0   # all the cores, the same bytes as --jobs 1
A --table-stats --first 4000 --seeds 1000 --jobs 14                # the table constants of the own-draws value (below)
```

Options: `--focus LIST` (default `normal`), `--others SPEC` (default `random`), `--first N` (default 4000), `--seeds COUNT` (default 250: 1000
hands per focus player), `--jobs N` (default 1; see below), `--time`, `--table-stats`, `--help`. Exit code 0 is done, 1 is an action the rules refused (a defect: the seed and the seat are
printed), 2 is a bad argument (printed with the usage).

**Threads.** `--jobs N` is N threads in all (1 to 256; `--jobs 0` is one thread per core, the hardware concurrency). The seeds come first: a seed is played whole by
one thread (all focus players, all four rotations) into a result slot of its own, and the slots are merged in seed order, so the report is the
same bytes at any N (only the `--time` table differs between runs, as always). When there are fewer seeds than N, each seed thread also owns
`N / seeds` threads (the integer division) that play the worlds of a search decision (`WorldThreads`), so the total never exceeds N and a
one-seed job uses all the cores; the worlds are summed in world order, so the answer is the same bytes too. `--time` is the wall time of a
decision: with the worlds level it is lower than the work in it, and the table sums the threads' microseconds, so it is not the wall time of the run. A hand the rules refuse stops the run, and the error printed is that of the lowest seed that
failed, whatever N is. The default stays 1. A search player (`searchWorlds`) plays hundreds of Normal decisions for each of its own, so use
`--jobs` for those runs; for the players without a search (`normal` and `easy`), on 14 cores in Debug, `--seeds 150` of two focus
players against `--others normal` takes 2.8 s instead of 21 s (against `--others random`: 2.5 s instead of 9.7 s). Time a decision at `--jobs 1`: the `--time` table sums the microseconds of all the threads, which
over-counts when the threads contend (shared cores, efficiency cores), so the Debug rule of 0.3 s for the slowest search decision
(`docs/bot-search.md`) is not read from a `--jobs 14` run.

A player is `random`, a level `1` .. `10` (`easy` and `normal` are the names of 1 and 5), or a level with knobs of `LevelProfile`, `<level>:<knob>=<value>,...` (the knobs are
`skipAcceptancePercent`, `skipDeclarationPercent`, `skipClaimPercent` (0..100), `takeValueClaimsAtEqualShanten` and `readDanger` (0 or 1: the danger tie-break against declared opponents, `DiscardDanger.h`; the two own-draws knobs, `ownDrawsDepth` and `ownDrawsPickWait`, are rated in `docs/own-draws-ev.md`) and the three knobs of
the search player, `searchWorlds` (0..4096, 0 = no search), `searchCandidates` (0..12, default 4) and `searchMargin` (0..100 tenths of a standard
error), see `CLAUDE.md`, "Computer player"; the level's profile is the start and the knobs are set on it). A search seat is slow: one decision
plays `searchWorlds` x candidates whole hands, so `normal:searchWorlds=2,searchCandidates=3` takes about 49 s on 20 seeds in Debug (one thread). In `--focus` a part with an `=` and no `:` goes on the knobs of the player before it,
so `normal,easy:skipClaimPercent=20,skipDeclarationPercent=10` is two players. (Hard was removed on the owner's decision and stays removed: the search player of
`mahjong-bot-search` did not beat Normal, so it is only these knobs; no level of the scale 1..10 uses them. Its measurements, its budget and the verdict are in
`docs/bot-search.md`.) `random` is the "seek win" seat of `ComputerStrengthTest` (wins when it can, declares when it can, builds a set
when it can, else any legal action).

## The conventions

They are those of `ComputerStrengthTest`, so the old numbers come out exactly:

- **Walls.** The seed `s` is the wall `MakeWall(MahjongRng(s), default settings, east)` with `east = s % 4`, round wind East, flowers
  off, one hand per seed. Every focus player plays the same walls, so any two of them can be compared hand by hand.
- **Seats.** For each seed the focus player plays each of the four seats in turn, against the "others" on the other three: four
  hands (one per seat, the rotations) per seed, so `--seeds 1000` is 4000 hands per focus player. A computer on seat `i` is
  `ComputerPlayer(SeatSeed(seed, i), level)`; the random seats share one `MahjongRng(seed * 13 + 5)`.
- **Seeds.** `--first 4000 --seeds 1000` is the seeds 4000 .. 4999. The seeds are a range you give, not a random draw, so a run is
  reproducible; the numbers below are for 4000 .. 4999.
- **Points** are the change of the focus seat in the hand (`deltas` of `SettleHand`); a drawn hand pays nothing.

## How to read the interval

- **The seed is the unit.** The four rotations of a seed play the same wall, so the four hands are not independent samples. A number
  is the mean over the seeds of the per-seed value (the four rotations divided by four) and `+-` is 1.96 standard errors of that mean,
  a 95 percent interval (a normal approximation: fine from about 100 seeds, rough below).
- **Two plain intervals that overlap do not mean "no difference".** The players play the same walls, so most of the luck of a wall is
  the same for both. Read the **paired difference** rows (`paired difference against <the first focus player>`): they take the
  difference of the two players seed by seed and the interval of that, which is much narrower. `significant` means that interval
  excludes 0.
- **The interval shrinks with the square root of the number of seeds:** to make a half-width ten times smaller you need a hundred
  times the seeds. A difference of 1 point a hand needs thousands of seeds, a few seconds of Release each thousand.
- **Do not pick the range after you have seen the result** (nor the knob value, nor the other player): decide the seeds first, or take
  a second range (`--first 20000`) to confirm what the first one suggests.
- **One `significant` row among twenty tried is expected by chance** (95 percent is one in twenty). Every knob value you try is one more
  try.
- Wins %, drawn % and declared % are per-seed means in the same way, not a binomial interval over the hands.

## Logs of real games

`A --logs PATH` reads match logs (`*.jsonl`, a folder searched recursively, or one file) instead of playing; what a log holds and how
the logs of testers are collected: `docs/tester-match-logs.md`. Options with it: `--check`, `--focus LIST` (the bots the people are
compared with; default `normal`; `random` is refused), `--differences N` (default 20; 0 lists none, the count of all is always
printed), `--jobs N`; `--first`, `--seeds`, `--others` and `--time` are refused. Exit code 0 done (also when files do not replay: they
are counted), 1 the path cannot be read, 2 a bad argument.

```bash
A --logs logs/matches --check                                  # one line per file: ok<TAB>name or bad<TAB>name<TAB>reason
A --logs logs/collected --focus normal,easy --jobs 0           # the corpus, the agreement, the real-game numbers, the differences
A --logs logs/matches/2026-11-09T18-30-10_seed1.jsonl --differences 0
```

- **`--check`** prints nothing but those lines, in name order, and exits 0 even when files are bad (`tools/collect_logs.py` relies on this
  format byte for byte). A log that does not parse is `unreadable: line N: ...`; one that parses and does not replay on the rules of
  this build is `does not replay: event N (Problem)`, so that a rules change that stops old logs replaying is told, not lost. A log that
  stops early (given up) is ok.
- **The corpus** is counted over the logs that replay; the dropped ones are counted by the kind of reason.
- **Agreement.** Every decision of a person seat (`by: person` in the log) with more than one legal action is a `TableView` built by the
  analyzer's `PositionsOfSeat` from what that seat saw, and each bot chooses on that view and nothing else; the tie-breaking rng is seeded
  from the log name, the hand and the action index, so a log gives one answer. Equal means the same action, `declareCalling` included.
  Shown for discards and claim windows apart and together, with a 95 % interval whose unit is the **match** (the mean of the per-match
  shares). Testers are not experts: this is how people play, not what is best.
- **Real games by the level of the computers met.** For every seat-hand of a person at a table with at least one computer seat, from
  the logged settlements: wins % (the person won), points a hand (the person's change), drawn %, declared % (the person declared a
  calling in that hand) and the average winning score; the same definitions as the arena's own table. The group is the level 1..10 when
  all the computers of the table have one level, else `mixed`; the match is the unit of the intervals; a hand that was not finished is
  not counted. Put beside each row the arena run with the computers of that level on the other seats, for example
  `A --focus normal --others <L> --first 4000 --seeds 1000`: the arena player takes each seat in turn, so as strong as the others it wins
  25 % of the hands. The sample of testers is small and the walls differ: read the order of magnitude.
- **Differences.** Where a person's action differs from the first bot, in log order: the log, the hand (from 1), the action index (from
  0), the seat, both actions in the notation of the analyzer, and the line `--log NAME --hand K --action I` for `BagiMahjongAnalyzer`.
- **`--jobs N`** plays whole matches in threads into slots of their own and merges them in name order: the same bytes at any N, and for
  files in any order (the report sorts by name). A match that replayed but whose positions cannot be built is named and counted, not
  hidden; its real-game numbers, which need no positions, still count.

## The first check

The first check reproduces the numbers of the Hard-lever measurements of `mahjong-computer-difficulty` (made with a throw-away harness
that the arena replaces). Seeds 4000 .. 4999, Release, on the machine it was written on (one thread, the default `--jobs 1`): the run of
1000 seeds takes 3.9 s for one focus player (60 seeds 0.26 s, 250 seeds 1.0 s, three focus players on 1000 seeds 12 s); `--jobs` divides
these by about the number of cores and does not change a byte of the report.

**(1) Normal against three random seats**, `A --focus normal --others random --first 4000 --seeds 1000`:

```
player                    hands            wins %           points/hand     drawn %  declared %   avg win score
normal                     4000     82.20 +- 1.20        155.16 +- 4.67       10.68       95.80           43.30
  others (per seat)        4000              2.38                -51.72       10.68       11.60           55.86
```

That is 3288 wins of 4000 (82.20 %), 620620 points (155.155 a hand), 427 drawn hands (10.68 %) and an average winning score of 43,
exactly the numbers of the old harness. The 60-seed run (`--seeds 60`) gives 186 wins of 240 (77.50 %) and +31730 points (132.21 a
hand), the numbers of `ComputerStrengthTest`.

**(2) Normal against three Normal seats**, `A --focus normal --others normal --first 4000 --seeds 1000`:

```
player                    hands            wins %           points/hand     drawn %  declared %   avg win score
normal                     4000     25.00 +- 0.00          0.00 +- 0.00        0.00       55.77           45.46
  others (per seat)        4000             25.00                  0.00        0.00       55.77           45.46
```

Exactly 1000 wins of 4000, 0 points, an interval of 0 (the mirror identity: the four rotations of a seed replay one game with the
seats turned, so the focus player wins exactly once in each of them). The 0 drawn hands is a fact of these 1000 walls and of
four Normal players (they all build a hand, and not one of the 1000 games ran the wall out), not a property of the arena: against
random seats 427 of the same walls are drawn. It is the same as the old harness found ("drawn hands 0 in that setup").

**(3) Easy, new numbers** (not expected from before): `A --focus easy --others random ...` and `A --focus easy --others normal ...`

```
easy against three random seats
player                    hands            wins %           points/hand     drawn %  declared %   avg win score
easy                       4000     40.45 +- 1.50         77.34 +- 4.91       45.42       59.55           47.93
  others (per seat)        4000              4.71                -25.78       45.42       19.50           50.44

easy against three Normal seats
easy                       4000      7.33 +- 0.78        -47.92 +- 2.85        0.40       18.27           44.51
  others (per seat)        4000             30.76                 15.97        0.40       62.32           43.74
```

Easy wins about half of Normal's wins against random seats (1618 wins against 3288) and earns half the points, and against three
Normal seats it wins 293 of its 4000 hands (a fair share would be 1000) and loses 48 points a hand. With both on the same walls
(`--focus normal,easy --others random`) the paired row is -77.82 +- 6.30 points a hand and -41.75 +- 1.90 wins %, both `significant`.

**(4) One of the Hard levers, the suit focus** (lever A of the entry's measurements: aim at one suit when the hand holds at least 5 tiles
of it, with a tolerance of 6 copies of acceptance, off-suit claims not passed). The lever is not in the tree: it was applied in a
scratch state (`experiments.diff` and `ComputerAim.*` of the measurements, with three rows added to `kProfileKnobs`), run, and
removed again. Normal and the lever on the same walls, seeds 4000 .. 4999 (the name of the lever's row is shortened and the
unused columns are cut):

```
others: random
normal                                                  4000     82.20 +- 1.20        155.16 +- 4.67       10.68       95.80           43.30
lever A (flushAimTiles=5,flushAcceptanceTolerance=6)    4000     77.78 +- 1.25        163.16 +- 6.01       14.43       92.97           48.04
paired difference                                       points/hand +8.01 +- 5.73 significant       wins % -4.42 +- 1.46 significant

others: normal
normal                                                  4000     25.00 +- 0.00          0.00 +- 0.00        0.00       55.77           45.46
lever A                                                 4000     22.23 +- 1.11         -2.94 +- 3.75        0.17       49.83           48.14
paired difference                                       points/hand -2.94 +- 3.75                   wins % -2.77 +- 1.11 significant
```

That is 3111 wins and +652650 points against random seats and 889 wins and -11740 points against three Normal seats, again the numbers of
the old harness. The reading is the one of the entry: the lever earns about 8 more points a hand against random seats at 4 percent fewer
wins, and against equal seats it wins fewer hands (the points are not significantly below Normal's here, the wins are), so it is no
stronger than Normal head to head.

## How a player wins and loses

Under the main table the report prints a second one, "how the hands end", with the same rows (the focus player, and `others (per seat)`
without intervals). The main table says how often a player wins; this one says how it wins and what it pays when another seat does.
Terms:

- **Lost hand:** a hand another seat won (a drawn hand is neither won nor lost). This is the "standing" of `docs/own-draws-ev.md`.
- **gave win %:** the lost hands whose winning tile the seat discarded (a win on a discard) or whose kong it owned (a robbed kong), as a
  percent of all the seat's hands. A win by a self-draw is nobody's gift. It has an interval, and a paired row.
- **self-draw % of wins:** the seat's wins that were self-draws, of its wins (a ratio of the totals, no interval).
- **standing score:** the mean rounded score (`ScoreSeat`) the seat holds over its lost hands: the number the own-draws value uses for the
  hand it does not win. It has an interval over the seeds (a seed's standing scores divided by the seed's lost hands; a seed in which the
  focus player lost no hand has no value and is left out, and a paired difference uses the seeds where both players have one), and a
  paired row.
- **points/lost hand:** the mean change of the seat's points over its lost hands (a ratio of the totals, no interval).

Read the columns together with the win rate: a player can win often and still give many wins.
(The Suphx paper, arXiv 2003.13590, reads the win rate with the deal-in rate for the same reason.) The table is always printed; the main
table and the paired table are the same bytes with or without it.

Normal and Easy against three Normal seats, `A --focus normal,easy --others normal --first 4000 --seeds 1000`, Release (the main-table
lines are those of "The first check" (2) and (3)):

```
how the hands end
player                        gave win %  self-draw % of wins      standing score  points/lost hand
normal                     19.10 +- 0.66                23.60        9.18 +- 0.40            -69.65
  others (per seat)                19.10                23.60                9.18            -69.65
easy                       23.98 +- 1.32                22.18        8.71 +- 0.33            -68.15
  others (per seat)                17.28                24.00                9.46            -66.09

paired difference against normal (the same seeds), how the hands end
player                        gave win %                   standing score
easy                       +4.88 +- 1.42  significant       -0.47 +- 0.35  significant
```

Normal against three Normal is the mirror, so its row and its others' row are equal. Easy gives the win on about 24 of 100 hands against
Normal's 19 (the paired row, `significant`) and wins 7.33 % of its hands against 25.00 %, while the share of its wins that are self-draws
(22.18 against 23.60) and what a lost hand costs it (-68.15 against -69.65 points) are close to Normal's: the standing score differs by
half a point of about nine.

## Adding a knob for an experiment

A knob is a field of `LevelProfile` (`src/Mahjong/Game/ComputerLevel.h`) that the choosers read. To make it a thing the arena can set from
the command line, add one row to `kProfileKnobs` in `src/Mahjong/Arena/ArenaPlayer.h` (the name of the field, a pointer to it, the largest
value) and raise `kProfileFieldCount`; the structured binding in `ArenaPlayer.cpp` that names every field stops compiling until it
lists the new one, and `ArenaPlayerTest` fails when a row does not move its own field. Then `--focus normal,normal:<knob>=<value>`
measures it against Normal, paired.

## The table statistics

`--table-stats` does not compare players: it plays each seed once with four Normal seats (`--first`, `--seeds`, `--jobs`; it refuses `--focus`,
`--others` and `--time`) and prints what the own-draws value of `docs/own-draws-ev.md` needs as constants: for each number of declared
opponents `d` (0..3) the own turns observed, the per mille chance that another seat wins in the next round and the survival after 1..4 rounds
next to `(1 - h)^i`; the mean score of a winner and the mean standing score of a seat that did not win, East and not East, with intervals; the
share of the won hands won on a discard, on a self-draw and on a robbed kong. A round is the stretch between a seat's own turn and its next one.
The same bytes at any `--jobs`; the measured numbers are in `docs/own-draws-ev.md`.

## What is not built

A line-up of four different players (round robin) or two against two: one against three covers the measurements so far, and a shell
loop over `--others` covers the rest. A Windows build of the arena is untried (the target is plain C++).
