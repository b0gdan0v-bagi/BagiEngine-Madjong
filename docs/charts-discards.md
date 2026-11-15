# Discard charts

## What this is

`BagiMahjongCharts --kind discards` answers one question over many hands: **on an own turn, when does the discard that the points
reading prefers gain points over the discard Normal makes, and where does Normal's rule lose?** It is the second set of charts next to
the claim charts (`docs/charts-claims.md`; same machinery, same habits): a table of classes with the mean points the valued discard
gains over Normal's discard, its 95 percent interval and the number of turns behind it.

How the numbers are made:

- One hand per seed, four Normal seats (the same walls and seat seeds as `BagiMahjongArena --table-stats` and the claim charts). A
  read-only observer looks at every own turn (`Phase::Discard`) through the seat's `TableView` and nothing else.
- On a turn that is **measured** ("The pair") the two discards, Normal's and the valued one, are each played out in the same sampled
  worlds with the rollouts of the search player (`BeginSearchOver`), Normal on for all four seats, the points of this hand only. A
  **row** of the chart is one turn: the mean points of the valued discard minus the mean points of Normal's, in the same worlds, so it
  is exactly the paired mean difference. **Positive: the valued discard is better, Normal's rule loses points in that class.**
- The unit of an interval is the **seed**, as in the claim charts: for a class, the mean of the values of its rows in each seed, then
  the 95 percent interval over the seeds that have a row.
- The analysis seed of a turn is a function of the hand's seed and the turn's index (every own turn of the hand counts, charted or not)
  and nothing else, so a row can be reproduced.

The program only measures. It changes no player and nothing in `Game/`.

## The pair

The charts are about the choice between **speed** (the discard that keeps the hand nearest to a win) and **points** (the discard that is
worth the most from the next draw). Two discards are compared on every measured turn.

- **Fast** is Normal's choice on an own turn whose rule is step 5 (`DecideTurn(view, rng, kNormalProfile).step == TurnStep::Discard`,
  with `MahjongRng(Analyzer::kNormalChoiceSeed)`, the seed of the analyzer). A turn that Normal answers with a win, a declaration, a
  kong or a kept wait (steps 1 to 4: every hand that is one discard from waiting, a declared hand) is **not charted** and is counted
  (`otherStep`): the choice between speed and points exists only in step 5. So the shanten of a charted hand after Normal's discard is 1,
  2 or 3 or more (a step-5 discard that left a waiting hand would be `otherStep` too; none is expected).
- **Valued** is the tile of the first row of `ValueDiscards(view, 1)` (`Game/OwnDraws.h`; a plain discard, as are Normal's step-5
  discards). Depth 1 is the one depth inside the time rule (`docs/own-draws-ev.md`). It rates up to six discards within one shanten of
  the best by their expected points from the next draw and the standing hand's score, so it may leave the shanten one higher: that is
  what the chart prices.
- **A turn is measured** only when the valued tile is **not** among Normal's own ties (the entries of `RateDiscards` equal to the first in
  shanten, acceptance, danger and keep value; Normal picks one of them by its rng, so a valued tile among them is not a disagreement).
  Otherwise the turn is counted as `sameAsNormal` and has no row. A turn on which `ValueDiscards` is empty is counted `noValue`. Every
  own turn is one of `otherStep`, `noValue`, `sameAsNormal` or measured, and the four add up to the own turns of the hands.
- The two candidates go through `BeginSearchOver` and nothing else: two actions a turn, not every legal action of the position. A turn
  for which no world can be sampled is counted `dropped` and has no row; any other failure is an error that names the seed and the turn.

## The classes

Fixed here, before any run, so that no result can move them. A class is the six fields below, a pure function of the seat's own view and
the pair (`ClassifyDiscard` in `ArenaChartsDiscards.h`; it cannot see the hidden tiles).

| Field | Bins | Why |
|-------|------|-----|
| value held | none, a single, a pair or a set | the most copies of one value tile (a dragon, the seat's wind, the round wind) the seat has, its melds counted. A pair of value tiles is a pung away from a double, and a valued discard is the one that sees it: the first thing to price |
| shanten after Normal's discard | 1, 2, 3 or more | how far the hand is from waiting once Normal has discarded. Near a wait the speed is nearly all there is; far from one a hand can still choose its points |
| cost | same, higher | the valued discard leaves the **same** shanten as Normal's or **one higher** (it is never lower: Normal's discard leaves the lowest). The price in speed the valued discard pays |
| stage of the wall | early, middle, late | `liveCount` in thirds of the live wall at the deal, as in the claim charts (`WallStage`): a slower, higher-scoring hand has the draws for it early and not late |
| open sets | 0, 1, 2 or more | the open melds of the seat (a concealed kong is not open; `OpenSets`): an open hand has fewer ways to score and a fixed set of tiles |
| East | East or not | East pays and is paid double, so the same points are worth more to it |

