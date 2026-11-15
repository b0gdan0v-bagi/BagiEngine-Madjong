# Claim charts

## What this is

`BagiMahjongCharts` answers one question over many hands: **in a claim window, when does a claim gain points over passing, and where
does the Normal player's rule disagree?** It is the poker chart of `docs/ev-analyzer.md` made for a whole class of positions: a table
of classes (kind of claim, tile, shanten before, stage of the wall, ...) with the mean points a claim gains over `Pass`, its 95 percent
interval and the number of decisions behind it.

How the numbers are made:

- One hand per seed, four Normal seats (the same walls and seat seeds as `BagiMahjongArena --table-stats`). A read-only observer
  collects every decision of `Phase::Claim` in which the seat's view lists `Pass` and at least one chow, pung or kong claim, and **no**
  mahjong claim: a win is taken at once by every level, so there is nothing to chart. The windows excluded for a win are counted and
  printed.
- Each such window is measured by the EV analyzer (`Analyzer::AnalyzePosition`) unchanged: the worlds and the rollouts of the search
  player, Normal on for all four seats, the points of this hand only. A **row** of the chart is one claim of the window (the three
  chows of one window are three rows); its value is the mean points of the claim minus the mean points of `Pass`, in the same worlds,
  so it is exactly the paired mean difference. The analysis seed of a window is a function of the hand's seed and the window's index
  and nothing else.
- The unit of an interval is the **seed**, as in the arena: for a class, the mean of the values of its rows in each seed, then the 95
  percent interval over the seeds that have a row (a seed without one is left out). The rows and the seeds behind a class are printed
  beside it.
- Every cell is split three ways: all its rows, the claims **Normal takes** and the claims **Normal passes**. A cell where Normal takes
  claims that lose points, or passes claims that gain them, is a class where Normal's rule is wrong.

The program only measures. It changes no player and nothing in `Game/`.

## The classes

Fixed here, before any run, so that no result can move them. A class is the seven fields below, a pure function of the seat's own
view and the claim (`ClassifyClaim` in `ArenaChartsClasses.h`; it cannot see the hidden tiles).

