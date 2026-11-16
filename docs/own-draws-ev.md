# The value of a discard from the seat's own draws

The entry `mahjong-own-draws-ev`: a computer seat rates each discard (and claim, and the declaration) in expected points from its own next draws, and the arena says whether that beats Normal. Two slices, one page: slice 1 (the first sections down to "For slice 2") is kept as history, slice 2 ("Slice 2: ...") is the final record. **Final verdict: no switch.** The knobs `ownDrawsDepth`, `ownDrawsPickWait`, `ownDrawsDiscardWins`, `ownDrawsClaimDepth` and `ownDrawsDeclareDepth` stay in the tree, off in every level; no level was changed. The entry closes (the owner closes it).

**What to believe.** Nothing inside the time rule beats Normal. The one real effect is the depth: depth 2 and 3 turn play toward dearer wins (+5 points a hand on the exploration range) and pay in speed (wins -0.4 to -2.6 %); on a fresh range depth 3 fell to +2.04 +- 3.95 with wins -1.40 sig., so the +5 is probably the best-of-seven pick. The choice of the wait, the win on a discard, the claims and the declaration are empty or harmful. Whether a deeper or better-leafed value would beat Normal is not known.

**The time rule.** Slice 1 used the rule of `docs/bot-search.md` as written: the slowest Debug decision at most 0.3 s. For slice 2 the plan relaxed it for a resumable stepped run (1.0 s in Debug, 30 ms a step; the browser factor behind it a guess), after slice 1's times showed depth 2 at 1.3 s. No variant reached the point where that mattered: none passed the strength conditions, so the stepped run was not built, and the rows below are judged by the 0.3 s rule as well.

## What is valued

`ValueDiscards(view, depth)` (step 5 of the turn) and `ValueWaits(view)` (step 2), `src/Mahjong/Game/OwnDraws.{h,cpp}`, pure functions of a `TableView` (no peeking, no rng). The arithmetic and the table constants are `OwnDrawsValue.{h,cpp}`.

- **Fixed point.** A probability is an integer over `kOwnDrawsProbBase` (1 000 000), a value an `int64_t` in milli-points, every division rounds half up through `RoundedDiv`. One input gives one choice on every platform.
- **What a win pays** (`WinPayoff`): the winner gets its score from each of the three others, and a payment with East is `kEastPaymentFactor` times larger.
- **The standing hand when another seat wins** (`NotWinningValue`): the seat pays that winner a typical winner's score and settles with the two other losers by the difference of the scores (`ScoreSeat` of the standing hand against a typical loser's score); an East seat has the East factor on all three payments, another seat on the payment with East only (expected factor as a fraction of integers). A drawn hand pays nothing.
- **The opponents are one table**: after the seat's `i`-th next draw nobody else has won with chance `Alive(i, d) = (1 - h(d))^i`, `d` the other seats that have declared. No hand of an opponent is modelled.
- **The search** is an expectimax over the seat's next `depth` draws from the unseen copies; only draws that lower the shanten branch (`ImprovingTiles`), every other draw is one "unchanged" branch; the discard after a draw is the best of those that keep the lowest shanten; a memo keyed on the state removes transpositions. Root candidates: the listed plain discards within `kSearchShantenSlack` of the best shanten, at most `kOwnDrawsMaxCandidates`. A hand that waits is taken as declared and valued in closed form over all the draws the wall leaves; where the search stops with a hand that does not wait, it does not win any more and the standing hand is what is left.
- **Left out on purpose:** a win on an opponent's discard (see the table below: 76 % of the won hands of four Normal seats), the opponents' hands, the limit setting (`kDefaultLimit`), the tile of an unchanged draw.

## The constants and the command that reproduces them

`BagiMahjongArena --table-stats` (Release, `A` is the arena binary): four Normal seats, each seed played once.

```bash
$A --table-stats --first 4000 --seeds 1000 --jobs 14        # 0.7 s; the same bytes at --jobs 1
```

