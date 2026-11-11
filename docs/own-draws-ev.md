# The value of a discard from the seat's own draws

Slice 1 of the entry `mahjong-own-draws-ev`: a computer seat rates each discard in expected points from its own next draws, and the arena says whether that beats Normal. **Verdict: no switch.** The knobs `ownDrawsDepth` and `ownDrawsPickWait` stay in the tree, off in every level; no level was changed.

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

**Retuning.** Only on the exploration range, once: `h` fitted to the survival after three rounds (37, 235, 382, 518) was played against the measured table for all three variants below and was not better (the paired points of `ownDrawsDepth=1` fell from -7.35 to -8.77, those of the pick-wait stayed at -0.4), so the measured constants stayed.

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

Depth 3 and 4 were timed on the first hand of seed 4000 only: the worst of a subset is a lower bound of the worst of the sample, so one decision over 0.3 s is enough to put a depth out. **Depth 2 is out by 4.4 times.** The cost is the first discards (14 tiles, six candidates, the full tree); the mean is far below the rule. Slice 2 therefore starts with the resumable stepped run (`ComputerThinking`), as the plan said. By the rule written first, **no range was played at depth 2, 3 or 4.** Depth 1 was not in the list of the exploration range of the plan (which named 2, 3, 4 "inside the time rule"); it is inside the rule, and it was added to the exploration before any strength number was seen, so every depth the rule lets in was looked at.

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

The pick-wait alone is the best variant by the paired points (-0.41), so it alone went to the decision range. (Fitted `h`, same range: pick-wait -0.36 +- 1.12, depth 1 -8.77 +- 4.30 sig., depth 1 + pick-wait -7.25 +- 4.50 sig.)

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
- **The standing score and doubles, at the depth that fits (1):** the value moves play toward value hands, and the score lines show it: pungs in 78.8 % of the won hands (Normal 66.9), dragon sets 19.0 (16.1), one suit with honours 1.6 (0.4), only pungs 0.7 (0), no-points hands 7.4 (14.2), the average winning score 52.4 (45.5, +15 %). It pays with speed: declared 40.8 % (55.8), wins 17.2 % (25.0), drawn hands 0.55 % (0). With one draw of horizon a hand that does not wait after the draw cannot win, so the number that differs between two discards is the standing hand's score, which is worth more than a one-draw chance to win; the gain in score is smaller than the loss in wins. Concealed against open hands (`FullyConcealed` 4.35 % against 4.59) did not move.
- **Not looked at:** the claims, the declaration "now or later", and depth 2 and up (out by the time rule). Whether the deeper value would beat Normal is **unknown** from this slice: the one number that says the structure may carry more is the score-line shift at depth 1, which is real and in the intended direction.
- **The blind spot that stays:** the 76 % of the won hands that are won on a discard are not in the value at all.

## For slice 2

The gate is the result: no variant inside the time rule is positive, and the part that is cheap enough (the wait) is empty. Slice 2 would have to start with the resumable run (depth 2 is 4.4 times over the rule), and the claims, that is, the rule that Normal wins 76 % on discards, are where the value is not yet modelled. The owner decides whether that is worth building as it stands (`mahjong-own-draws-ev`, "Left for the owner").
