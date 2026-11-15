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
| shanten before | 0, 1, 2, 3 or more | the hand as it is, four sets and a pair only. 0 is a waiting hand: Normal claims no chow and no pung there, and a kong only when the hand stays waiting and it rates the kong better, so the cell shows what the claims it passes cost |
| shanten change | lower, equal, higher | the shanten the claim leaves (`RateClaim`) against the shanten before: Normal takes a claim that lowers it, or keeps it for a value pung or kong |
| stage of the wall | early, middle, late | the live wall (`liveCount`) in thirds of the live wall at the deal (68 tiles with flowers off; the program reads it from the rules). Early is above two thirds, late up to the first third. A claim early has the whole hand left to use it, a late one may be a hand that never finishes |
| East | East or not | East pays and is paid double, so the same claim is worth more to it |
| open sets | 0, 1, 2 or more | the open melds of the seat before the claim (a concealed kong is not open): an open hand has fewer ways to win and to score |

Correction after the study: the "Why" text of the shanten-before row said Normal claims nothing but a win at 0; it was rewritten to what
Normal's claim rule does there. The bins and the fields are unchanged.

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

W 250 or 500 would give about 1 100 or 550 seeds in the same time, about 9 900 or 4 900 rows: fewer rows and fewer seeds for an interval whose
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
  So `Normal passes` means **Normal does not take this claim**, not that it passes the window: in a window with a pung and a kong of
  the same tile, or two chows, it may take the other claim. The tables cannot tell the two apart.
- **Windows left out.** A window in which the seat could win is counted and not measured; a window that cannot be analysed (no world
  can be sampled for the position) is counted as dropped and has no rows. Both counts are printed in the header of every report.

## How to read it

A line of a table is a class and a claim kind of that class: `all` is every claim of the class, `Normal takes` the claims Normal's rule
takes, `Normal passes` the claims it does not take (see the caveat). The number is the mean points a claim gains over passing in the same
worlds, then the half-width of its 95 percent interval over the seeds; `rows` are the claims behind it, `seeds` the hands that have
one. Positive: claiming is better than passing in that class; negative: passing is better. Examples, with the discovery range and the
confirmation range one after the other:

- **A chow with the hand waiting (0 shanten), middle of the wall: -21.9 +- 1.9 (1587 rows), then -22.2 +- 1.9 (1523 rows). Pass is
  better by about 22 points.** Normal takes none of them. The same holds early (-22.5 and -20.4) and late (-19.6 and -25.0).
- **A pung at 1 shanten, early: +35.7 +- 5.2 (1173 rows), then +37.2 +- 4.4 (1163 rows). The claim is better by about 36 points.**
  The ones Normal takes gain +63.3 and +62.7, the ones it does not take lose -6.7 and -6.7 (both significant in both ranges): its
  rule separates the good pungs from the bad ones.
- **A chow at 2 shanten, middle of the wall: +3.4 +- 2.0 and +2.5 +- 1.8, a small gain on the whole.** The chows Normal takes gain
  +12.7 and +13.3, the ones it passes lose -11.9 and -12.9. A chow is worth taking only when it is the right one.
- **A claim (any kind) that raises the shanten ("higher"): -64.0 +- 5.1 and -62.5 +- 4.8.** Never take it; Normal never does (no row of
  this class is one it takes).

A class mean is the average of many different claims. Where the mean of the claims Normal takes is positive, some of them still lose
in their own hand: the charts say which classes, not which claims. The `all` mean of a class depends on how often the table offers the
good claims, and `Normal takes` is mostly above `all` and `Normal passes` below it, because the rule picks claims for a reason (a class
where it is not is one where the rule picks badly). The question the tables answer is whether the **sign** of a sub-row is the sign Normal acts on.

## What Normal does against it

By the "Protocol" above, a class holds when it is `significant` in both ranges with the same sign. The 153 numbers read (the 36 cells of
the main table and the 15 values of the five one-way tables, each as `all`, `Normal takes` and `Normal passes`) fall in these groups,
by the two ranges together:

| Group | Numbers |
|-------|---------|
| hold (significant in both, same sign) | 90 |
| a `few` number in a range, same sign in both: leads | 22 |
| significant in a range, not in the other (both with 30 rows or more), same sign: do not hold | 6 |
| the two ranges have the other sign: do not hold | 2 |
| neither range significant | 8 |
| no number in a range (no rows) | 25 |

**Where Normal takes a claim that loses points: none.** No `Normal takes` sub-row of the main table or of the one-way tables is
negative in either range, but one: a chow at 3 or more shanten, late, which Normal took twice in the confirmation range (-1.9 +- 6.6,
2 rows, `few`; none in the discovery range): nothing to read. The weakest that holds is a chow at 3 or more shanten, early
(+2.2 +- 1.1 and +2.4 +- 1.1, 2033 and 2048 rows): a small gain.

**Where Normal does not take a claim that gains points** (all hold unless said):