The main table is **value held x shanten x cost** (3 x 3 x 2 = 18 cells): the three fields that say what the discard trades. The one-way
tables are stage, open sets, East, shanten, cost and value held (a cell of a one-way table sums every row with that value, so one row is
counted in each table).

**`kFewRows` = 30**, as in the claim charts. A class of fewer rows is `few`: shown and marked, never hidden and never called
`significant`, because an interval over a handful of seeds says nothing. It is a guess, fixed here, not tuned to a result.

A class **holds** by the rule of `docs/charts-claims.md` ("Protocol", "How a finding is read"): it is `significant` in the discovery range and its confirmation
mean has the same sign and is `significant` too. A `few` class that agrees in sign between the ranges is a lead, not a finding. The
ranges and the commands of the study are in "Protocol" below, which was written before the study was run.

## Protocol

Fixed here, before any number of the study was looked at (the commit that adds this section comes before the run's commit; the cost
runs below are on a range that is used for nothing else, and of their output only the counters and the times are used: a table of 20
seeds is no basis for any class).

| Range | Command | Seeds | Worlds |
|-------|---------|-------|--------|
| discovery | `BagiMahjongCharts --kind discards --first 500000 --seeds 1400 --worlds 100 --jobs 12 --markdown` | 500000 .. 501399 | 100 |
| confirmation | `BagiMahjongCharts --kind discards --first 600000 --seeds 1400 --worlds 100 --jobs 12 --markdown` | 600000 .. 601399 | 100 |

- The two ranges are disjoint, of the same size and with the same worlds. Neither touches the cost range (400000 .. 400019), the cost
  range of the claim study (100000 .. 100019) or its two study ranges (200000 .. 202399 and 300000 .. 302399). The discovery range is
  run first and read first. The confirmation range is run once, with the same build, and nothing is tuned between the two: no class, no
  worlds, no seed count, no filter is changed after a number is seen. A run that fails (exit 1) is a defect, fixed with its own test,
  and the run starts again on the same ranges. No other range is run.
- The worlds are 100 a turn. The spread between turns (a discard is worth tens of points in one position and minus tens in another) is
  far larger than the noise of the worlds of one row, so the seeds and rows are worth more than more worlds ("Cost" below).
- The discovery range is expected to hold about 15 000 rows (about ten and a half a seed). A cell the thirds of the wall or the value
  held thin out may stay `few`: that is a result.
- `--jobs 12` is the threads of one run on a machine that other loops share; the output does not depend on it.
- **How a finding is read.** The cells read are every cell of the main table (18) and of the six one-way tables (stage 3, open sets 3,
  East 2, shanten 3, cost 2, value held 3: 16), 34 numbers in each range. A class **holds** when it is `significant` in the discovery
  range and its confirmation mean has the same sign and is `significant` too. A class that holds is a **finding**. A class that is
  `significant` in one range only, or whose confirmation has the other sign, **does not hold** and is said so. A `few` class that
  agrees in sign between the two ranges is a **lead**, not a finding: its number is shown with its count and nothing is built on it.
  Where Normal's rule loses is read from the sign: a class that holds with a positive gain is one where Normal's discard is the worse of
  the two; one that holds with a negative gain is one where Normal's discard is the better.
- A finding is a statement about the table of this program (the caveats below), against three Normal bots and one hand's points.

## Cost

`BagiMahjongCharts --kind discards --first 400000 --seeds 20 --worlds W --jobs 10` in Release (`macos-release`, 14 cores, shared with
other loops: load average 13 to 77 during the runs), run before the protocol was fixed; only the counters and the times are used.
`user` is the CPU time of all the threads.

| Worlds W | Wall, s | CPU (user), s | CPU per seed, s | Own turns | Rows | Rows per seed |
|----------|---------|---------------|-----------------|-----------|------|---------------|
| 100 | 28.3 | 100.5 | 5.0 | 677 | 213 | 10.7 |
| 250 | 62.4 | 254.7 | 12.7 | 677 | 213 | 10.7 |
| 500 | 138.1 | 493.7 | 24.7 | 677 | 213 | 10.7 |

