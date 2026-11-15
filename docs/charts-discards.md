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
ranges and the commands of the study go into a "Protocol" section of this doc, written before the study is run.

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
  and the tables hold well over fifty cells. A single `significant` cell is a lead; only a class that holds in both ranges of the
  protocol is read as a finding.