```
hands won 1000, drawn 0 (0.00 %); won on a discard 763 (76.30 %), on a self-draw 236 (23.60 %), on a robbed kong 1 (0.10 %)

another seat wins in the following round, by the opponents that have declared (d)
d   turns   h per mille   survival % after 1 / 2 / 3 / 4 rounds   (1-h)^i %
0   23706        7.2      99.28 / 95.62 / 89.21 / 80.41          99.28 / 98.56 / 97.85 / 97.15
1    6995      188.4      81.16 / 62.10 / 44.76 / 30.19          81.16 / 65.87 / 53.46 / 43.38
2    3173      336.0      66.40 / 40.78 / 23.59 / 12.78          66.40 / 44.09 / 29.28 / 19.44
3     937      474.9      52.51 / 25.53 / 11.18 /  5.02          52.51 / 27.57 / 14.48 /  7.60

mean score             samples   mean (95 %)
winner, East               283   47.92 +- 4.09
winner, not East           717   44.49 +- 1.97
standing, East             717    9.37 +- 0.97
standing, not East        2283    9.12 +- 0.43
```

A round is the stretch between an own turn of a seat and its next one; a turn counts for `i` rounds when the next `i` turns exist, or when another seat won (the win is then in a known round), so a hand that ends by the seat's own win does not count rounds that never came out. The constants in `OwnDrawsValue.h`:

| Constant | Value | From |
|---|---|---|
| `kOpponentWinPerRoundPermille` | 7, 188, 336, 475 (d = 0..3) | the `h` column, rounded |
| `kTypicalScoresEast`, `kTypicalScoresOther` | winner 45, standing 9 | pooled means; East and the others do not differ significantly (the intervals overlap), so one pair |

**What the table shows beyond the constants.** The survival after two or more rounds is below `(1 - h)^i`: the number of declared opponents grows during the horizon (d = 0 survives 80 % of four rounds, not 97 %), so the model's `Alive` discounts the later draws too little. The structure was fixed before the numbers and was not changed. All 1000 hands were won (Normal does not draw), and 76 % of them on a discard: the own-draws value sees at most the 24 % won on a self-draw.

**Retuning.** Only on the exploration range, once: `h` fitted to the survival after three rounds (37, 235, 382, 518) was played against the measured table for all three variants below and was within noise (the paired points of `ownDrawsDepth=1` moved from -7.35 to -8.77, those of the pick-wait from -0.41 to -0.36), so the measured constants stayed.

## Debug time

The rule of `docs/bot-search.md` ("The budget"): the slowest decision at most 0.3 s in Debug. Debug arena at `--jobs 1 --time --others 5`, seeds 4000 .. 4015 (the focus seat plays all four rotations), each decision timed with a temporary stderr print (not committed), two runs, the smaller time of each decision; the machine was shared with other sessions.

```bash
$D --first 4000 --seeds 16 --jobs 1 --time --others 5 --focus 5:ownDrawsDepth=<D>,ownDrawsPickWait=1      # twice
```

| variant | decisions | worst decision | mean decision | the rule |
|---|---|---|---|---|
| `ownDrawsPickWait=1` | 709 | 3.2 ms | 0.5 ms | in |
| depth 1 + pick-wait | 702 | 23.1 ms | 4.0 ms | in |
| depth 2 + pick-wait | 667 | 1321 ms | 62.9 ms | **out** |
| depth 3 + pick-wait | seed 4000 only | 7144 ms (first discard of one hand) | | **out** |
| depth 4 + pick-wait | seed 4000 only | 21266 ms (first discard of one hand, one run) | | **out** |

Depth 3 and 4 were timed on the first hand of seed 4000 only (depth 4 once, depth 3 as in the table): the worst of a subset is a lower bound of the worst of the sample, so one decision over 0.3 s is enough to put a depth out. **Depth 2 is out by 4.4 times.** The cost is the first discards (14 tiles, six candidates, the full tree); the mean is far below the rule. Slice 2 therefore starts with the resumable stepped run (`ComputerThinking`), as the plan said. By the rule written first, **no range was played at depth 2, 3 or 4.** Depth 1 was not in the list of the exploration range of the plan (which named 2, 3, 4 "inside the time rule"); it is inside the rule, and it was added to the exploration before any strength number was seen, so every depth the rule lets in was looked at.

## The ranges

Release arena, `--others 5` (three Normal seats), each variant paired with Normal on the same seeds. "sig." is the arena's `significant`.

**Exploration range** (`--first 4000 --seeds 1000 --jobs 14`):

```bash
$A --first 4000 --seeds 1000 --jobs 14 --others 5 --focus 5,5:ownDrawsPickWait=1,5:ownDrawsDepth=1,5:ownDrawsDepth=1,ownDrawsPickWait=1
```