The counters do not depend on W (the same turns): of 677 own turns, 114 are `otherStep`, 0 are `noValue`, 350 are `sameAsNormal` and 213
are measured (31 percent of the own turns, 38 percent of the 563 that Normal answers in step 5); none is not analysable. The cost is
linear in W and in the seeds. Twenty seeds on ten threads are two rounds on a loaded machine, so the wall time of this small run is
long against its CPU time; for a long run the wall time is about CPU divided by the cores the machine really gives, which the
claim study found to be about eight. For the target of about 15 minutes a run (about 7 200 CPU seconds) and at least about 10 000 rows
in the discovery range:

| W | Seeds in 7 200 CPU s | Rows | CPU for 1400 seeds, s | Wall at about 8 cores, min |
|---|----------------------|------|-----------------------|----------------------------|
| 100 | about 1 440 | about 15 300 | 7 000 | about 14.6 |
| 250 | about 570 | about 6 000 | 17 800 | about 37 |
| 500 | about 290 | about 3 100 | 34 600 | about 72 |

W 250 or 500 would fit fewer than 10 000 rows in 15 minutes: fewer rows and fewer seeds for an interval whose width is set by the seeds
and by the spread between turns. So W = 100 and 1400 seeds are chosen (about 15 000 rows, about 15 minutes at eight cores).

## Caveats

- **Against Normal bots.** The three other seats, and the seat itself after the discard, play Normal (level 5). A number says "against
  this table", never "the best discard in mahjong".
- **Hidden tiles are uniform.** The tiles the seat cannot see are drawn uniformly at random from what it cannot see (`SampleWorld`).
- **One hand's points.** The value is the change of the seat's score in this hand (`SettleHand`; a drawn hand pays 0): no standing of the
  match.
- **Normal play after the discard.** Both discards are followed by Normal's rule for every later decision of the seat, so a discard that
  is good only if played better afterwards is not credited, and a valued discard that Normal later undoes is priced as played.
- **The valued discard is a reading, not a proven rule.** It is `ValueDiscards` at depth 1, a reading that lost against Normal over whole
  matches (`docs/own-draws-ev.md`). A class where it wins is a lead for a better rule, not a rule: the reading was made for the whole
  game, and a class cuts away from it the turns where it hurts.
- **Normal's choice among its ties is one valid choice.** Where Normal's ratings tie it picks by its rng; the chart's Normal is the
  choice of the analyzer's fixed seed, and a turn on which the valued tile is one of the ties is left out, so the chart never prices a
  coin toss of Normal's.
- **Selected turns.** The turns are those on which the two readings differ and Normal is in step 5: not every turn of a hand. A class is
  a statement about those turns, not about the game.
- **Many cells, so some are significant by chance.** About one cell in twenty is `significant` at 95 percent with no effect at all,
  and each range holds 34 numbers, so one or two are `significant` by chance alone. A single `significant` cell is a lead; only a
  class that holds in both ranges of the protocol is read as a finding.