| Field | Bins | Why |
|-------|------|-----|
| kind | chow, pung, kong | the three claims differ in what they cost: a chow scores nothing, a pung scores a set, a kong takes three tiles and draws a loose one. Only the open kong of a discard is a claim; a concealed or upgraded kong is a turn action |
| tile group of the claimed tile | simple (ranks 2..8), terminal (rank 1 or 9), value honour (a dragon, the seat's wind, the round wind), other honour (the other winds) | a pung of a value honour scores a double, a terminal pung scores more than a simple one, an other-wind pung scores nothing special; the group is the first thing a player asks about a claim |
| shanten before | 0, 1, 2, 3 or more | the hand as it is, four sets and a pair only. 0 is a waiting hand: Normal claims nothing but a win there, so the cell shows what that costs |
| shanten change | lower, equal, higher | the shanten the claim leaves (`RateClaim`) against the shanten before: Normal takes a claim that lowers it, or keeps it for a value pung or kong |
| stage of the wall | early, middle, late | the live wall (`liveCount`) in thirds of the live wall at the deal (68 tiles with flowers off; the program reads it from the rules). Early is above two thirds, late up to the first third. A claim early has the whole hand left to use it, a late one may be a hand that never finishes |
| East | East or not | East pays and is paid double, so the same claim is worth more to it |
| open sets | 0, 1, 2 or more | the open melds of the seat before the claim (a concealed kong is not open): an open hand has fewer ways to win and to score |

The main table is **kind x shanten before x stage** (36 cells); the one-way tables are tile group, shanten change, East, open sets and
kind alone (a cell of a one-way table sums every row with that value, so one row is counted in each table).

**`kFewRows` = 30.** A class of fewer rows is `few`: it is shown and marked, never hidden and never called `significant`, because an
interval over a handful of seeds says nothing. The number is a guess at where the normal approximation of the interval stops being
absurd, and it is fixed here, not tuned to a result. A kong cell may well stay `few`: that is a result, not a defect.

## Build and run

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"          # the reflection generator needs jinja2
cmake --preset macos-release
cmake --build --preset macos-release --target BagiMahjongCharts
build/macos-release/bin/BagiMahjongCharts --first 1 --seeds 4 --worlds 50 --jobs 0
```

Options: `--first` (the first seed), `--seeds` (one hand each), `--worlds` (per window), `--jobs` (threads in all, 0 = one per core; whole
seeds each, so it helps up to the number of seeds), `--markdown` (the tables as markdown: the numbers of this doc are pasted from it,
never typed), `--help`. The same arguments give the same bytes at any `--jobs`. Exit codes as the arena: 0, 1 (a refused action or a
window that cannot be measured: a defect, the seed is printed), 2 (bad arguments). The program is not part of `ctest`; the code
(`src/Mahjong/Arena/ArenaCharts*`) is tested by `ArenaChartsClassesTest`, `ArenaChartsWindowsTest`, `ArenaChartsRunTest` and
`ArenaChartsReportTest`. Run it in Release: a window costs `--worlds` rollouts of whole hands.

## Protocol

Fixed here, before any number of the study was looked at (the commit that adds this section comes before the run's commit; the cost
runs below are on a range that is not used for anything else; of their output only the counters and the times are used, a table of
20 seeds is no basis for any class).

| Range | Command | Seeds | Worlds |
|-------|---------|-------|--------|
| discovery | `BagiMahjongCharts --first 200000 --seeds 2400 --worlds 100 --jobs 12 --markdown` | 200000 .. 202399 | 100 |
| confirmation | `BagiMahjongCharts --first 300000 --seeds 2400 --worlds 100 --jobs 12 --markdown` | 300000 .. 302399 | 100 |

- The two ranges are disjoint, of the same size and with the same worlds; neither touches the cost range (100000 .. 100019). The
  discovery range is run first and read first. The confirmation range is run once, with the same build, and nothing is tuned between
  the two: no class, no worlds, no seed count, no filter is changed after a number is seen. A run that fails (exit 1) is a defect,
  fixed with its own test, and the run starts again on the same ranges. No other range is run.
- The worlds are 100 a window (the default). A row's value is the mean over its worlds, and the spread between windows (a claim is
  worth tens of points in one position and minus tens in another) is far larger than the noise of the worlds of one row, so the
  seeds and rows are worth more than more worlds ("Cost" below).
- The discovery range is expected to hold about 20 000 rows (nine a seed). A kong cell, and any cell the thirds of the wall thin
  out, may stay `few`: that is a result.
- `--jobs 12` is the threads of one run on a machine that other loops share; the output does not depend on it.
- **How a finding is read.** The cells read are every cell of the main table and of the five one-way tables, each as its whole, its
  `Normal takes` sub-row and its `Normal passes` sub-row. A class **holds** when it is `significant` in the discovery range and its
  confirmation mean has the same sign and is `significant` too. A class that holds is a **finding**. A class that is `significant` in
  one range only, or whose confirmation has the other sign, **does not hold** and is said so. A `few` class that agrees in sign
  between the two ranges is a **lead**, not a finding: its number is shown with its count and nothing is built on it. Where Normal's
  rule is wrong is read from the sub-rows: a `Normal takes` row that holds with a negative gain (it takes what loses points), or a
  `Normal passes` row that holds with a positive gain (it passes what gains them).
- A finding is a statement about the table of this program (the caveats below), against three Normal bots and one hand's points.

## Cost

`BagiMahjongCharts --first 100000 --seeds 20 --worlds W --jobs 10` in Release (`macos-release`, 14 cores, shared with other loops:
load average 27 to 56 during the runs), run before the protocol was fixed; only the counters and the times are used. `user` is the CPU time
of all the threads.

| Worlds W | Wall, s | CPU (user), s | CPU per seed, s | Windows with a claim | Rows | Rows per seed |
|----------|---------|---------------|-----------------|----------------------|------|---------------|
| 100 | 11.5 | 58.0 | 2.9 | 168 | 180 | 9.0 |
| 250 | 15.1 | 126.7 | 6.3 | 168 | 180 | 9.0 |
| 500 | 29.6 | 256.6 | 12.8 | 168 | 180 | 9.0 |

The cost is linear in W and in the seeds. The rows do not depend on W (the same windows). Twenty seeds on ten threads are two rounds, so
the wall time of this small run is longer than its CPU time over ten; for a long run the wall time is about CPU divided by the cores
the machine really gives, which on a loaded machine was about eight. For the target of about 15 minutes a run and at least about
20 000 rows in the discovery range:

| W | CPU for 2400 seeds, s | Wall at about 8 cores, min | Rows |
|---|-----------------------|----------------------------|------|
| 100 | 6 960 | about 14.5 | about 21 600 |
| 250 | 15 120 | about 31 | about 21 600 |
| 500 | 30 720 | about 64 | about 21 600 |

W 250 or 500 would give 1 400 or 700 seeds in the same time, 12 600 or 6 300 rows: fewer rows and fewer seeds for an interval whose
width is set by the seeds and by the spread between windows, so W = 100 and 2400 seeds are chosen.

## Caveats

- **Against Normal bots.** The three other seats and the seat itself after the claim play Normal (level 5). A number says "against this
  table", never "the best move in mahjong": a defence the Normal bots do not make is not priced, and a person or a stronger bot would
  leave different numbers.
- **Hidden tiles are uniform.** The tiles the seat cannot see are drawn uniformly at random from what it cannot see
  (`SampleWorld`); real opponents' discards say more about their hands than that.
- **One hand's points.** The value is the change of the seat's score in this hand (`SettleHand`; a drawn hand pays 0). The standing of
  the match is not modeled: no last hands, no score to protect or to chase.
- **Normal play after the action.** Both the claim and `Pass` are followed by Normal's rule for every later decision, so a claim that is
  good only if played better afterwards is not credited.
- **Many cells, so some are significant by chance.** About one cell in twenty is `significant` at 95 percent with no effect at all, and
  the tables hold well over a hundred cells. A single `significant` cell is a lead. Only a class that holds in both ranges of the
  protocol is read as a finding.
- **What "Normal takes" means.** The row marked `Normal takes` is the claim the analyzer's Normal rule picks in that window. Where Normal
  breaks a tie between equal ratings (two chows of one window, say) it uses the analyzer's fixed tie-break seed, so the marked claim is
  one valid choice of Normal, not the only one it could make in a game. A window never marks more than one claim.
- **Windows left out.** A window in which the seat could win is counted and not measured; a window that cannot be analysed (no world
  can be sampled for the position) is counted as dropped and has no rows. Both counts are printed in the header of every report.