| variant | wins % | points/hand | declared % | avg win score | paired points/hand | paired wins % |
|---|---|---|---|---|---|---|
| Normal | 25.00 | 0 | 55.77 | 45.46 | | |
| pick-wait | 24.73 +- 0.28 | -0.41 +- 1.11 | 55.77 | 45.66 | -0.41 +- 1.11 | -0.28 +- 0.28 |
| depth 1 | 17.23 +- 1.04 | -7.35 +- 4.28 | 40.75 | 52.38 | -7.35 +- 4.28 sig. | -7.78 +- 1.04 sig. |
| depth 1 + pick-wait | 17.45 +- 1.05 | -4.63 +- 4.68 | 40.75 | 54.83 | -4.63 +- 4.68 | -7.55 +- 1.05 sig. |

The pick-wait alone is the best variant by the paired points (-0.41), so it alone went to the decision range. (Fitted `h`, same range: pick-wait -0.36 +- 1.12 against -0.41, within noise; depth 1 -8.77 +- 4.30 sig., depth 1 + pick-wait -7.25 +- 4.50 sig.)

**Decision range** (`--first 100000 --seeds 1000 --jobs 14`, played once):

```bash
$A --first 100000 --seeds 1000 --jobs 14 --others 5 --focus 5,5:ownDrawsPickWait=1
```

| variant | wins % | points/hand | avg win score | paired points/hand | paired wins % |
|---|---|---|---|---|---|
| Normal | 24.98 | 0 | 44.58 | | |
| pick-wait | 24.90 +- 0.34 | -0.03 +- 1.00 | 44.60 | -0.03 +- 1.00 | -0.07 +- 0.34 |

The paired points are not significantly above 0, so by the rule the confirmation range (`--first 130000`) is not played, and the random-seat range was not needed. No range was replayed with other numbers.

## The verdict

By "The rule of the verdict" (all of: both ranges significantly above 0 on points and not below on wins, pooled at least +3 points a hand, not below 0 against random seats, the slowest Debug decision at most 0.3 s): the time rule fails for every depth from 2, the depth that does fit loses to Normal significantly, and the decision range of the only variant that was close is a flat zero. **No switch.** Levels 6..10 stay Normal.

## What part of the value was looked at

- **The choice of the wait (`ownDrawsPickWait`): empty.** The declaring discard it picks changes the average winning score by +0.2 and the wins by -0.1 to -0.3 %: no effect on either range. The waits Normal picks (most unseen copies) are the ones worth the most, to within the noise.
- **The standing score and doubles, at the depth that fits (1):** the value moves play toward value hands, and the score lines show it: pungs in 78.8 % of the won hands (Normal 66.9), dragon sets 19.0 (16.1), one suit with honours 1.6 (0.4), only pungs 0.7 (0), no-points hands 7.4 (14.2), the average winning score 52.4 (45.5, +15 %). It pays with speed: declared 40.8 % (55.8), wins 17.2 % (25.0), drawn hands 0.55 % (0). With one draw of horizon a hand that does not wait after the draw cannot win, so the number that differs between two discards is the standing hand's score, which is worth more than a one-draw chance to win; the gain in score is smaller than the loss in wins. Concealed hands (`FullyConcealed`) did not move: 4.70 % of the focus seat's hands at depth 1 against Normal's 4.59 (the other seats of the same table: 4.35).
- **Not looked at:** the claims, the declaration "now or later", and depth 2 and up (out by the time rule). Whether the deeper value would beat Normal is **unknown** from this slice: the one number that says the structure may carry more is the score-line shift at depth 1, which is real and in the intended direction.
- **The blind spot that stays:** the 76 % of the won hands that are won on a discard are not in the value at all.

## For slice 2

The gate is the result: no variant inside the time rule is positive, and the part that is cheap enough (the wait) is empty. Slice 2 would have to start with the resumable run (depth 2 is 4.4 times over the rule), and the claims, that is, the rule that Normal wins 76 % on discards, are where the value is not yet modelled. The owner decides whether that is worth building as it stands (`mahjong-own-draws-ev`, "Left for the owner").

## Slice 2: the hit rate of a discard