## Build and run

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"          # the reflection generator needs jinja2
cmake --preset macos-release
cmake --build --preset macos-release --target BagiMahjongCharts
build/macos-release/bin/BagiMahjongCharts --kind discards --first 1 --seeds 4 --worlds 50 --jobs 0
```

The program is the one of `docs/charts-claims.md` with `--kind discards` (the default kind is `claims`, so the old command lines and
their bytes stand; any other value is an error that names `--kind`). `--first`, `--seeds`, `--worlds` (per own turn here), `--jobs`
(threads in all, 0 = one per core; whole seeds each), `--markdown` (the tables as markdown: the numbers of this doc are pasted from
it, never typed) and `--help` are as there; the same arguments give the same bytes at any `--jobs`, and the exit codes are the arena's
(0, 1 a refused action or a turn that cannot be measured: a defect, the seed and the turn are printed, 2 bad arguments).

The header gives the range, the worlds, the gain rule and the counters: hands, own turns, **other step** (Normal's step is not 5:
not charted), **no valued discard**, **same as Normal** (the valued tile is one of Normal's ties), **not analysable** (no world to
sample) and **rows**; own turns are the sum of the five that follow it. Then the main table (value held x shanten after Normal's
discard x cost, 18 cells) and the one-way tables (stage, open sets, East, shanten, cost, value held). A cell is one line: its rows,
the seeds that have one, `gain +- half` and the word `significant` or `few`; there is no `Normal takes` / `Normal passes` split,
Normal always takes its own discard. The code (`src/Mahjong/Arena/ArenaChartsDiscards.*`, `ArenaChartsRun.*`,
`ArenaChartsReport.*`) is tested by `ArenaChartsDiscardsTest`, `ArenaChartsRunTest` and `ArenaChartsReportTest`. Run it in Release: a
turn costs `2 x --worlds` rollouts of whole hands.

## How to read it

A line of a table is a class. The number is the mean points the valued discard gains over Normal's discard in the same worlds, then the
half-width of its 95 percent interval over the seeds; `rows` are the turns behind it, `seeds` the hands that have one. Positive: the
valued discard is better and Normal's rule loses points in that class; negative: Normal's discard is better. The shanten in a class
name is the one **after Normal's discard**; `same shanten` and `one higher` are what the valued discard leaves. Examples, the
discovery range and the confirmation range one after the other:

- **One shanten, the valued discard keeps the shanten, a value pair or set held: +55.0 +- 11.3 (244 rows), then +54.1 +- 16.8 (206 rows).**
  The valued discard is better by about 55 points. With no value tile held, the same cell is +12.0 +- 3.0 and +14.0 +- 4.8.
- **Two shanten, the valued discard costs a shanten, a single value tile held: -11.0 +- 0.9 (3097 rows), then -12.4 +- 1.0 (2853 rows).**
  Normal's discard is better by about 11 to 12 points. The same sign and size hold at 2 shanten with no value tile (-12.6, -12.5) and
  with a pair or set (-13.7, -11.2).
- **Three or more shanten, the valued discard keeps the shanten, a single value tile held: -0.2 +- 0.5, then +0.7 +- 0.5 (3862 and
  3994 rows).** Nothing: about zero, and the sign changes between the ranges.

A class mean is the average of many different positions: where it is positive some of the turns in it still lose, and the program
prints no tile pair, so the charts say which classes, not which discards.

## What Normal does against it

By the "Protocol" above, a class holds when it is `significant` in both ranges with the same sign. The 34 numbers read (18 cells of
the main table and 16 of the six one-way tables), by the two ranges together:

| Group | Numbers |
|-------|---------|
| hold, positive (the valued discard is better: Normal loses) | 5 |
| hold, negative (Normal's discard is better) | 18 |
| `few` in a range, same sign in both: a lead | 1 |
| `significant` in one range only: do not hold | 5 |
| neither range `significant`, no lead | 5 |

Both ranges ran with exit code 0 and `not analysable 0`. The turns the program leaves out are many: of the own turns, 19 to 20 percent
are not step 5 of Normal's rule (`otherStep`), about 49 percent are turns on which the valued tile is one of Normal's own ties
(`sameAsNormal`), and about 32 percent are measured (39 percent of the turns that are charted at all). The chart is about those
measured turns, on which the two readings differ.

**Where Normal's discard loses points** (all hold, `D` the discovery range, `C` the confirmation range):

| class | D rows | D gain +- 95 % | D | C rows | C gain +- 95 % | C |
|---|---:|---:|---|---:|---:|---|
| none, 1, same shanten | 270 | +11.97 +- 3.00 | significant | 284 | +13.98 +- 4.84 | significant |
| pair or set, 1, same shanten | 244 | +54.97 +- 11.26 | significant | 206 | +54.10 +- 16.80 | significant |
| open sets: 2+ | 435 | +10.70 +- 5.48 | significant | 512 | +7.32 +- 5.70 | significant |
| shanten: 1 | 705 | +25.06 +- 4.66 | significant | 696 | +23.03 +- 5.73 | significant |
| cost: same shanten | 9078 | +1.47 +- 0.69 | significant | 9227 | +1.90 +- 0.88 | significant |

- **One shanten after the discard** is where Normal's rule loses: the valued discard gains +25.1 +- 4.7 and +23.0 +- 5.7 over about 700
  rows a range, about 0.5 turns a hand. In it the valued discard that **keeps the shanten** gains most when a value pair or set is held
  (+55.0 and +54.1; 244 and 206 rows) and still gains with no value tile held (+12.0 and +14.0). The same class with a single value tile
  (+4.7 and +3.5, 38 and 50 rows) does not hold: it is not significant in either range. The tables do not say which tile is
  discarded. A pair of value tiles is a pung away from a double, and the valued reading counts the points of the standing hand.
- **An open hand of two sets or more** gains +10.7 +- 5.5 and +7.3 +- 5.7 (435 and 512 rows): small, with wide intervals.
- **A valued discard that keeps the shanten, taken over every class,** gains +1.5 +- 0.7 and +1.9 +- 0.9 (about 9 100 rows). It is
  small and rests on the 1-shanten cells above: in the cells at 2 shanten with no or one value tile it is negative (below).

**Where Normal's discard is better** (all hold, negative; 18 numbers, the full tables are under "Results"):

| class | D rows | D gain +- 95 % | D | C rows | C gain +- 95 % | C |
|---|---:|---:|---|---:|---:|---|
| none, 2, same shanten | 1109 | -2.71 +- 1.31 | significant | 1138 | -1.61 +- 1.14 | significant |
| none, 2, one higher | 939 | -12.61 +- 1.37 | significant | 973 | -12.47 +- 1.37 | significant |
| none, 3+, one higher | 52 | -5.20 +- 3.84 | significant | 72 | -6.09 +- 4.99 | significant |
| single, 2, same shanten | 1228 | -3.38 +- 0.99 | significant | 1191 | -3.42 +- 1.06 | significant |
| single, 2, one higher | 3097 | -10.98 +- 0.88 | significant | 2853 | -12.37 +- 0.95 | significant |
| single, 3+, one higher | 1150 | -7.11 +- 1.12 | significant | 1095 | -5.94 +- 1.10 | significant |
| pair or set, 2, one higher | 1172 | -13.68 +- 2.27 | significant | 1272 | -11.15 +- 2.12 | significant |
| pair or set, 3+, one higher | 299 | -11.36 +- 3.25 | significant | 308 | -13.14 +- 3.50 | significant |
| stage: early | 14542 | -4.03 +- 0.45 | significant | 14503 | -3.61 +- 0.53 | significant |
| open sets: 0 | 11311 | -4.28 +- 0.48 | significant | 11002 | -4.22 +- 0.55 | significant |
| open sets: 1 | 4194 | -2.69 +- 1.29 | significant | 4442 | -2.38 +- 1.47 | significant |
| East: east | 3849 | -4.38 +- 1.43 | significant | 3944 | -3.99 +- 1.61 | significant |
| East: not east | 12091 | -3.43 +- 0.51 | significant | 12012 | -3.42 +- 0.49 | significant |
| shanten: 2 | 8352 | -8.23 +- 0.61 | significant | 8295 | -7.12 +- 0.73 | significant |
| shanten: 3+ | 6883 | -1.56 +- 0.53 | significant | 6965 | -1.67 +- 0.52 | significant |
| cost: one higher | 6862 | -11.06 +- 0.68 | significant | 6729 | -10.85 +- 0.74 | significant |
| value held: none | 2822 | -3.80 +- 0.93 | significant | 2895 | -3.31 +- 1.27 | significant |
| value held: single | 9460 | -4.79 +- 0.44 | significant | 9263 | -4.25 +- 0.47 | significant |

- **Whenever the valued discard costs a shanten at 2 shanten or more** it loses: in every cell of the main table at 2 shanten (-11.0
  to -13.7 in D, -11.2 to -12.5 in C) and at 3 or more (-5.2 to -13.1). The whole cost cell "one higher" is -11.1 +- 0.7 and
  -10.9 +- 0.7 over about 6 800 rows a range; it also holds the 1-shanten rows (153 and 156), which do not hold on their own. This is
  the price the reading pays in speed, consistent with its losing over whole matches (`docs/own-draws-ev.md`). Normal's discard is
  right to keep the shanten there.
- **At 2 shanten with the same shanten** the valued discard loses a little with no value tile (-2.7 and -1.6) or a single one (-3.4 and
  -3.4); with a pair or set it is +0.3 and +5.1 (significant in the confirmation range only). At 2 shanten as a whole: -8.2 and -7.1; at
  3 or more: -1.6 and -1.7.
- **Over the whole table** the valued discard loses in the early wall (91 percent of the rows): -4.0 +- 0.5 and -3.6 +- 0.5. The same
  sign holds for East (-4.4 and -4.0) and for the others (-3.4 and -3.4), for zero or one open set (-4.3 and -4.2; -2.7 and -2.4) and
  for none or a single value tile held (-3.8 and -3.3; -4.8 and -4.3). It is the weight of the 2-shanten turns.

**Significant in one range only** (do not hold):

| class | D rows | D gain +- 95 % | D | C rows | C gain +- 95 % | C |
|---|---:|---:|---|---:|---:|---|
| none, 3+, same shanten | 435 | +2.00 +- 1.47 | significant | 395 | -0.60 +- 1.70 |  |
| single, 1, one higher | 85 | -4.14 +- 3.47 | significant | 80 | +0.38 +- 5.81 |  |
| single, 3+, same shanten | 3862 | -0.18 +- 0.54 |  | 3994 | +0.73 +- 0.52 | significant |
| pair or set, 1, one higher | 51 | +14.75 +- 11.19 | significant | 43 | +17.72 +- 19.29 |  |
| pair or set, 2, same shanten | 807 | +0.25 +- 2.60 |  | 868 | +5.06 +- 3.65 | significant |

**Neither range significant** (no lead):

| class | D rows | D gain +- 95 % | D | C rows | C gain +- 95 % | C |
|---|---:|---:|---|---:|---:|---|
| single, 1, same shanten | 38 | +4.68 +- 5.03 |  | 50 | +3.48 +- 4.06 |  |
| pair or set, 3+, same shanten | 1085 | +0.37 +- 1.73 |  | 1101 | -1.67 +- 1.72 |  |
| stage: middle | 1372 | +1.26 +- 2.91 |  | 1414 | +1.06 +- 2.93 |  |
| stage: late | 26 | -1.59 +- 7.69 | few | 39 | +9.29 +- 15.73 |  |
| value held: pair or set | 3658 | -0.10 +- 1.89 |  | 3798 | -0.19 +- 1.99 |  |

**A lead, not a finding.** At 1 shanten with no value tile where the valued discard costs a shanten: +2.8 +- 9.7 (17 rows, `few`) and
+3.1 +- 9.9 (33 rows); the sign agrees, the number is nothing. No change is made on it.

No class is `significant` in both ranges with opposite signs. Five cells change sign between the ranges (none, 3+, same shanten;
single, 1, one higher; single, 3+, same shanten; pair or set, 3+, same shanten; stage late), each thin or near zero in one range or
both, and nothing is read from them. The five cells that are `significant` in one range only are said so above. No class was dropped
and no run was repeated on another range.

## Left to a better player

The input of a later entry, with no change made here. The valued discard is a reading that lost over whole matches
(`docs/own-draws-ev.md`); these tables say where it is worth taking and where it is not:

1. **At 1 shanten after the discard, take the valued tile when it keeps the shanten** and, above all, when a pair or a set of value
   tiles is held: +55 in about 220 turns a range (0.2 a hand), +12 to +14 with no value tile held in about 280. In these rows the
   valued tile is not one of Normal's ties, so a rule that takes it overrides Normal's ratings of shanten, acceptance, danger and
   keep value, and it needs a feature inside the class that the tables do not carry (which tile; whether the hand is concealed). The next measure is the rule itself, played in the arena over whole matches.
2. **Nowhere else take a higher shanten.** At 2 shanten or more the valued discard that costs a shanten loses in every one of the
   twelve cells (all hold): about 11 to 14 points at 2 shanten and about 5 to 13 at 3 or more; a better rule leaves Normal's choice
   there. At 1 shanten no cell with a higher shanten holds. The one that gains (a pair or set, one higher: +14.8 and +17.7, 51 and 43
   rows) is `significant` in the discovery range only, so it is not a finding either.
3. **The same direction as the claim charts.** The claim charts (`docs/charts-claims.md`) found that Normal passes value honours that
   gain about +30 over passing; here the hand with a pair or set of value tiles at 1 shanten is where its discard loses most. Both
   say the same thing about value tiles near a wait, and neither table alone says that the two losses are one. Whether one rule
   (keep a value pair and take the pung) covers both is a question for the entry that builds it.
4. **The turns left out.** Normal's step 1 to 4 turns (a wait, a declaration, a kong: about 19 percent of the own turns) and the turns on which
   the valued tile is a tie of Normal's (about 49 percent) are not in the tables; a rule is only judged on the turns it changes.

## Results

Run on 2026-11-15 with `BagiMahjongCharts` built in Release from the sources of commit `bcaa395` (the commit that follows it adds this
document's protocol and nothing else), on a machine shared with other loops: the discovery run took 1111 s and the confirmation run
1405 s with 12 threads (CPU 7606 s and 7555 s), load average 47 to 93 and 54 to 93, longer than the 15 minutes the "Cost" section asked
for because of that load (the work was about 7 600 CPU seconds a run against the 7 000 estimated). Both runs exit with code 0, `not
analysable 0`. The counters of the two ranges:

| Range | Hands | Own turns | Other step | No valued discard | Same as Normal | Measured (rows) | Rows per hand |
|-------|-------|-----------|------------|-------------------|----------------|-----------------|---------------|
| discovery | 1400 | 50442 | 9909 | 0 | 24593 | 15940 | 11.4 |
| confirmation | 1400 | 50407 | 9660 | 0 | 24791 | 15956 | 11.4 |

The numbers below are the program's output, pasted unchanged (the tables of the two ranges are the same classes, so a cell is found by
its name in both).

### Discovery range

`build/macos-release/bin/BagiMahjongCharts --kind discards --first 500000 --seeds 1400 --worlds 100 --jobs 12 --markdown`

Discard charts: seeds 500000 .. 501399 (1400 seeds), 100 worlds per turn.

hands 1400, own turns 50442, other step 9909, no valued discard 0, same as Normal 24593, not analysable 0, rows 15940.

gain = mean points of the valued discard minus points of Normal's discard, in the same worlds; positive: the valued discard is better and Normal's rule loses points in the class; +- is 1.96 standard errors over the seeds (a 95 percent interval), a seed is one sample; a cell of fewer than 30 rows is few: shown, never significant.

caveat: against three Normal bots, hidden tiles uniform, both discards followed by Normal play, the points of this hand only; the valued discard is a reading that lost over whole matches, so a class where it wins is a lead, not a rule; about one significant cell in 20 is chance, so read only what holds in both ranges (docs/charts-discards.md).

#### Main table: value held x shanten after Normal's discard x cost of the valued discard

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| none, 1, same shanten | 270 | 220 | +11.97 +- 3.00 | significant |
| none, 1, one higher | 17 | 14 | +2.76 +- 9.74 | few |
| none, 2, same shanten | 1109 | 670 | -2.71 +- 1.31 | significant |
| none, 2, one higher | 939 | 597 | -12.61 +- 1.37 | significant |
| none, 3+, same shanten | 435 | 312 | +2.00 +- 1.47 | significant |
| none, 3+, one higher | 52 | 50 | -5.20 +- 3.84 | significant |
| single, 1, same shanten | 38 | 37 | +4.68 +- 5.03 |  |
| single, 1, one higher | 85 | 57 | -4.14 +- 3.47 | significant |
| single, 2, same shanten | 1228 | 751 | -3.38 +- 0.99 | significant |
| single, 2, one higher | 3097 | 1063 | -10.98 +- 0.88 | significant |
| single, 3+, same shanten | 3862 | 1202 | -0.18 +- 0.54 |  |
| single, 3+, one higher | 1150 | 640 | -7.11 +- 1.12 | significant |
| pair or set, 1, same shanten | 244 | 203 | +54.97 +- 11.26 | significant |
| pair or set, 1, one higher | 51 | 31 | +14.75 +- 11.19 | significant |
| pair or set, 2, same shanten | 807 | 530 | +0.25 +- 2.60 |  |
| pair or set, 2, one higher | 1172 | 655 | -13.68 +- 2.27 | significant |
| pair or set, 3+, same shanten | 1085 | 589 | +0.37 +- 1.73 |  |
| pair or set, 3+, one higher | 299 | 252 | -11.36 +- 3.25 | significant |

#### By stage of the wall

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| early | 14542 | 1399 | -4.03 +- 0.45 | significant |
| middle | 1372 | 608 | +1.26 +- 2.91 |  |
| late | 26 | 17 | -1.59 +- 7.69 | few |

#### By open sets

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| 0 | 11311 | 1398 | -4.28 +- 0.48 | significant |
| 1 | 4194 | 1194 | -2.69 +- 1.29 | significant |
| 2+ | 435 | 274 | +10.70 +- 5.48 | significant |

#### By East

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| east | 3849 | 1312 | -4.38 +- 1.43 | significant |
| not east | 12091 | 1399 | -3.43 +- 0.51 | significant |

#### By shanten after Normal's discard

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| 1 | 705 | 486 | +25.06 +- 4.66 | significant |
| 2 | 8352 | 1394 | -8.23 +- 0.61 | significant |
| 3+ | 6883 | 1368 | -1.56 +- 0.53 | significant |

#### By cost of the valued discard

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| same shanten | 9078 | 1393 | +1.47 +- 0.69 | significant |
| one higher | 6862 | 1370 | -11.06 +- 0.68 | significant |

#### By value tiles held

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| none | 2822 | 1056 | -3.80 +- 0.93 | significant |
| single | 9460 | 1381 | -4.79 +- 0.44 | significant |
| pair or set | 3658 | 1036 | -0.10 +- 1.89 |  |

### Confirmation range

`build/macos-release/bin/BagiMahjongCharts --kind discards --first 600000 --seeds 1400 --worlds 100 --jobs 12 --markdown`

Discard charts: seeds 600000 .. 601399 (1400 seeds), 100 worlds per turn.

hands 1400, own turns 50407, other step 9660, no valued discard 0, same as Normal 24791, not analysable 0, rows 15956.

gain = mean points of the valued discard minus points of Normal's discard, in the same worlds; positive: the valued discard is better and Normal's rule loses points in the class; +- is 1.96 standard errors over the seeds (a 95 percent interval), a seed is one sample; a cell of fewer than 30 rows is few: shown, never significant.

caveat: against three Normal bots, hidden tiles uniform, both discards followed by Normal play, the points of this hand only; the valued discard is a reading that lost over whole matches, so a class where it wins is a lead, not a rule; about one significant cell in 20 is chance, so read only what holds in both ranges (docs/charts-discards.md).

#### Main table: value held x shanten after Normal's discard x cost of the valued discard

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| none, 1, same shanten | 284 | 244 | +13.98 +- 4.84 | significant |
| none, 1, one higher | 33 | 22 | +3.12 +- 9.86 |  |
| none, 2, same shanten | 1138 | 695 | -1.61 +- 1.14 | significant |
| none, 2, one higher | 973 | 619 | -12.47 +- 1.37 | significant |
| none, 3+, same shanten | 395 | 299 | -0.60 +- 1.70 |  |
| none, 3+, one higher | 72 | 58 | -6.09 +- 4.99 | significant |
| single, 1, same shanten | 50 | 45 | +3.48 +- 4.06 |  |
| single, 1, one higher | 80 | 48 | +0.38 +- 5.81 |  |
| single, 2, same shanten | 1191 | 711 | -3.42 +- 1.06 | significant |
| single, 2, one higher | 2853 | 1006 | -12.37 +- 0.95 | significant |
| single, 3+, same shanten | 3994 | 1200 | +0.73 +- 0.52 | significant |
| single, 3+, one higher | 1095 | 634 | -5.94 +- 1.10 | significant |
| pair or set, 1, same shanten | 206 | 168 | +54.10 +- 16.80 | significant |
| pair or set, 1, one higher | 43 | 30 | +17.72 +- 19.29 |  |
| pair or set, 2, same shanten | 868 | 563 | +5.06 +- 3.65 | significant |
| pair or set, 2, one higher | 1272 | 665 | -11.15 +- 2.12 | significant |
| pair or set, 3+, same shanten | 1101 | 612 | -1.67 +- 1.72 |  |
| pair or set, 3+, one higher | 308 | 249 | -13.14 +- 3.50 | significant |

#### By stage of the wall

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| early | 14503 | 1400 | -3.61 +- 0.53 | significant |
| middle | 1414 | 623 | +1.06 +- 2.93 |  |
| late | 39 | 26 | +9.29 +- 15.73 |  |

#### By open sets

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| 0 | 11002 | 1394 | -4.22 +- 0.55 | significant |
| 1 | 4442 | 1212 | -2.38 +- 1.47 | significant |
| 2+ | 512 | 274 | +7.32 +- 5.70 | significant |

#### By East

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| east | 3944 | 1299 | -3.99 +- 1.61 | significant |
| not east | 12012 | 1399 | -3.42 +- 0.49 | significant |

#### By shanten after Normal's discard

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| 1 | 696 | 478 | +23.03 +- 5.73 | significant |
| 2 | 8295 | 1395 | -7.12 +- 0.73 | significant |
| 3+ | 6965 | 1372 | -1.67 +- 0.52 | significant |

#### By cost of the valued discard

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| same shanten | 9227 | 1395 | +1.90 +- 0.88 | significant |
| one higher | 6729 | 1362 | -10.85 +- 0.74 | significant |

#### By value tiles held

| class | rows | seeds | gain +- 95 % | |
|---|---:|---:|---:|---|
| none | 2895 | 1074 | -3.31 +- 1.27 | significant |
| single | 9263 | 1371 | -4.25 +- 0.47 | significant |
| pair or set | 3798 | 1030 | -0.19 +- 1.99 |  |
