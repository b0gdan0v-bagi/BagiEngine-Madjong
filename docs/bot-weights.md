# The weighted player and its tuning

The entry `mahjong-selfplay-weight-tuning` asks whether a computer player whose choices are a **sum of weights times features**,
with the weights tuned by self-play in the bot arena (`docs/bot-arena.md`), beats Normal, whose order (shanten, then acceptance, then
keep value) was set by hand. It is built in four stages, one slice each; this page is the record, written in the order of the work:
the policy and the method **before any run**, the numbers after. Stage 1 (this section's code) delivers the family and its cost and
no run of a tuner; the tuner, the training and the judged run are stages 2 to 4 and fill the sections marked so.

The code is `src/Mahjong/Game/ComputerWeights.{h,cpp}`, called from `ChooseDiscard` (`ComputerTurn.cpp`) and `DecideClaim`
(`ComputerClaim.cpp`) only when `LevelProfile::weighted` is on; the weights and their defaults are in `ComputerLevel.h`. **No level
turns `weighted` on, and nothing here changes a level.**

## The policy

With `weighted` off the code path is today's, byte for byte. With it on and the default weights the choices **and the draws from the
rng** are Normal's; a test sweeps random hands and claim windows for it (`ComputerWeightsTest`), and the arena's paired difference of
`normal:weighted=1` against `normal` is exactly 0 (`ArenaRunTest`, and "Cost" below at 1000 seeds). Steps 1 to 4 of the turn (win,
declare, kong, keep the wait) and steps 1 and 2 of a claim window (mahjong, the waiting hand) do not read the weights. The hints and
the coach (`ExplainAdvice`) explain Normal's order: a weighted profile is not supported there and is never passed to it.

**Features.** Every feature is oriented so that a larger weight means more of the good thing, and the weights are whole numbers
`0 .. kWeightMax` (65535):

| feature | of | value |
|---|---|---|
| nearness | the 13 tiles left | `kShantenCeiling - shanten` (8 minus the shanten) |
| acceptance | the 13 tiles left | `AcceptanceOf`: unseen copies that lower the shanten, at most `kAcceptanceMax` (136) |
| keep | the discarded kind | `kKeepValueTile - KeepValue`, 0 to `kKeepSpread` (3) |
| safety | the discarded kind | `kDangerWorst - Danger` (`DiscardDanger.h`), 0 to 3; **0 danger for every kind unless `wSafe > 0` and an opponent has declared** |
| claim rank | a claim | `ClaimRank` (chow 0, pung 1, kong 2: `ComputerClaim.h`) |

**Discard** (step 5, after the `skipAcceptancePercent` roll and the `ownDrawsDepth` branch, which keep their order and draws):
`R = wShanten * nearness + wAcceptance * acceptance + wKeep * keep + wSafe * safety` for every kind held (Normal rates only the
lowest-shanten kinds), in 64 bits. The highest `R` is discarded; kinds with an equal `R` are the ties, in the order of the tile ids,
and `PickOne` takes one, drawing from the rng exactly when there is more than one. `readDanger` is not read: `wSafe` replaces it.

**Claim** (a window with a pung, kong or chow listed, after the mahjong and the waiting hand are answered; it takes the place of
`BestClaims` and of the final `Roll(skipClaimPercent)` only when `weighted`; `ownDrawsClaimDepth` and the search stay ahead). For
each listed claim, the hand after it is the 13 tiles left by its best discard (the best by the discard rating, the lowest tile id
among equal ones, no rng; a kong is the hand as it stands, as `RateClaim`). With `H(s, a) = wShanten * (kShantenCeiling - s) +
wAcceptance * a`, the claim's rating is `C = H(sAfter, aAfter) + wClaimRank * ClaimRank` and the pass's is `P = H(sBefore, aBefore)`.
A claim is **acceptable** when

```
C - P + (value pung or kong at an equal shanten ? wClaimValue : 0)  >  wClaimMargin + wClaimWall * liveCount / kWallWeightUnit
```

(`kWallWeightUnit` 100 tiles; compared without a division, so the slope is not rounded). The **best acceptable by `C`** is taken (the
bonus is not part of the order); equal ones by `PickOne`; then `Roll(skipClaimPercent)` may pass it, as in Normal, so the draws are
Normal's. **`takeValueClaimsAtEqualShanten` is not read on this path**: `wClaimValue` is its weight, so a weighted seat takes value
claims at an equal shanten whatever the flag says. Levels 1 and 2 have the flag off, so `weighted=1` on them is not their unweighted
play at the default weights; on Normal and levels 3 and up (the flag on) it is.

**The default weights are Normal's, and why that is exact.** Each weight is above the largest spread of every term below it, so
the sum orders like Normal's lexicographic order; `ComputerLevel.h` states each step as a `static_assert` from the named constants,
so a change of one constant is caught at build time:

| weight | default | above |
|---|---|---|
| `wKeep` | 1 | (the lowest term) |
| `wSafe` | 0 (`kNormalWeightSafeStep` 4 stands for `readDanger`) | the span of keep, 3 |
| `wAcceptance` | 16 | the spread of keep plus of 3 grades of danger at 4, 15 |
| `wShanten` | 4400 | the spread of acceptance, 136 x 16 = 2176 |
| `wClaimRank` | 1 | (the rank spread is 2, below `wAcceptance`) |
| `wClaimWall` | 0 | |
| `wClaimMargin` | 2178 = 16 x 136 + 1 x 2 | a gain of acceptance and rank alone never passes |
| `wClaimValue` | 4400 = `wShanten` | a value claim at an equal shanten passes |

A lowered shanten gains at least `wShanten` minus the whole acceptance (4400 - 2176 = 2224) and passes the gate (above 2178)
whatever the acceptance does; a raised shanten loses at least as much and never passes. The margin is 2178, not 2176: the claim's
rank is in `C`, so the gate has to clear the rank too. Where this could not be exact the constants would change, not the rule.

**Knobs.** `weighted` and the eight weights are rows of `kProfileKnobs`, so the arena sets them
(`normal:weighted=1,wShanten=3000`); each is in `kNormalProfile`, so every level has them at the Normal-equivalent defaults.

## The split (fixed before any run)

- **Training seeds** are `>= 200000` only; generation g plays a slice of its own above that bound. The tuner (stage 2) refuses to
  train anywhere else.
- **Exploration range**, `--first 4000`, 1000 seeds: may be looked at, and is the monitoring call of the curve (the mean against
  three Normal seats). **Never used to choose.**
- **Decision range**, `--first 100000`, and **confirmation range**, `--first 130000`, 1000 seeds each: played **once** with the final
  weights (the ranges of `docs/bot-search.md`, "Levels"); the tool refuses to play a judged range twice for the same weights.
- **The weights that are judged** are the last generation's mean, rounded, fixed by this rule before the judged run: never the best
  generation by a monitored number.

## The method (stage 2 fills the numbers)

**Cross-entropy (CEM), not SPSA.** One generation is one arena call: the current mean is the first focus player and N sampled
candidates follow it, **all three other seats the current mean** (self-play, as Suphx trains); the first focus player is the
baseline of the paired rows, so a candidate's score is its paired points a hand against the mean in a table of means. The elite are
the best quarter by paired points; the mean and the spread move by smoothing. Weights are sampled on `log1p(weight)`, so a spread is
relative (the Normal-equivalent start has a ratio of hundreds between the shanten and the keep weight, and 0 stays reachable). It is
seeded (`random.Random(seed)`), so a run is repeated by its seed, and every generation plays fresh training seeds.

Why CEM: the objective is noisy, and every arena call returns N paired values at once on the same walls (common random numbers), so
a population is the natural use of the arena; the weights live on a lattice with scales from 1 to thousands, and CEM adapts a spread
per weight with no step-size schedule to tune. SPSA's gain and perturbation sequences are exactly the knobs that a noisy,
scale-ragged objective makes fragile, and it uses two cheap evaluations a step where a population is robust against the noise of one
candidate.

The budget (candidates, seeds a generation, generations) is sized from "Cost" below and written here by stages 2 and 3 before the
run it is for. The pilot (stage 2) and the training (stage 3) record their curve and weights in this section; the numbers of
stage 1 do not tune anything.

## The verdict rule (fixed before any run)

As the "Levels" rule of `docs/bot-search.md`. The weights are **stronger than Normal** only when, against three Normal seats on
**both** the decision and the confirmation range, the paired difference of points a hand and of wins % is significant and positive,
and the pooled difference (the mean of the two ranges, half-width `sqrt(h1^2 + h2^2) / 2`) of points and of wins is positive. Two
more judgements, each read on the pooled interval of the two ranges:

- **seats turned**: Normal as the focus against three tuned seats; Normal's points a hand and wins % must not be significantly above 0
  and 25 %;
- **against three random seats**: the tuned player is not significantly worse than Normal, paired.

The record says how each came out; a failed one makes the verdict "not stronger", whatever the first says. Only a verdict "stronger
than Normal" makes the weights a named profile (`kTunedProfile`, the arena player `tuned`, stage 4); which levels take them is the
owner's decision.

## Cost

The weighted seat rates every kind held (Normal rates the lowest-shanten ones) and, in a claim window, the best discard of each
claim by the weighted rating, so it is dearer per decision. Measured on 2026-11-16, Release `BagiMahjongArena` (`macos-release`), Apple
M4 Pro, 14 logical cores (10 performance, 4 efficiency), other work on the machine, `--jobs 0` (all cores), `--others normal --first
4000 --seeds 1000 --time` (4000 hands of each focus player, 1000 seeds, 4 rotations):

| run | wall time | hands a second | us a decision, the focus seat |
|---|---|---|---|
| `--focus normal,normal:weighted=1`, first | 7.83 s | 1020 | 219 and 405 |
| the same, second | 8.13 s | 984 | 280 and 507 |
| `--focus normal` alone | 3.25 s | 1230 | 229 |
| `--focus normal:weighted=1` alone | 4.32 s | 926 | 477 |

A decision of the weighted seat takes about 1.8 to 2.1 times Normal's (the table sums the threads' microseconds and `--time`
reads a clock around every decision, so the absolute numbers are higher than a run without it), and a run in which one seat of
four is weighted takes about 1.33 times the wall time of an all-Normal one. **The two focus players' rows are identical** (the report
above the time table is the same bytes in both runs, and the rows of each alone have the same numbers): wins 25.00 %, points 0.00,
the paired difference row `+0.00 +- 0.00` on every column, 1000 seeds. Stage 2 and 3 size a generation from about 1000 hands a second for a
candidate (a run that has one weighted focus player among all-Normal seats), 14 cores, to be measured again for the candidates of a
real generation.

## Not done by this stage

The JSON report and the tuner (stage 2), the own-draws value as a feature (stage 3), the judged run, any named constant and any
change of a level (stage 4).