- **A kong of the discard.** The kongs Normal does not take gain +17.8 +- 4.8 (309 rows) and +17.6 +- 5.6 (311 rows); Normal takes
  145 and 128 kongs with +88.3 and +86.7. By cell the same holds, in both ranges, at 1 shanten early (+17.3 and +15.0) and middle
  (+18.0 and +30.6), at 2 shanten early (+26.3 and +25.5) and at 3 or more shanten early (+27.5 and +19.4), each of 52 to 76 rows.
  The reading is limited: a kong claim of a discard is offered together with the pung of the same tile, and a row of `Normal passes`
  may be a window where Normal took the pung. So this is not "Normal passes a kong that is worth +18" but "the kong that Normal does not
  take is worth +18 over passing", and whether the pung it took is worth more is not in the tables.
- **A value honour** (a dragon, the seat's wind or the round wind; a pung or a kong). Normal does not take 232 and 203 of them, which gain
  +30.3 +- 18.2 and +27.3 +- 16.0. The intervals are wide and the claims are a mix (pung or kong, any shanten); the tables do not say
  in which window the pass is wrong.
- **A claim that lowers the shanten.** Normal does not take 813 and 787 of them, which gain +3.1 +- 2.1 and +3.3 +- 2.1: small, and a
  window in which Normal took another claim is among them.

All the other `Normal passes` sub-rows that hold are negative (a chow in every class, a pung at 1 shanten: -6.7 and -6.7 early, -6.9
and -5.1 middle, a claim at equal shanten: -14.5 and -14.8, a claim that raises it: -64.0 and -62.5): Normal's passes there are right.

**Leads, not findings** (a `few` class that agrees in sign in the two ranges; each with its rows, the row counts are in the table):
Normal takes the kong of a waiting hand early (+95.9 and +87.7, 16 and 13 rows) and of a hand at 1, 2 or 3 or more shanten (+63.6 to
+131.7); the kongs it does not take at 2 shanten in the middle (+25.7 and +12.3, 11 and 19 rows) and the pungs it does not take at 3 or more
shanten in the middle (+12.2 and +19.2, 4 and 9 rows) gain. No change is made on these.

**Do not hold** (a number that was significant in one range and not in the other, or the two ranges with the other sign), with all
numbers (the two tables below are cut out of the two outputs under "Results" by class, nothing is recomputed):

| class | claims | D rows | D gain +- 95 % | D | C rows | C gain +- 95 % | C |
|---|---|---:|---:|---|---:|---:|---|
| kong, 0 shanten, late | all | 1 | -21.70 | few | 13 | +72.30 +- 56.19 | few |
| chow, 1 shanten, late | all | 113 | -0.21 +- 6.24 |  | 144 | +8.60 +- 7.02 | significant |
| chow, 2 shanten, early | all | 3422 | -1.65 +- 1.25 | significant | 3228 | -0.18 +- 1.30 |  |
| chow, 3+ shanten, middle | Normal takes | 81 | +3.26 +- 3.49 |  | 93 | +3.84 +- 2.97 | significant |
| chow, 3+ shanten, middle | Normal passes | 30 | -6.96 +- 7.71 |  | 36 | -13.19 +- 5.62 | significant |
| pung, 0 shanten, middle | all | 558 | -7.87 +- 9.95 |  | 498 | -13.12 +- 5.73 | significant |
| pung, 0 shanten, middle | Normal passes | 558 | -7.87 +- 9.95 |  | 498 | -13.12 +- 5.73 | significant |
| tile group: other honour | Normal passes | 215 | +5.20 +- 5.06 | significant | 209 | +5.09 +- 5.84 |  |

**Leads** (`D` is the discovery range, `C` the confirmation range):

| class | claims | D rows | D gain +- 95 % | D | C rows | C gain +- 95 % | C |
|---|---|---:|---:|---|---:|---:|---|
| chow, 2 shanten, late | all | 9 | +19.40 +- 14.56 | few | 15 | +10.77 +- 11.03 | few |
| chow, 2 shanten, late | Normal takes | 6 | +28.07 +- 12.08 | few | 13 | +10.72 +- 11.06 | few |
| chow, 2 shanten, late | Normal passes | 3 | -6.60 +- 16.86 | few | 2 | -3.00 +- 1.76 | few |
| pung, 1 shanten, late | Normal takes | 25 | +46.72 +- 12.00 | few | 39 | +57.59 +- 13.61 | significant |
| pung, 1 shanten, late | Normal passes | 10 | -0.82 +- 18.52 | few | 12 | -0.39 +- 13.34 | few |
| pung, 2 shanten, late | all | 7 | +36.59 +- 15.32 | few | 5 | +15.14 +- 10.19 | few |
| pung, 2 shanten, late | Normal takes | 7 | +36.59 +- 15.32 | few | 5 | +15.14 +- 10.19 | few |
| pung, 3+ shanten, middle | Normal passes | 4 | +12.20 +- 18.44 | few | 9 | +19.21 +- 12.41 | few |
| kong, 0 shanten, early | all | 29 | +43.57 +- 30.55 | few | 24 | +44.81 +- 37.55 | few |
| kong, 0 shanten, early | Normal takes | 16 | +95.91 +- 36.31 | few | 13 | +87.65 +- 53.36 | few |
| kong, 0 shanten, early | Normal passes | 13 | -20.86 +- 20.54 | few | 11 | -5.83 +- 34.91 | few |
| kong, 0 shanten, middle | Normal passes | 27 | -7.69 +- 19.90 | few | 25 | -9.38 +- 17.81 | few |
| kong, 0 shanten, late | Normal passes | 1 | -21.70 | few | 3 | -31.97 +- 24.75 | few |
| kong, 1 shanten, early | Normal takes | 26 | +83.85 +- 16.98 | few | 20 | +88.88 +- 44.65 | few |
| kong, 1 shanten, middle | Normal takes | 14 | +131.66 +- 86.25 | few | 13 | +72.06 +- 13.15 | few |
| kong, 1 shanten, late | all | 4 | +14.10 +- 17.64 | few | 2 | +36.30 +- 66.84 | few |
| kong, 1 shanten, late | Normal passes | 4 | +14.10 +- 17.64 | few | 2 | +36.30 +- 66.84 | few |
| kong, 2 shanten, early | Normal takes | 22 | +88.69 +- 23.90 | few | 14 | +117.64 +- 86.13 | few |
| kong, 2 shanten, middle | all | 16 | +44.41 +- 20.27 | few | 24 | +32.95 +- 18.92 | few |
| kong, 2 shanten, middle | Normal takes | 5 | +85.52 +- 43.79 | few | 5 | +111.42 +- 34.22 | few |
| kong, 2 shanten, middle | Normal passes | 11 | +25.73 +- 10.90 | few | 19 | +12.29 +- 8.49 | few |
| kong, 3+ shanten, early | Normal takes | 15 | +63.56 +- 21.79 | few | 17 | +84.35 +- 28.09 | few |

## Left to a better player

The input of a later entry, with no change made here. A rule that does better than Normal has to change a class that holds; the classes
are these:

1. **The kong of a discard.** The kongs Normal leaves gain about +18 over passing in 309 and 311 rows, at every shanten from 1 early.
   The measure to take next is a kong against the pung of the same tile in the same window, which these tables cannot give (they
   compare each claim with `Pass`).
2. **The value honour that Normal does not take.** About +30 over passing, in about 200 claims a range, with wide intervals. A rule that
   takes it needs a feature inside the class (the shanten, the stage, the count of the tile) that the seven classes do not carry.
3. **The claims that lower the shanten and are not taken** (+3, small): only worth a rule if a feature says which.
4. **Nothing for the chows and the pungs.** In every class that holds, the sign of `Normal takes` is positive and that of `Normal passes`
   is negative: the rule's boundary is right at the level of these classes. A better rule has to find its gain inside a class
   (which chow of the two, which hand at 2 shanten), not between classes; that needs a window-level measure, not a table of class
   means.
5. **A waiting hand.** The chow costs about 22 points at every stage; the pung costs nothing measurable early (+0.3 and -3.0), about -7.9 and -13.1 in the middle (does not hold) and -26.4 and -16.7
   late (57 and 78 rows, holds); Normal passes every one of them, so nothing changes there either. The kong of a waiting hand, which
   Normal takes in the middle (+80.7 and +68.8, 47 and 36 rows), holds as a gain.

## Results

Run on 2026-11-15 with `BagiMahjongCharts` built in Release from the sources of commit `61785ae` (the commit that follows it adds this
document's protocol and nothing else), on a machine shared with other loops: the discovery run took 999 s and the confirmation run
1082 s with 12 threads, load average 27 to 78. Both runs exit with code 0, `not analysable 0`. The numbers below are the program's
output, pasted unchanged (the tables of the two ranges are the same classes, so a cell is found by its name in both).

### Discovery range

`build/macos-release/bin/BagiMahjongCharts --first 200000 --seeds 2400 --worlds 100 --jobs 12 --markdown`

Claim charts: seeds 200000 .. 202399 (2400 seeds), 100 worlds per window.

hands 2400, windows with a claim 21871, left out for a listed win 1833, not analysable 0, rows 23009.

gain = mean points of the claim minus points of passing, in the same worlds; +- is 1.96 standard errors over the seeds (a 95 percent interval), a seed is one sample; a cell of fewer than 30 rows is few: shown, never significant.

caveat: against three Normal bots, hidden tiles uniform, the points of this hand only; about one significant cell in 20 is chance, so read only what holds in both ranges (docs/charts-claims.md).

#### Main table: kind x shanten before x stage of the wall

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| chow, 0 shanten, early | all | 578 | 418 | -22.49 +- 2.66 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 578 | 418 | -22.49 +- 2.66 | significant |
| chow, 0 shanten, middle | all | 1587 | 846 | -21.87 +- 1.86 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 1587 | 846 | -21.87 +- 1.86 | significant |
| chow, 0 shanten, late | all | 197 | 105 | -19.55 +- 3.79 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 197 | 105 | -19.55 +- 3.79 | significant |
| chow, 1 shanten, early | all | 2565 | 1397 | -3.20 +- 1.75 | significant |
|  | Normal takes | 800 | 713 | +26.95 +- 2.61 | significant |
|  | Normal passes | 1765 | 1056 | -19.41 +- 1.47 | significant |
| chow, 1 shanten, middle | all | 2376 | 1106 | +1.80 +- 2.03 |  |
|  | Normal takes | 852 | 687 | +31.12 +- 2.87 | significant |
|  | Normal passes | 1524 | 829 | -18.46 +- 1.64 | significant |
| chow, 1 shanten, late | all | 113 | 67 | -0.21 +- 6.24 |  |
|  | Normal takes | 32 | 29 | +24.88 +- 8.70 | significant |
|  | Normal passes | 81 | 51 | -12.05 +- 4.80 | significant |
| chow, 2 shanten, early | all | 3422 | 1700 | -1.65 +- 1.25 | significant |
|  | Normal takes | 1698 | 1256 | +11.55 +- 1.54 | significant |
|  | Normal passes | 1724 | 1106 | -15.96 +- 1.27 | significant |
| chow, 2 shanten, middle | all | 868 | 542 | +3.44 +- 2.02 | significant |
|  | Normal takes | 502 | 425 | +12.67 +- 2.23 | significant |
|  | Normal passes | 366 | 258 | -11.86 +- 2.54 | significant |
| chow, 2 shanten, late | all | 9 | 8 | +19.40 +- 14.56 | few |
|  | Normal takes | 6 | 6 | +28.07 +- 12.08 | few |
|  | Normal passes | 3 | 2 | -6.60 +- 16.86 | few |
| chow, 3+ shanten, early | all | 3035 | 1673 | -2.46 +- 0.97 | significant |
|  | Normal takes | 2033 | 1470 | +2.22 +- 1.05 | significant |
|  | Normal passes | 1002 | 758 | -12.38 +- 1.40 | significant |
| chow, 3+ shanten, middle | all | 111 | 84 | +0.54 +- 3.59 |  |
|  | Normal takes | 81 | 76 | +3.26 +- 3.49 |  |
|  | Normal passes | 30 | 23 | -6.96 +- 7.71 |  |
| chow, 3+ shanten, late | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |
| pung, 0 shanten, early | all | 223 | 206 | +0.34 +- 12.08 |  |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 223 | 206 | +0.34 +- 12.08 |  |
| pung, 0 shanten, middle | all | 558 | 435 | -7.87 +- 9.95 |  |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 558 | 435 | -7.87 +- 9.95 |  |
| pung, 0 shanten, late | all | 57 | 42 | -26.38 +- 11.78 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 57 | 42 | -26.38 +- 11.78 | significant |
| pung, 1 shanten, early | all | 1173 | 954 | +35.71 +- 5.19 | significant |
|  | Normal takes | 679 | 623 | +63.26 +- 7.44 | significant |
|  | Normal passes | 494 | 422 | -6.69 +- 3.69 | significant |
| pung, 1 shanten, middle | all | 940 | 699 | +27.15 +- 3.98 | significant |
|  | Normal takes | 569 | 482 | +49.07 +- 5.23 | significant |
|  | Normal passes | 371 | 320 | -6.91 +- 3.40 | significant |
| pung, 1 shanten, late | all | 35 | 32 | +31.41 +- 12.26 | significant |
|  | Normal takes | 25 | 23 | +46.72 +- 12.00 | few |
|  | Normal passes | 10 | 10 | -0.82 +- 18.52 | few |
| pung, 2 shanten, early | all | 1828 | 1352 | +34.70 +- 2.77 | significant |
|  | Normal takes | 1464 | 1175 | +42.72 +- 3.02 | significant |
|  | Normal passes | 364 | 333 | -1.97 +- 3.42 |  |
| pung, 2 shanten, middle | all | 415 | 359 | +29.95 +- 3.69 | significant |
|  | Normal takes | 342 | 313 | +34.77 +- 3.94 | significant |
|  | Normal passes | 73 | 69 | +2.77 +- 5.09 |  |
| pung, 2 shanten, late | all | 7 | 7 | +36.59 +- 15.32 | few |
|  | Normal takes | 7 | 7 | +36.59 +- 15.32 | few |
|  | Normal passes | 0 | 0 |  | few |
| pung, 3+ shanten, early | all | 2397 | 1586 | +24.98 +- 1.41 | significant |
|  | Normal takes | 2219 | 1518 | +27.34 +- 1.54 | significant |
|  | Normal passes | 178 | 164 | -3.02 +- 4.73 |  |
| pung, 3+ shanten, middle | all | 61 | 60 | +23.20 +- 5.90 | significant |
|  | Normal takes | 57 | 57 | +23.34 +- 6.19 | significant |
|  | Normal passes | 4 | 4 | +12.20 +- 18.44 | few |
| pung, 3+ shanten, late | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |
| kong, 0 shanten, early | all | 29 | 29 | +43.57 +- 30.55 | few |
|  | Normal takes | 16 | 16 | +95.91 +- 36.31 | few |
|  | Normal passes | 13 | 13 | -20.86 +- 20.54 | few |
| kong, 0 shanten, middle | all | 74 | 74 | +48.44 +- 18.06 | significant |
|  | Normal takes | 47 | 47 | +80.69 +- 21.17 | significant |
|  | Normal passes | 27 | 27 | -7.69 +- 19.90 | few |
| kong, 0 shanten, late | all | 1 | 1 | -21.70 | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 1 | 1 | -21.70 | few |
| kong, 1 shanten, early | all | 91 | 88 | +36.98 +- 9.34 | significant |
|  | Normal takes | 26 | 26 | +83.85 +- 16.98 | few |
|  | Normal passes | 65 | 62 | +17.32 +- 6.68 | significant |
| kong, 1 shanten, middle | all | 81 | 81 | +37.67 +- 19.27 | significant |
|  | Normal takes | 14 | 14 | +131.66 +- 86.25 | few |
|  | Normal passes | 67 | 67 | +18.03 +- 10.40 | significant |
| kong, 1 shanten, late | all | 4 | 4 | +14.10 +- 17.64 | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 4 | 4 | +14.10 +- 17.64 | few |
| kong, 2 shanten, early | all | 85 | 84 | +42.55 +- 10.33 | significant |
|  | Normal takes | 22 | 22 | +88.69 +- 23.90 | few |
|  | Normal passes | 63 | 63 | +26.30 +- 7.79 | significant |
| kong, 2 shanten, middle | all | 16 | 16 | +44.41 +- 20.27 | few |
|  | Normal takes | 5 | 5 | +85.52 +- 43.79 | few |
|  | Normal passes | 11 | 11 | +25.73 +- 10.90 | few |
| kong, 2 shanten, late | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |
| kong, 3+ shanten, early | all | 72 | 71 | +35.15 +- 11.71 | significant |
|  | Normal takes | 15 | 15 | +63.56 +- 21.79 | few |
|  | Normal passes | 57 | 56 | +27.53 +- 13.02 | significant |
| kong, 3+ shanten, middle | all | 1 | 1 | +33.50 | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 1 | 1 | +33.50 | few |
| kong, 3+ shanten, late | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |

#### By kind

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| chow | all | 14861 | 2361 | -5.16 +- 0.69 | significant |
|  | Normal takes | 6004 | 2202 | +12.86 +- 0.92 | significant |
|  | Normal passes | 8857 | 2203 | -18.22 +- 0.74 | significant |
| pung | all | 7694 | 2306 | +25.95 +- 1.62 | significant |
|  | Normal takes | 5362 | 2199 | +38.29 +- 1.79 | significant |
|  | Normal passes | 2332 | 1395 | -4.42 +- 3.15 | significant |
| kong | all | 454 | 420 | +40.23 +- 6.14 | significant |
|  | Normal takes | 145 | 141 | +88.33 +- 13.02 | significant |
|  | Normal passes | 309 | 288 | +17.84 +- 4.81 | significant |

#### By tile group of the claimed tile

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| simple | all | 15655 | 2360 | -0.39 +- 0.78 |  |
|  | Normal takes | 6825 | 2252 | +18.02 +- 1.00 | significant |
|  | Normal passes | 8830 | 2176 | -16.13 +- 0.89 | significant |
| terminal | all | 4634 | 2063 | +2.79 +- 1.14 | significant |
|  | Normal takes | 2413 | 1594 | +17.17 +- 1.34 | significant |
|  | Normal passes | 2221 | 1357 | -14.62 +- 1.43 | significant |
| value honour | all | 1935 | 1366 | +65.22 +- 3.69 | significant |
|  | Normal takes | 1703 | 1310 | +68.00 +- 3.74 | significant |
|  | Normal passes | 232 | 206 | +30.34 +- 18.18 | significant |
| other honour | all | 785 | 639 | +19.09 +- 1.98 | significant |
|  | Normal takes | 570 | 525 | +22.51 +- 2.12 | significant |
|  | Normal passes | 215 | 146 | +5.20 +- 5.06 | significant |

#### By shanten change of the claim

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| lower | all | 11919 | 2374 | +23.53 +- 1.08 | significant |
|  | Normal takes | 11106 | 2374 | +24.49 +- 1.10 | significant |
|  | Normal passes | 813 | 640 | +3.08 +- 2.09 | significant |
| equal | all | 10754 | 2251 | -11.27 +- 0.95 | significant |
|  | Normal takes | 405 | 380 | +62.93 +- 5.90 | significant |
|  | Normal passes | 10349 | 2236 | -14.49 +- 0.91 | significant |
| higher | all | 336 | 281 | -64.00 +- 5.05 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 336 | 281 | -64.00 +- 5.05 | significant |

#### By East

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| east | all | 5734 | 2209 | +11.79 +- 2.37 | significant |
|  | Normal takes | 2792 | 1832 | +35.01 +- 2.90 | significant |
|  | Normal passes | 2942 | 1435 | -17.58 +- 2.66 | significant |
| not east | all | 17275 | 2386 | +6.27 +- 0.77 | significant |
|  | Normal takes | 8719 | 2354 | +22.12 +- 0.91 | significant |
|  | Normal passes | 8556 | 2177 | -12.29 +- 0.88 | significant |

#### By open sets before the claim

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| 0 | all | 13999 | 2391 | +5.11 +- 0.87 | significant |
|  | Normal takes | 7481 | 2376 | +21.20 +- 1.03 | significant |
|  | Normal passes | 6518 | 1968 | -17.85 +- 0.94 | significant |
| 1 | all | 7248 | 2193 | +16.17 +- 1.94 | significant |
|  | Normal takes | 3368 | 1920 | +35.20 +- 2.30 | significant |
|  | Normal passes | 3880 | 1583 | -7.63 +- 1.69 | significant |
| 2+ | all | 1762 | 1007 | +15.07 +- 3.78 | significant |
|  | Normal takes | 662 | 583 | +42.45 +- 4.64 | significant |
|  | Normal passes | 1100 | 684 | -7.39 +- 5.65 | significant |

### Confirmation range

`build/macos-release/bin/BagiMahjongCharts --first 300000 --seeds 2400 --worlds 100 --jobs 12 --markdown`

Claim charts: seeds 300000 .. 302399 (2400 seeds), 100 worlds per window.

hands 2400, windows with a claim 21936, left out for a listed win 1900, not analysable 0, rows 23025.

gain = mean points of the claim minus points of passing, in the same worlds; +- is 1.96 standard errors over the seeds (a 95 percent interval), a seed is one sample; a cell of fewer than 30 rows is few: shown, never significant.

caveat: against three Normal bots, hidden tiles uniform, the points of this hand only; about one significant cell in 20 is chance, so read only what holds in both ranges (docs/charts-claims.md).

#### Main table: kind x shanten before x stage of the wall

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| chow, 0 shanten, early | all | 608 | 418 | -20.38 +- 2.78 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 608 | 418 | -20.38 +- 2.78 | significant |
| chow, 0 shanten, middle | all | 1523 | 819 | -22.23 +- 1.89 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 1523 | 819 | -22.23 +- 1.89 | significant |
| chow, 0 shanten, late | all | 255 | 127 | -24.97 +- 4.86 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 255 | 127 | -24.97 +- 4.86 | significant |
| chow, 1 shanten, early | all | 2576 | 1368 | -3.28 +- 2.03 | significant |
|  | Normal takes | 786 | 665 | +28.63 +- 3.42 | significant |
|  | Normal passes | 1790 | 1059 | -19.22 +- 1.49 | significant |
| chow, 1 shanten, middle | all | 2376 | 1077 | +1.28 +- 1.85 |  |
|  | Normal takes | 874 | 688 | +30.40 +- 2.49 | significant |
|  | Normal passes | 1502 | 812 | -19.05 +- 1.56 | significant |
| chow, 1 shanten, late | all | 144 | 93 | +8.60 +- 7.02 | significant |
|  | Normal takes | 65 | 58 | +31.90 +- 8.29 | significant |
|  | Normal passes | 79 | 55 | -15.09 +- 4.44 | significant |
| chow, 2 shanten, early | all | 3228 | 1673 | -0.18 +- 1.30 |  |
|  | Normal takes | 1654 | 1246 | +13.00 +- 1.50 | significant |
|  | Normal passes | 1574 | 1039 | -15.69 +- 1.34 | significant |
| chow, 2 shanten, middle | all | 962 | 584 | +2.53 +- 1.83 | significant |
|  | Normal takes | 528 | 437 | +13.29 +- 2.05 | significant |
|  | Normal passes | 434 | 299 | -12.90 +- 2.15 | significant |
| chow, 2 shanten, late | all | 15 | 12 | +10.77 +- 11.03 | few |
|  | Normal takes | 13 | 12 | +10.72 +- 11.06 | few |
|  | Normal passes | 2 | 2 | -3.00 +- 1.76 | few |
| chow, 3+ shanten, early | all | 3092 | 1670 | -2.09 +- 1.03 | significant |
|  | Normal takes | 2048 | 1460 | +2.44 +- 1.14 | significant |
|  | Normal passes | 1044 | 787 | -11.82 +- 1.39 | significant |
| chow, 3+ shanten, middle | all | 129 | 100 | +0.82 +- 3.15 |  |
|  | Normal takes | 93 | 91 | +3.84 +- 2.97 | significant |
|  | Normal passes | 36 | 29 | -13.19 +- 5.62 | significant |
| chow, 3+ shanten, late | all | 2 | 2 | -1.85 +- 6.57 | few |
|  | Normal takes | 2 | 2 | -1.85 +- 6.57 | few |
|  | Normal passes | 0 | 0 |  | few |
| pung, 0 shanten, early | all | 211 | 190 | -2.95 +- 19.69 |  |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 211 | 190 | -2.95 +- 19.69 |  |
| pung, 0 shanten, middle | all | 498 | 415 | -13.12 +- 5.73 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 498 | 415 | -13.12 +- 5.73 | significant |
| pung, 0 shanten, late | all | 78 | 61 | -16.68 +- 12.24 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 78 | 61 | -16.68 +- 12.24 | significant |
| pung, 1 shanten, early | all | 1163 | 920 | +37.18 +- 4.42 | significant |
|  | Normal takes | 711 | 633 | +62.65 +- 5.71 | significant |
|  | Normal passes | 452 | 387 | -6.72 +- 3.79 | significant |
| pung, 1 shanten, middle | all | 884 | 666 | +31.12 +- 4.06 | significant |
|  | Normal takes | 529 | 452 | +53.42 +- 5.02 | significant |
|  | Normal passes | 355 | 290 | -5.05 +- 3.42 | significant |
| pung, 1 shanten, late | all | 51 | 49 | +44.14 +- 12.96 | significant |
|  | Normal takes | 39 | 38 | +57.59 +- 13.61 | significant |
|  | Normal passes | 12 | 12 | -0.39 +- 13.34 | few |
| pung, 2 shanten, early | all | 1852 | 1378 | +36.86 +- 2.85 | significant |
|  | Normal takes | 1435 | 1160 | +47.16 +- 3.52 | significant |
|  | Normal passes | 417 | 375 | +1.69 +- 3.08 |  |
| pung, 2 shanten, middle | all | 435 | 373 | +30.56 +- 5.22 | significant |
|  | Normal takes | 353 | 323 | +36.40 +- 5.81 | significant |
|  | Normal passes | 82 | 70 | -2.83 +- 4.85 |  |
| pung, 2 shanten, late | all | 5 | 5 | +15.14 +- 10.19 | few |
|  | Normal takes | 5 | 5 | +15.14 +- 10.19 | few |
|  | Normal passes | 0 | 0 |  | few |
| pung, 3+ shanten, early | all | 2428 | 1626 | +26.16 +- 1.56 | significant |
|  | Normal takes | 2254 | 1562 | +28.42 +- 1.60 | significant |
|  | Normal passes | 174 | 164 | -4.16 +- 4.81 |  |
| pung, 3+ shanten, middle | all | 71 | 70 | +25.69 +- 5.32 | significant |
|  | Normal takes | 62 | 62 | +26.52 +- 5.79 | significant |
|  | Normal passes | 9 | 8 | +19.21 +- 12.41 | few |
| pung, 3+ shanten, late | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |
| kong, 0 shanten, early | all | 24 | 24 | +44.81 +- 37.55 | few |
|  | Normal takes | 13 | 13 | +87.65 +- 53.36 | few |
|  | Normal passes | 11 | 11 | -5.83 +- 34.91 | few |
| kong, 0 shanten, middle | all | 61 | 61 | +36.77 +- 16.77 | significant |
|  | Normal takes | 36 | 36 | +68.81 +- 19.76 | significant |
|  | Normal passes | 25 | 25 | -9.38 +- 17.81 | few |
| kong, 0 shanten, late | all | 13 | 12 | +72.30 +- 56.19 | few |
|  | Normal takes | 10 | 9 | +107.05 +- 58.46 | few |
|  | Normal passes | 3 | 3 | -31.97 +- 24.75 | few |
| kong, 1 shanten, early | all | 88 | 87 | +31.96 +- 14.93 | significant |
|  | Normal takes | 20 | 20 | +88.88 +- 44.65 | few |
|  | Normal passes | 68 | 67 | +14.97 +- 11.51 | significant |
| kong, 1 shanten, middle | all | 65 | 63 | +38.57 +- 18.23 | significant |
|  | Normal takes | 13 | 13 | +72.06 +- 13.15 | few |
|  | Normal passes | 52 | 51 | +30.62 +- 21.82 | significant |
| kong, 1 shanten, late | all | 2 | 2 | +36.30 +- 66.84 | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 2 | 2 | +36.30 +- 66.84 | few |
| kong, 2 shanten, early | all | 90 | 87 | +40.31 +- 16.90 | significant |
|  | Normal takes | 14 | 14 | +117.64 +- 86.13 | few |
|  | Normal passes | 76 | 73 | +25.47 +- 8.75 | significant |
| kong, 2 shanten, middle | all | 24 | 24 | +32.95 +- 18.92 | few |
|  | Normal takes | 5 | 5 | +111.42 +- 34.22 | few |
|  | Normal passes | 19 | 19 | +12.29 +- 8.49 | few |
| kong, 2 shanten, late | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |
| kong, 3+ shanten, early | all | 72 | 71 | +34.97 +- 10.67 | significant |
|  | Normal takes | 17 | 17 | +84.35 +- 28.09 | few |
|  | Normal passes | 55 | 54 | +19.42 +- 7.05 | significant |
| kong, 3+ shanten, middle | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |
| kong, 3+ shanten, late | all | 0 | 0 |  | few |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 0 | 0 |  | few |

#### By kind

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| chow | all | 14910 | 2367 | -4.26 +- 0.75 | significant |
|  | Normal takes | 6063 | 2239 | +13.73 +- 0.91 | significant |
|  | Normal passes | 8847 | 2192 | -17.73 +- 0.77 | significant |
| pung | all | 7676 | 2323 | +26.79 +- 1.55 | significant |
|  | Normal takes | 5388 | 2208 | +39.91 +- 1.76 | significant |
|  | Normal passes | 2288 | 1357 | -6.06 +- 3.13 | significant |
| kong | all | 439 | 393 | +37.66 +- 6.87 | significant |
|  | Normal takes | 128 | 125 | +86.66 +- 15.36 | significant |
|  | Normal passes | 311 | 287 | +17.55 +- 5.61 | significant |

#### By tile group of the claimed tile

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| simple | all | 15592 | 2365 | -0.05 +- 0.82 |  |
|  | Normal takes | 6760 | 2270 | +18.61 +- 0.92 | significant |
|  | Normal passes | 8832 | 2163 | -16.08 +- 0.93 | significant |
| terminal | all | 4730 | 2060 | +4.59 +- 1.16 | significant |
|  | Normal takes | 2528 | 1633 | +18.27 +- 1.49 | significant |
|  | Normal passes | 2202 | 1309 | -13.36 +- 1.37 | significant |
| value honour | all | 1911 | 1377 | +67.57 +- 3.45 | significant |
|  | Normal takes | 1708 | 1333 | +70.36 +- 3.56 | significant |
|  | Normal passes | 203 | 180 | +27.25 +- 15.96 | significant |
| other honour | all | 792 | 663 | +18.41 +- 2.20 | significant |
|  | Normal takes | 583 | 542 | +21.88 +- 2.19 | significant |
|  | Normal passes | 209 | 146 | +5.09 +- 5.84 |  |

#### By shanten change of the claim

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| lower | all | 11952 | 2384 | +24.34 +- 1.01 | significant |
|  | Normal takes | 11165 | 2384 | +25.40 +- 1.03 | significant |
|  | Normal passes | 787 | 634 | +3.32 +- 2.05 | significant |
| equal | all | 10730 | 2245 | -11.30 +- 0.94 | significant |
|  | Normal takes | 414 | 384 | +65.81 +- 7.38 | significant |
|  | Normal passes | 10316 | 2227 | -14.81 +- 0.93 | significant |
| higher | all | 343 | 272 | -62.51 +- 4.80 | significant |
|  | Normal takes | 0 | 0 |  | few |
|  | Normal passes | 343 | 272 | -62.51 +- 4.80 | significant |

#### By East

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| east | all | 5720 | 2165 | +13.60 +- 2.38 | significant |
|  | Normal takes | 2767 | 1798 | +38.25 +- 2.72 | significant |
|  | Normal passes | 2953 | 1408 | -18.69 +- 2.73 | significant |
| not east | all | 17305 | 2394 | +6.71 +- 0.72 | significant |
|  | Normal takes | 8812 | 2369 | +22.25 +- 0.84 | significant |
|  | Normal passes | 8493 | 2174 | -12.38 +- 0.76 | significant |

#### By open sets before the claim

| class | claims | rows | seeds | gain +- 95 % | |
|---|---|---:|---:|---:|---|
| 0 | all | 13878 | 2398 | +5.92 +- 0.84 | significant |
|  | Normal takes | 7518 | 2388 | +21.65 +- 0.96 | significant |
|  | Normal passes | 6360 | 1958 | -17.47 +- 1.04 | significant |
| 1 | all | 7397 | 2203 | +15.57 +- 1.51 | significant |
|  | Normal takes | 3368 | 1929 | +36.15 +- 2.07 | significant |
|  | Normal passes | 4029 | 1611 | -8.68 +- 1.22 | significant |
| 2+ | all | 1750 | 991 | +17.55 +- 4.88 | significant |
|  | Normal takes | 693 | 584 | +49.57 +- 6.02 | significant |
|  | Normal passes | 1057 | 644 | -8.95 +- 5.62 | significant |