`--table-stats` also prints, for a declared, waiting seat, the tiles it could win on: an exposure is one discard of another seat (taken from the state before the discard, the seat's own view) or one of the seat's own draws; the expected hits are the sum of `unseen winning copies / unseen total` over the exposures. Measured with `$A --table-stats --first 4000 --seeds 1000 --jobs 14` (Release, 1.4 s):

```
                        exposures  expected hits       hits  hit per mille
discard of another          16884       990.68        798        805.5
own draw (check)             4627       273.39        238        870.6
```

`kDiscardHitPermille` is 806 (the discard row). The own draw is the check and should be about 1000 by the nature of the wall; it is 871 here and 888 on `--first 20000 --seeds 6000` (the discard row 803 there), so it is below 1000 by 4 standard errors. The count is right (a hand worked out by hand in `ArenaTableTest`); the likely reason is that the opponents hold back the winning kinds of a declared seat, so the unseen copies are over-represented in their hands, not in the wall. That is the same effect that makes the discard rate lower, and it is a hypothesis, not measured. This run also differs slightly from the slice 1 table above (761 wins on a discard against 763): Normal moved between the slices; the slice 2 rows are measured on the code of this branch.

### The evaluator made fast (WP 3)

The search of one call keeps, per concealed hand, the `Shanten`, the improving kinds, the winning kinds, the standing value and the discards that leave the lowest shanten (`HandFacts`), a hash table for the memo, and the number of copies drawn on the line; the sampled profile before it said `Shanten` was two thirds of a decision. A golden table of 11 positions (`OwnDrawsTest`: depth 1 and 2, `ValueWaits`, the held hand with and without a loose tile) was written against the old code and does not move. Debug, `--jobs 1 --time`, seeds 4000 .. 4003, focus `5:ownDrawsDepth=2`, others `5`, the machine shared (two runs each):

| | mean decision | slowest decision |
|---|---|---|
| before | 85.9 to 86.5 ms | 1751 ms |
| after | 37.3 to 37.7 ms | 673 to 693 ms |

The first discard of 14 tiles is still 2.2 times over the 0.3 s of the time rule (a): the cheap steps give a factor 2.3 on the mean and 2.6 on the slowest decision, not 4. Release, the same command: 6.2 ms a decision, 121 ms slowest. Left as it was, and why: `WinScore` (3 % of a decision), and the test "does this hand wait" by the shanten, which would miss seven pairs and thirteen orphans (they wait at a high shanten).

## Slice 2: the measurement

Played by the rule written before any number (`todo-loop-mahjong-own-draws-ev-2`, "The rule of the verdict"): players against three Normal seats (`--others 5`), paired with Normal, Release, `--jobs 14`; the exploration range `--first 4000 --seeds 1000` with the seven rows below and nothing else; the decision range `--first 100000 --seeds 1000` for the one row the rule picks; the confirmation range (`--first 130000`) and the random-seat range (`--others random --first 30000 --seeds 250`) only if the decision range allows. Normal's own row moved from slice 1's (avg win score 45.93 here against 45.46, see "the hit rate of a discard"); every row below is measured on the code of this branch. **The constants were not retuned:** `kDiscardHitPermille` is the rate measured by `--table-stats` (806), `kOpponentWinPerRoundPermille` and the typical scores are slice 1's, and no row asked for it: the one row that is positive on points and not significantly negative on wins is X6, whose gap is the depth, not a constant. No default was changed, **every knob stays off in every level**.

### Debug time

The time rule (a): the slowest decision at most 0.3 s in Debug, `--jobs 1 --time`, seeds 4000 .. 4015, two runs, the smaller of the two slowest decisions (the `slowest us` column of the arena, so no temporary print this time). Depth 3 on seeds 4000 .. 4003 only. The four runs were played side by side on one machine (one thread each, the machine shared), so the times are a little high; none is near the limit.

```bash
$D --first 4000 --seeds <16 | 4> --jobs 1 --time --others 5 --focus 5:<knobs>        # twice
```

| variant | decisions | slowest decision (run 1 / run 2) | mean decision | rule (a) |
|---|---|---|---|---|
| `ownDrawsDepth=2` | 661 | 840 / 993 ms | 45.8 / 47.6 ms | **out**, 2.8 times |
| `ownDrawsDepth=2,ownDrawsDiscardWins=1` | 658 | 835 / 1005 ms | 46.0 / 47.4 ms | **out**, 2.8 times |
| `ownDrawsDepth=3` (4 seeds) | 170 | 11116 / 12551 ms | 212 / 215 ms | **out**, 37 times |
| `ownDrawsDepth=3,ownDrawsDiscardWins=1` (4 seeds) | 170 | 11506 / 12281 ms | 213 / 213 ms | **out**, 38 times |

The discard wins cost nothing measurable. The slowest decision is the first discard of 14 tiles. The rows with no depth (the claims, the declaration, the pick-wait) were not timed in Debug: none of them reached the point where the time decides.

### The timing of 1000 seeds, estimated from 20

Release, `$A --first 4000 --seeds 20 --jobs 14 --others 5 --focus 5:<knobs>`, wall time, and what the same row then took on 1000 seeds (20 seeds fill 14 threads in two waves, so the estimate of 50 times is about twice too high):

| row | 20 seeds | x50 estimate | played in |
|---|---|---|---|
| X1 `ownDrawsDepth=2` | 1.04 s | 52 s | 67 s with X2, X5 and Normal in one command |
| X2 depth 2 + discard wins | 0.76 s | 38 s | |
| X3 claims 2 + discard wins | 0.15 s | 8 s | 7 s with X4 and Normal |
| X4 declaration 2 + discard wins | 0.06 s | 3 s | |
| X5 pick-wait + discard wins | 0.04 s | 2 s | |
| X6 depth 3 (with and without discard wins) | 5.6 / 5.3 s | 280 / 263 s | 134 s with Normal |
| X7 (depth 2, discard wins, claims 2) | not timed alone | | 35 s with Normal |

No row was over the 20 minutes, so X6 was played. Depth 4 was not played (slice 1: 21 s a first discard in Debug).

### The exploration range

```bash
$A --first 4000 --seeds 1000 --jobs 14 --others 5 --focus 5,5:ownDrawsDepth=2,5:ownDrawsDepth=2,ownDrawsDiscardWins=1,5:ownDrawsPickWait=1,ownDrawsDiscardWins=1   # X1, X2, X5
$A ... --focus 5,5:ownDrawsClaimDepth=2,ownDrawsDiscardWins=1,5:ownDrawsDeclareDepth=2,ownDrawsDiscardWins=1                                                      # X3, X4
$A ... --focus 5,5:ownDrawsDepth=3,ownDrawsDiscardWins=1                                                                                                          # X6
$A ... --focus 5,5:ownDrawsDepth=2,ownDrawsDiscardWins=1,ownDrawsClaimDepth=2                                                                                     # X7
```

The settings the rule left to the numbers, decided from them and written before the later rows were played: X2's paired points (+5.41) are above X1's (+5.37), so X3 and X4 are played **with** `ownDrawsDiscardWins=1`; X6 is the depth-3 version of X2 (`ownDrawsDepth=3,ownDrawsDiscardWins=1`); X7 holds the knobs whose own row is above 0: the depth (X1, X2, X6; the best depth played is 2: +5.41 against +5.26), the discard wins (X2 above X1) and the claims (X3, +0.17), at depth 2; the declaration (X4, -2.19) and the pick-wait (X5, -0.60) are out.

| row | wins % | points/hand | declared % | avg win score | paired points/hand | paired wins % |
|---|---|---|---|---|---|---|
| Normal | 25.00 | 0 | 55.77 | 45.93 | | |
| X1 depth 2 | 22.45 +- 1.08 | 5.37 +- 4.68 | 49.27 | 50.56 | +5.37 +- 4.68 sig. | -2.55 +- 1.08 sig. |
| X2 depth 2 + discard wins | 22.57 +- 1.09 | 5.41 +- 4.68 | 49.85 | 50.74 | +5.41 +- 4.68 sig. | -2.42 +- 1.09 sig. |
| X3 claims 2 + discard wins | 24.55 +- 0.69 | 0.17 +- 2.19 | 55.73 | 45.84 | +0.17 +- 2.19 | -0.45 +- 0.69 |
| X4 declaration 2 + discard wins | 23.95 +- 0.33 | -2.19 +- 0.97 | 55.38 | 46.49 | -2.19 +- 0.97 sig. | -1.05 +- 0.33 sig. |
| X5 pick-wait + discard wins | 24.70 +- 0.32 | -0.60 +- 1.45 | 55.77 | 45.89 | -0.60 +- 1.45 | -0.30 +- 0.32 |
| X6 depth 3 + discard wins | 24.57 +- 1.09 | 5.26 +- 4.16 | 54.75 | 48.22 | +5.26 +- 4.16 sig. | -0.42 +- 1.09 |
| X7 depth 2 + discard wins + claims 2 | 21.82 +- 1.08 | 4.38 +- 4.46 | 48.80 | 50.64 | +4.38 +- 4.46 | -3.17 +- 1.08 sig. |

Seven rows are seven tries on one range, so the three "significant" rows on points (X1, X2, X6, all the same depth knob and not three findings) are read together, and none alone. The rule's pick for the decision range: among the rows whose paired wins % is not significantly below 0 (X3, X5, X6) the best by the paired points is **X6**. X1 and X2 are out of that choice by the rule (their wins are significantly below), although they hold the same points: the depth-2 value turns the hand toward dearer wins (avg win score +4.6 to +4.8, pungs and dragon sets up) and pays with the speed (declared 49 %, wins -2.5 %).

### The decision range

```bash
$A --first 100000 --seeds 1000 --jobs 14 --others 5 --focus 5,5:ownDrawsDepth=3,ownDrawsDiscardWins=1      # about 3 minutes
```

| row | wins % | points/hand | declared % | avg win score | paired points/hand | paired wins % |
|---|---|---|---|---|---|---|
| Normal | 24.98 | 0 | 55.85 | 45.08 | | |
| X6 depth 3 + discard wins | 23.57 +- 1.09 | 2.04 +- 3.95 | 53.80 | 48.41 | +2.04 +- 3.95 | -1.40 +- 1.09 sig. |

The paired points fall from +5.26 to +2.04, inside the interval of both (the usual fall of the best of seven: the exploration picked a row for its high number) and are not significantly above 0; the paired wins are significantly below 0. By the rule the confirmation range and the random-seat range are **not played** (nothing to confirm).

### The verdict

By "The rule of the verdict": (1) both ranges with the paired points significantly above 0 and the paired wins not significantly below: **fails** (decision range +2.04 +- 3.95, wins -1.40 +- 1.09 sig.); (2) pooled at least +3: (+5.26 and +2.04 pool to +3.65, which is not a pass because (1) fails); (3) random seats: not played; (4) the time rule: **fails** for every row from depth 2 (slowest Debug decision 0.84 s at depth 2, 11 s at depth 3; limit 0.3 s). The decision range does not even give "marginal". **No switch.** Levels 6..10 stay Normal, every knob is off in every level.

**No variant passes the strength conditions (1) to (3) and fails only the time rule**, so the resumable stepped run of the plan's WP 8 is not built: a stepped run would give the same answer as the one-shot function, and the answer is not better than Normal's.

### What part of the value was looked at

- **The win on a discard (`ownDrawsDiscardWins`): empty.** Alone it changes nothing; at depth 2 it moves the paired points by +0.04 (X2 against X1) and the wins by +0.13 %, far inside the noise, and the pick-wait with it (X5) is -0.60, as without it (slice 1: -0.41). 76 % of Normal's wins are on a discard; a reading, not measured: the closed form already ranks the waits by their copies, and the discard model lifts all of them together instead of changing their order.
- **The claims (`ownDrawsClaimDepth`): empty.** +0.17 +- 2.19 points, -0.45 wins; added to depth 2 (X7) it lowers the points (+4.38 against +5.41) and the wins (-3.17 against -2.42): no help.
- **The declaration now or later (`ownDrawsDeclareDepth`): harmful.** -2.19 +- 0.97 points and -1.05 +- 0.33 wins, both significant, with the declared share falling only from 55.77 to 55.38 %: the few times the value waits it loses more than the lost round the model charges, so the model undercharges what a delayed declaration costs (a reading, not looked at further: the value has no opponent's hand in it).
- **The depth: this is where the points are, and it does not hold.** Depth 2 gives +5.4 points a hand on the exploration range and pays 2.4 to 2.6 wins % for it (a hand worth more and later); depth 3 loses fewer wins (-0.42) for the same points, then +2.04 and -1.40 sig. on a fresh range. Neither is a result the rule accepts, and both are slow (0.84 s and 11 s in Debug for the first discard). Whether the +5 of depth 2 is real is not known: depth 2 was out of the decision range by the wins rule, as written.
- **Not played, and why:** depth 4 (21 s a first discard in Debug in slice 1); the confirmation range and the random-seat range (the decision range failed); Debug time of the rows without a depth; a leaf estimate for a hand that does not wait where the search stops (a new entry, "Left for the owner" of the plan).
- **The entry closes:** the verdict is no switch; the owner closes it.

### Loose ends of the arena code

- `--table-stats` counts a concealed kong as two own turns of the seat (the kong and the replacement draw each open an own turn); this biases `h` slightly and is left as it is.
- `Arena/ArenaTable.cpp` and `Arena/ArenaRun.cpp` each run the seed-slot thread loop (an atomic next seed, lowest failed seed, a world pool a thread); one helper would remove the copy; it was not made, because the entry ends with no switch and the arena code stays as it is.
