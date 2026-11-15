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
