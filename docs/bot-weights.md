# The weighted player and its tuning

The entry `mahjong-selfplay-weight-tuning` asks whether a computer player whose choices are a **sum of weights times features**,
with the weights tuned by self-play in the bot arena (`docs/bot-arena.md`), beats Normal, whose order (shanten, then acceptance, then
keep value) was set by hand. It is built in four stages, one slice each; this page is the record, written in the order of the work:
the policy and the method **before any run**, the numbers after. Stage 1 (this section's code) delivers the family and its cost and
no run of a tuner. Stage 2 adds the report (`--json`), the tuner and a pilot run of it ("The pilot"); the training and the judged run are
stages 3 and 4.

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
| discard value | the discarded kind | `V`: the own-draws value of the discard above the lowest rated one, in milli-points (below); 0 unless `wValueDiscard > 0` |
| claim value gain | a claim | `G`: the own-draws value of the claim minus that of the pass, in milli-points, signed (below); 0 unless `wValueClaim > 0` |

**Discard** (step 5, after the `skipAcceptancePercent` roll and the `ownDrawsDepth` branch, which keep their order and draws):
`R = wShanten * nearness + wAcceptance * acceptance + wKeep * keep + wSafe * safety + wValueDiscard * V` for every kind held (Normal rates only the
lowest-shanten kinds), in 64 bits. The highest `R` is discarded; kinds with an equal `R` are the ties, in the order of the tile ids,
and `PickOne` takes one, drawing from the rng exactly when there is more than one. `readDanger` is not read: `wSafe` replaces it.

**The discard value** `V` (stage 3). `ValueDiscards(view, OwnDrawsModel{.depth = weightedValueDepth})` (`docs/own-draws-ev.md`) rates at
most `kOwnDrawsMaxCandidates` kinds, in milli-points (`kMilliPerPoint` 1000 a point), in fixed point and 64 bits. `V(kind)` is its
`expected` minus the lowest `expected` of the rated kinds: 0 for the worst rated kind and for every kind it does not rate, never
negative, so a larger weight always means more of the better discard. Not a discard turn (nothing rated): `V` is 0 for all. It is
**computed only when `wValueDiscard > 0` and `weightedValueDepth > 0`**: at 0 the call is not made, so the choice, the draws from the
rng and the cost are the nine-weight family's. It is added only in the choice of the discard of step 5 (`ChooseWeightedDiscard`);
the best discard after a claim stays by the nine-weight rating (the claim's own value is `G`), and `ownDrawsDepth > 0` still goes first.

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

**The claim value gain** `G` (stage 3). `ValueClaims(view, OwnDrawsModel{.depth = weightedValueDepth})` rates the pass and each claim it
lists; `G = value(claim).expected - value(pass).expected`, signed, matched to the legal claim by `HandAction::operator==`. A claim
it does not list, or an empty answer, has `G = 0`. The claim's rating becomes `C + wValueClaim * G`, so the gate above sees the gain
in `C - P`, and the best acceptable claim is by the new `C`. Like `V` it is **computed only when `wValueClaim > 0` and
`weightedValueDepth > 0`**, once for the window; `ownDrawsClaimDepth > 0` and the search stay ahead of this path.

**The depth** `weightedValueDepth` (0 .. `kOwnDrawsMaxDepth`, default `kWeightedValueDepth`) is the horizon, in own draws, of both
values; 0 means no value term. It is a knob of the arena, never a coordinate of the tuner: every candidate, the mean and the other
seats play the same depth. **It is 1**: by the rule written before the measurement (the largest depth in {1, 2} for which a generation
of the proposed shape, all four seats weighted with both value weights near 4, is estimated at 60 s or less; else 1) depth 2 is out
at over 700 s a generation and depth 1 is the answer in any case ("Cost").

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
| `wValueDiscard` | 0 | (a feature that is off at the Normal-equivalent start: at 0 it is not even computed) |
| `wValueClaim` | 0 | (the same) |

A lowered shanten gains at least `wShanten` minus the whole acceptance (4400 - 2176 = 2224) and passes the gate (above 2178)
whatever the acceptance does; a raised shanten loses at least as much and never passes. The margin is 2178, not 2176: the claim's
rank is in `C`, so the gate has to clear the rank too. Where this could not be exact the constants would change, not the rule.

**Knobs.** `weighted`, the ten weights (the eight above and `wValueDiscard`, `wValueClaim`) and `weightedValueDepth` are rows of
`kProfileKnobs`, so the arena sets them (`normal:weighted=1,wShanten=3000`, `normal:weighted=1,wValueDiscard=4`); each is in
`kNormalProfile`, so every level has them at the Normal-equivalent defaults. A point is 1000 milli-points, so a value weight of 4 is
about one shanten step (`wShanten` 4400) for a difference of one point: the scale the tuner starts from.

## The split (fixed before any run)

- **Training seeds** are `>= 200000` only; generation g plays a slice of its own above that bound. The tuner (stage 2) refuses to
  train anywhere else.
- **Exploration range**, `--first 4000`, 1000 seeds: may be looked at, and is the monitoring call of the curve (the mean against
  three Normal seats). **Never used to choose.**
- **Decision range**, `--first 100000`, and **confirmation range**, `--first 130000`, 1000 seeds each: played **once** with the final
  weights (the ranges of `docs/bot-search.md`, "Levels"); the tool refuses to play a judged range twice for the same weights.
- **The weights that are judged** are the last generation's mean, rounded, fixed by this rule before the judged run: never the best
  generation by a monitored number.

## The method

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

**The tool and its numbers** (`tools/mahjong_tune.py train`, standard library only; the contract is in `tools/CLAUDE.md`). The
coordinates are the ten weights of `WEIGHTS` (the Normal-equivalent defaults above, maximum `kWeightMax`: the eight of stage 1 and, from
stage 3, `wValueDiscard` and `wValueClaim`, which start at 0 like `wSafe`; the start is still Normal itself and its monitoring row still
exactly 0). A candidate is
`w = clamp(round(expm1(mu + sigma * gauss)), 0, 65535)` per weight; the start is `mu = log1p(default)` with `sigma` 0.5, and 2.0 for
a weight whose default is 0 (`log1p(6)`; its samples below 0 become 0). The candidate's own `x = log1p(w)` is that of what was
**played**, rounded and clamped. The elite are the best `ceil(N / 4)` by paired points a hand (ties by index) and the update is
`mu' = (1 - a) mu + a mean(x_elite)`, `sigma' = max(0.05, (1 - a) sigma + a std(x_elite))` with `a` 0.5. The stream of a generation is
`random.Random(seed * 1000003 + g)`, so a resume needs only the state of the last generation.

**The tie rule** (stage 3; the pilot's finding, "The pilot"). A candidate is a **twin** of the mean when its paired points a hand are
exactly 0 with half-width 0: it played the very same hands, so its weights are on the mean's plateau of the piecewise constant
objective and it says nothing. When **every member of the elite is a twin** (no candidate beat the mean, and the best quarter by index
is the first four twins), the generation is a **tie generation**: `mu` does not move, `sigma` becomes `min(start sigma, 1.25 sigma)`
per weight (the start sigma is 0.5, 2.0 for a weight that starts at 0), the record has `"tie": true` and `"elite": []`. A spread that the
earlier updates collapsed to the floor (0.05) is back at the start's in 11 ties (17 for a weight that starts at 0). Any other generation
is the update above, and a twin among better ones stays in the elite as before. The start stays Normal.

**One generation is two arena calls.** Training: `--focus <mean>,<c1>,..,<cN> --others <mean> --first <200000 + g * seeds> --seeds
<seeds> --jobs 0 --json` (the mean first: the baseline of the paired rows; every other seat the mean). Monitoring: `--focus
normal,<new mean> --others normal --first 4000 --seeds <monitor seeds> --json`, read as the paired row of the new mean against
Normal on the exploration range (it is looked at and **never used to choose**: the update reads the training call only). Before
generation 0 the start mean is monitored once and must score exactly 0 on points and wins, else the tool stops ("the start weights
are not Normal's"): a check that the weighted path with the defaults is Normal's.

**The guards** refuse a run, never clamp it: a training seed below 200000, an exploration range outside `[4000, 5000)`, any range
that touches the decision (`100000`) or confirmation (`130000`) range, an overflow of the 32-bit seed, a log inside a git work tree
(even a git-ignored place), an existing log without `--resume`, a resume whose header (seed, candidates, seeds, ranges, weights)
differs. The log is JSONL outside the repository (`~/.claude/plans/mahjong-tune/`): a header, then one record a generation (the
weights played, every candidate's paired points and wins, the elite, the new mean, `mu`, `sigma`, the monitoring row, hands,
`wallSeconds`); everything but `wallSeconds` is a function of the seed and the arena's answers, and a resumed run writes the same
bytes as an uninterrupted one.

**To repeat a run**: the same command line (below) with the same arena build. The seed and the options fix the log; the arena's
`--json` bytes are the same at any `--jobs`.

### The budget of the pilot (written before the pilot ran)

The pilot is a smoke of the method, not a result: it asks whether the loop runs and what its curve looks like. At most 10 minutes of
wall time in one foreground command; the tool stops before a generation that would end after 480 s, so the overrun of one generation
stays under 600 s.

- **Cost probe**, Release arena, `--jobs 0`, 2026-11-16, Apple M4 Pro, 14 logical cores, **load average about 90 from the other workers
  of the todo loop** (the numbers are dearer than a quiet machine's, and so is the pilot's wall time per hand): one generation with
  250 seeds and the monitoring call of 250 seeds took 16.3 s with 8 candidates (11000 hands) and 28.1 s with 16 candidates (19000
  hands), 675 hands a second both times. That is about 0.7 times the 1000 hands a second of "Cost" below: all four seats are
  weighted here, and the machine was loaded.
- **Chosen**: 16 candidates, 250 seeds a generation (17 players x 1000 hands = 17000 hands), monitor seeds 250 (the start mean and the
  new mean, 2 x 1000 hands), tool seed 7, `--generations 20`, `--max-seconds 480`. A generation is 19000 hands, about 28 s; the start
  monitoring is 2000 hands, about 3 s. The time stop (480 s) leaves room for about **16 generations** (3 s + 16 x 28 s = 451 s;
  one more would end after 480 s), 304000 hands plus 2000. `--generations 20` is only a ceiling that the time stop is expected to
  reach first.
- **The command**:
  `python3 tools/mahjong_tune.py train --arena build/macos-release/bin/BagiMahjongArena --log ~/.claude/plans/mahjong-tune/pilot.jsonl
  --seed 7 --candidates 16 --seeds 250 --monitor-seeds 250 --generations 20 --max-seconds 480 --jobs 0`.
- A monitoring row of 250 seeds is 1000 hands: its half-width is about 1 to 2 points a hand and 0.2 to 0.6 wins %, so a curve is
  readable only when the move is a few points; this is the noise the pilot is to show.

### The pilot (stage 2; a smoke of the method, not a result)

Run 2026-11-16 with the command of the budget above, one foreground command, the log
`~/.claude/plans/mahjong-tune/pilot.jsonl` (outside the repository, as every output of the run). The machine was the one of the probe
(load average 80 to 180 from the other workers; a 8 s run of the tuner's Python test overlapped it once): 474 s wall time, **18
generations** (the budget said about 16: the generations took 19.7 to 36.5 s, 26.3 s on average, against the probe's 28 s, so two more
fitted), 344000 hands (the start monitoring 2000 plus 18 x 19000), the tool stopped by `--max-seconds` before generation 18, as it
should, with exit 0. The budget above was not edited.

The curve: the mean of each generation against three Normal seats on the exploration range `[4000, 4250)` (1000 hands), paired points
a hand and wins %, with the half-widths of the log; `significant` is the log's flag on either.

| g | points a hand | wins % | significant | mean: wShanten wAcceptance wKeep wSafe wClaimRank wClaimValue wClaimMargin wClaimWall |
|---|---|---|---|---|
| 0 | -0.32 +- 2.07 | +0.0 +- 0.6 | no | 4864 14 1 1 1 5440 1540 1 |
| 1 | -0.32 +- 2.07 | +0.0 +- 0.6 | no | 5060 13 1 1 1 5227 1421 1 |
| 2 | -1.13 +- 2.27 | -0.1 +- 0.7 | no | 4536 13 1 1 0 5360 1414 3 |
| 3 | -1.13 +- 2.27 | -0.1 +- 0.7 | no | 4601 14 1 1 0 5911 1437 3 |
| 4 | -1.13 +- 2.27 | -0.1 +- 0.7 | no | 4614 15 1 1 0 6001 1530 3 |
| 5 | -0.81 +- 0.86 | -0.1 +- 0.2 | no | 4615 15 1 0 0 5546 1502 2 |
| 6 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 4563 14 1 0 1 5381 1389 1 |
| 7 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 4437 13 1 0 1 5464 1484 1 |
| 8 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 3993 13 1 0 1 5565 1410 2 |
| 9 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 3923 13 1 0 1 5526 1534 2 |
| 10 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 3885 13 1 0 1 5477 1556 2 |
| 11 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 3618 13 1 0 1 5611 1549 2 |
| 12 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 3761 13 1 0 1 5552 1488 2 |
| 13 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 3943 13 1 0 1 5634 1482 2 |
| 14 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 4195 13 1 0 1 5533 1430 2 |
| 15 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 4155 13 1 0 1 5441 1508 2 |
| 16 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 4201 13 1 0 1 5554 1485 3 |
| 17 | +0.00 +- 0.00 | +0.0 +- 0.0 | no | 4410 13 1 0 1 5615 1482 2 |

- **The last mean** (generation 17; the weights stage 4 would judge by the rule if this were the final run): `wShanten` 4410,
  `wAcceptance` 13, `wKeep` 1, `wSafe` 0, `wClaimRank` 1, `wClaimValue` 5615, `wClaimMargin` 1482, `wClaimWall` 2.
- **Best monitored mean so far**, a record only (nothing is chosen on the exploration range): +0.00 points and +0.0 wins %, first
  at generation 6 and the same on every later one; no monitored mean was above Normal.
- **What the curve shows.** Nothing above noise: the first six rows are within their half-widths of 0 (the 1000 hands of a row give
  about +-1 to 2 points), and from generation 6 on the row is **exactly** Normal's (half-width 0): the mean plays the very same hands as
  Normal. It is not a result and not a sign of learning, and it points at the method, not at the weights:
  - **The objective is piecewise constant.** A play depends on the order the weights give, not on their values. The last mean has
    `wAcceptance` 13 above the keep span (3) and `wShanten` above the acceptance span (136 x 13 = 1768), so its discards are Normal's,
    and its claim gate (1482) is above what an acceptance gain alone reaches in practice. Different weights (rows 0 and 1) give the same
    1000 hands.
  - **The search collapses onto that plateau.** All 16 candidates scored exactly 0 against the mean at generation 8 and from 10 on
    (14 of 16 at generation 7, 15 at 9, 6 or fewer before 6), and then the elite are the first four by index (the tie rule), which
    carries no information, and the spread falls to the floor on the small weights (`wKeep`, `wSafe` 0.05) while the large ones drift. Large
    moves of the early generations (a candidate at -46 points a hand) were rejected, the plateau around Normal was kept: this is what a
    method that finds Normal's ordering again looks like, and it found nothing better in 18 generations of 16.
  - **What this means for stage 3 and the training.** A generation whose candidates all tie says nothing: stage 3 decides what to
    do with a tie (skip the update, keep the spread, widen it) and whether the start should be Normal itself or a point off the
    plateau; the values and the noise (a row of 250 seeds cannot show a difference below about 1 point a hand; the candidates' own
    half-widths were about 0.5 to 2 while they differed) are the facts it starts from. The method was not changed after the pilot.

### The budget of the training (stage 3; written before the training ran)

The training tunes the ten weights of `WEIGHTS` in at most 30 minutes of wall time, in **three resumable commands of at most 10
minutes each**; it gives no verdict and plays no judged range (stage 4 does).

- **Probe**, 2026-11-16, Release arena (`macos-release`), Apple M4 Pro, 14 logical cores, `--jobs 0`, **not a quiet machine: load
  average 50 at the start of the probe, 65 to 82 during it, 76 at its end, from the other workers of the todo loop**. One command,
  `python3 tools/mahjong_tune.py train --arena build/macos-release/bin/BagiMahjongArena --log ~/.claude/plans/mahjong-tune/probe3.jsonl
  --seed 11 --candidates 16 --seeds 200 --monitor-seeds 250 --generations 3 --train-first 300000 --jobs 0`: 318 s in all, the start
  monitoring about 2 s, and generations of **40.6 s, 143.4 s and 132.1 s** (15600 hands each). The first is cheap because the mean is
  Normal's weights and three seats of four play without a value term; from the second the mean has value weights above 0 and so do
  the other three seats and most candidates. The probe's log is a working file, not the training's: the training uses another log name
  and the same seeds from `--train-first` (the probe's seeds are not special).
- **The number of seconds a generation is 132** (the probe's third, the better guess for a generation whose seats all carry value
  weights; the mean of the second and third is 138). It is about 118 hands a second, against 675 of the pilot (no value terms) and the
  50 to 63 s a generation that "Cost" estimated from a load of 33: the load of this machine is a factor of its own, and nothing here
  is made to look faster than it was.
- **Chosen**: 16 candidates, 200 seeds a generation (17 players x 800 hands = 13600 hands), monitor seeds 250 (the new mean's
  monitoring call is 2 x 1000 hands), tool seed 11, `--train-first 300000` (generation g plays seeds `300000 + 200 g` and on), the
  monitoring range `[4000, 4250)`, `--max-seconds 450`, `--jobs 0`. A generation is **15600 hands**, the start monitoring 2000. These
  are the proposal's numbers; **only `G` came from the probe**, the proposal had it as at most 40.
- **`G = floor(3 x 450 / 132) = floor(10.2) = 10` generations**, fixed now: 10 x 15600 + 2000 = **158000 hands** at most. The tool stops
  before a generation when the seconds since the command started plus the last generation's seconds reach 450, so a command ends
  under 450 s plus the amount by which its last generation took longer than the one before it (under 600 s unless a generation takes
  150 s more than the last one). With generations of 132 s the stops fall after about 4, 3 and 3 generations (the first has the cheap
  generation 0), which is the 10 of the budget; **a slower machine stops the third command before 10, and that is recorded, not
  corrected** (the budget is not edited afterwards).
- **The three commands**, the same line, the second and the third with `--resume` added, stdout of each to its own file next to the log
  (`~/.claude/plans/mahjong-tune/train3-1.out`, `-2`, `-3`):
  `python3 tools/mahjong_tune.py train --arena build/macos-release/bin/BagiMahjongArena --log ~/.claude/plans/mahjong-tune/train3.jsonl
  --seed 11 --candidates 16 --seeds 200 --monitor-seeds 250 --generations 10 --train-first 300000 --max-seconds 450 --jobs 0`.
- **The tie rule** ("The method") is in: a generation in which no candidate beats the mean or differs from it moves nothing and
  widens the spread, as the pilot's plateau (18 generations of the same row) asked. The start is Normal, so the start monitoring row is
  exactly 0 and the first mean may stay on Normal's plateau for some generations; the value weights start at 0 and are sampled above
  it from the first generation (`sigma` 2.0 on `log1p`, samples up to a few hundred), so a candidate with a value term leaves the plateau.
- **What the budget buys**: 10 generations of 16 candidates, a monitoring row of 1000 hands (half-width about 1 to 2 points a hand
  on a mean that plays like Normal and 8 on one that does not: the probe's rows, which are read only for the cost). Nothing here is
  expected to be a result; the record of the run is "The training (stage 3)" after it.

### The training (stage 3; not a result)

Run 2026-11-16 with the command of the budget above, the log `~/.claude/plans/mahjong-tune/train3.jsonl` and the outputs
`train3-1.out`, `-2`, `-3` next to it (outside the repository). Machine: the Release arena, Apple M4 Pro, 14 logical cores, `--jobs 0`,
**not a quiet one**: load average (1 minute) 51.7 before the first command, 19.1 before the second, 43.6 before the third, other
workers of the todo loop; noted, not corrected.

- **Command 1**: 434.4 s wall (the tool's generations: 35.2, 116.6, 131.1, 73.4 and 76.2 s), generations 0 to 4, stopped by
  `--max-seconds` before generation 5 (exit 0).
- **Command 2** (`--resume`): 389.8 s, generations 5 to 9 (60.8, 70.8, 69.3, 82.4 and 106.5 s), the log reached `G = 10` (exit 0).
- **Command 3** (`--resume`): 0.05 s; the log held 10 of 10 generations and the tool did nothing, as the plan said it would.
- **In all**: 824 s of wall time (the sum of the generations 822.3 s), 10 generations, **158000 hands** (10 x 15600 plus the start
  monitoring 2000), **0 tie generations**. The budget's `G` was reached; the budget text is not edited. The generations were faster than the probe's 132 s
  from the fourth on (60 to 107 s), because the load fell; the budget's guess of three commands was two.

The curve: the new mean of each generation against three Normal seats on the exploration range `[4000, 4250)` (1000 hands), paired
points a hand and wins % with the half-widths of the log; `sig` is the log's flag (points, wins). The start (Normal's weights) is
exactly 0 and 25.0 %.

| g | points a hand | wins % | sig | mean: wShanten wAcceptance wKeep wSafe wClaimRank wClaimValue wClaimMargin wClaimWall wValueDiscard wValueClaim |
|---|---|---|---|---|
| 0 | +11.54 +- 7.94 | +1.20 +- 2.13 | yes, no | 5275 14 1 1 1 5199 2267 0 3 1 |
| 1 | +11.36 +- 8.01 | +0.70 +- 2.12 | yes, no | 5655 13 1 1 1 4737 2162 0 8 1 |
| 2 | +10.84 +- 7.99 | +0.60 +- 2.10 | yes, no | 6969 12 1 1 1 5316 2135 0 6 1 |
| 3 | +11.98 +- 8.09 | +0.80 +- 2.09 | yes, no | 7314 11 1 1 1 5055 1900 0 4 2 |
| 4 | +11.55 +- 8.03 | +0.60 +- 2.06 | yes, no | 7917 10 1 1 1 5233 1682 0 4 2 |
| 5 | +11.27 +- 8.06 | +0.40 +- 2.08 | yes, no | 7705 9 1 1 1 4555 1609 0 4 2 |
| 6 | +11.25 +- 8.06 | +0.40 +- 2.08 | yes, no | 7836 9 1 1 0 5051 1681 0 4 2 |
| 7 | +11.25 +- 8.06 | +0.40 +- 2.08 | yes, no | 8144 9 1 1 0 5623 1574 0 4 2 |
| 8 | +11.52 +- 8.04 | +0.50 +- 2.07 | yes, no | 7927 8 1 1 0 5713 1427 0 4 2 |
| 9 | +11.67 +- 8.04 | +0.60 +- 2.08 | yes, no | 7698 8 1 1 0 6229 1358 0 4 2 |

The points interval excludes 0 in every row (the log's flag, 1.35 to 1.48 half-widths from 0); the wins interval includes 0 in every row.

**The final weights** (the last generation's `newMean`, what stage 4 judges by the rule, not a chosen generation):
`wShanten` 7698, `wAcceptance` 8, `wKeep` 1, `wSafe` 1, `wClaimRank` 0, `wClaimValue` 6229, `wClaimMargin` 1358, `wClaimWall` 0,
`wValueDiscard` 4, `wValueClaim` 2 (the ten weights of `WEIGHTS`). **As a record only**: the best monitored mean so far on the
exploration range is generation 3 (+11.98 points a hand); nothing is chosen on that range.

**Reading.** The mean left Normal's plateau at once: the first mean of generation 0 is already +11.5 points a hand (half-width about 8,
paired, 1000 hands), and the row stays between +10.8 and +12.0 for all ten generations, with wins within 0.4 to 1.2 points of 25 %.
That is flat, not rising: the differences between the rows (about 1 point) are far inside the half-width (about 8). The weights moved
steadily (`wShanten` 4400 to 7698, `wAcceptance` 16 to 8, `wClaimMargin` 2178 to 1358, from the start's values) and both value
weights rose above 0 (`wValueDiscard` 0 to 4, `wValueClaim` 0 to 2), while the points row did not follow them. No tie generation
happened, so the tie rule was not used. The rows are one 1000-hand range and nothing is chosen on it: **this is not a result**, the
judged run (stage 4) is.

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

### The value features (stage 3)

Measured on 2026-11-16, the same Release arena and machine, **load average 33 to 100 from the other workers of the loop** (it fell
and rose between runs; a repeat of each number is given, and the cost of a depth is read by orders of magnitude, not by the
decimals). `--others normal --first 4000 --seeds 500 --jobs 0 --time`, one focus player a run (2000 hands, 500 seeds, 4 rotations);
the value weights are 4, `weightedValueDepth` is the `d` of the row:

| focus | wall time (two runs) | hands a second | us a decision, the focus seat |
|---|---|---|---|
| `normal` | 0.66 s, 0.58 s | 3046, 3438 | 97, 86 |
| `normal:weighted=1` | 0.74 s, 0.75 s | 2708, 2664 | 172, 173 |
| `wValueDiscard=4`, d 1 | 2.27 s, 1.99 s | 882, 1003 | 1076, 955 |
| `wValueDiscard=4`, d 2 | 15.71 s, 13.44 s | 127, 149 | 9302, 7930 |
| `wValueClaim=4`, d 1 | 0.98 s, 0.98 s | 2041, 2038 | 301, 306 |
| `wValueClaim=4`, d 2 | 2.64 s, 2.42 s | 759, 828 | 1323, 1211 |
| both at 4, d 1 | 2.33 s, 2.11 s | 860, 948 | 1134, 1033 |
| both at 4, d 2 | 18.59 s, 13.99 s | 108, 143 | 11125, 8323 |

(The table sums the threads' microseconds and `--time` reads a clock around every decision.) The discard value dominates: it rates
up to `kOwnDrawsMaxCandidates` kinds a turn, the claim value once a window. With the discard value a decision is about six
times the nine-weight player's at depth 1 and fifty to sixty times at depth 2. The paired rows against Normal on the same seeds are read only to see that the value
weights change the play (they do: the rows differ from Normal's, and the points a hand are within their intervals' noise); they are
**not a result**: for both at 4, depth 1, +3.02 +- 4.98 points a hand and -0.85 +- 1.42 wins %; at depth 2 +2.86 +- 4.92 and -0.80 +- 1.41.

**What a generation costs.** One generation of the proposed shape (16 candidates and the mean as 17 focus players, 200 seeds, the
mean as the three other seats, then the mean's monitoring call on 250 seeds against Normal) was sized from three focus players with
the value weights (4 and 4, 3 and 5, 5 and 3) against three seats of the first, 200 seeds from 300000, times 17/3, plus the
monitoring call of one focus player on 250 seeds:

| depth | 3 focus players, 200 seeds | monitoring, 250 seeds | a generation (estimate) | load |
|---|---|---|---|---|
| 1 | 10.97 s, 8.57 s | 1.18 s, 1.16 s | 63 s, 50 s | 33, 32 |
| 2 | 130.2 s, 138.3 s | 16.9 s, 11.2 s | 755 s, 795 s | 70, 76 |

**`kWeightedValueDepth` is 1.** The rule, written before these numbers: the largest depth in {1, 2} for which a generation of this
shape is estimated at 60 s or less, else 1. Depth 2 is more than ten times over; depth 1 is at the edge (50 to 63 s on a loaded
machine) and is the default whatever the rule says. The constant stays 1 (the knob was added with that value), so the tests that read it are unchanged.
The generation of the tuner is measured again in the probe of the budget (stage 3, before the run).

## Not done by this stage

The judged run, any named constant and any change of a level (stage 4). `judge`, the verdict of the final weights above against
the decision and confirmation ranges, does not exist yet; `kTunedProfile` and the arena player `tuned` are not added.
