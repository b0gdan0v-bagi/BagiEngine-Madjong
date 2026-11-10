# The search player

The entry `mahjong-bot-search` built a computer player that looks ahead by playing out sampled hands, and measured it head to head
against Normal with the bot arena (`docs/bot-arena.md`). This page is the record: the design in a page, the budget and how it was
found, the numbers, the decision rule applied to them, and the verdict. The code is `src/Mahjong/Game/ComputerSearch.{h,cpp}` (the
chooser), `ComputerWorld.{h,cpp}` (the sample of a hand) and `HandModel::Capture` / `Restore` (a hand rebuilt from plain data); the
search is a set of knobs of `LevelProfile` (`searchWorlds`, `searchCandidates`, `searchMargin`) that the arena can set
(`normal:searchWorlds=2`), off in every level that ships.

**VERDICT: no switch**

The search player does not beat Normal. At the budget the time rule allows it is slightly worse (-3 points a hand, -1.5 points of
wins, wins significantly below Normal's on both ranges); with 4 times and 16 times the worlds it is level with Normal, within
intervals that are wide. Section "The rule applied" has the arithmetic, section "What the numbers do and do not show" the limits.

## The design

**What is searched.** A decision is "which of a few candidate actions". The candidates are made by the Normal choosers
(`ChooseTurnAction`, `ChooseClaimAction`): Normal's own choice is always one of them (the baseline), plus at most
`searchCandidates - 1` others: the other discards with a shanten at most one above the best, a listed concealed or upgraded kong,
and in a claim window `Pass` and every listed pung, kong and chow. Never searched, answered as Normal does at once: `DeclareWin`, a
mahjong claim, the pass of a waiting hand (claim rule 2), a window with only `Pass`, and any decision with a single candidate. When
Normal declares the calling hand the candidates are the declaring discards only (a declaration is never worse, and a win needs it).

**Sampling (determinisation).** From the `TableView` alone, never from the `HandModel` of the table, so there is no peeking by
construction: the unseen copies of every kind (`UnseenCopies`) are a pool; each other seat gets `13 - 3 * (open melds + concealed
kongs)` concealed tiles drawn from it, and the rest is dealt in random order to the wall (the live count of the view, the loose
reserve, the dead wall). A declared opponent is drawn again, up to `kWorldDeclaredTries` (400) times, until its 13 tiles wait. The
flowers are inferred from the size of the pool. The world is a real `HandModel` (`HandModel::Restore`), and the test of the sampler
is that `MakeTableView(world, seat)` equals the view it came from field by field.

The approximations of the sample, from the header of `ComputerWorld.h`: the order and the discarders of the pile are drawn at random;
a declared opponent's hand is any hand that waits, not the one it really holds, and with fewer than three melds the sample often
does not wait at all (about half of the samples with two melds, almost never with fewer); what earlier seats passed in a claim
window is not in the view, so they decide again in the rollout; a declared opponent is taken to have declared on its second
discard, never its first (the double for a declaration on the first discard is never given to it); the move log is empty.

**Rollout and value.** The candidate is applied in the sampled hand, then four Normal players (`kNormalProfile`, seeds derived from
the sample) play the hand to its end, the acting seat too. The value is the **points of the seat** (`deltas[seat]` of `SettleHand`),
not the win rate: points are what the match is scored by, they count the doubles, the limit hands and the double payments of East,
and Normal already maximises the speed to a win, so a win-rate objective would mostly copy Normal. The cost of points is variance,
which is cut by **common random numbers**: every candidate is played on the same worlds with the same rollout seeds, and the choice
uses the paired difference to the baseline. Normal's choice is replaced only when the best candidate's mean advantage is above
`searchMargin` tenths of the standard error of that paired difference.

**Randomness.** A search decision draws exactly one number from the player's own seeded rng; the worlds and the rollouts derive from
it by a fixed mixing, so one seed is one match. A player with the search off draws nothing (Normal and Easy stay byte-identical, the
golden of `ComputerLevelTest`).

## The budget

A decision costs at most `searchWorlds` x `searchCandidates` rollouts (a rollout is a whole hand by four Normal players; only the
candidates that exist are played, and many decisions have fewer than `searchCandidates`). The shipped
constants are in `ComputerLevel.h` and used by no level:

```
kSearchWorlds     = 2
kSearchCandidates = 4
kSearchMargin     = 30      (tenths of a standard error)
```

**Worlds and candidates: the time rule.** The rule fixed before the measurement: the slowest decision (the first discard of 14
tiles with the full candidate count) takes at most 0.3 s in a Debug build, because the step of a computer blocks the frame and
`kComputerStepSeconds` is 1.0 s. A budget is a count of rollouts, never a clock, so a seed stays one match. Measured with a
Debug `BagiMahjongArena` at `--jobs 1` (summed thread time over-counts, see `docs/bot-arena.md`), `--others normal`, seeds 4000 ..
4015 (margin 0: the cost of a decision is the same at any margin; 727 search decisions for 2 x 4, 724, 710, 706 and 723 for the
other shapes of the table, so the means are of slightly different samples), each decision timed with a temporary stderr print (not committed) in two runs, and the table gives the
smaller of the two times of each decision (the machine was shared with other sessions, so a single run has contention spikes):

| worlds x candidates | worst decision | mean decision |
|---|---|---|
| **2 x 4 (shipped)** | **248 ms** | 67 ms |
| 3 x 3 | 319 ms | 77 ms |
| 4 x 2 | 314 ms | 76 ms |
| 5 x 2 | 335 ms | 96 ms |
| 3 x 2 | 471 ms | 76 ms |

On 4 seeds (one run each) 4 x 4, 6 x 4 and 8 x 4 had worst decisions of 469, 653 and 757 ms, and 2 x 5 one decision of 1.1 s.
The 14-tile turns are the slowest: 2 x 4 has a mean of 108 ms there (275 decisions). The raw single-run worst of 2 x 4 was 686 ms in
the first run (machine loaded) and 298 ms in the second.

So the rule allows 8 rollouts a decision. **248 ms is the worst observed on this sample, not a bound:** a rollout that runs to the
end of the wall costs about 110 ms in Debug, and eight of them in one decision would be about 0.9 s; the sample of 727 decisions
had none such. The cost of sampling a declared opponent (about 3 ms a world in Debug at 400 tries) is not what dominates, the
rollouts are, so `kWorldDeclaredTries` was not changed.

In Release a search decision of the shipped budget costs **9.87 ms** on average (12 seeds, `--jobs 1`, 555 decisions) against
**74 us** for a decision of a Normal player in the same run, about 130 times; Debug to Release is about 7 times.

**The margin** is not set by time. It was looked at on the exploration range, seeds 4000 .. 4999, 1000 seeds, `--others normal`,
Release (paired difference to Normal; the range is free to look at, and nothing was tuned on the decision and confirmation
ranges):

| worlds x candidates | margin | points/hand | wins % | declared % |
|---|---|---|---|---|
| 2 x 4 | 0 | -34.73 +- 3.32 | -13.25 +- 0.95 | 29.7 (Normal 55.8) |
| 2 x 4 | 10 | -6.79 +- 3.90 | -3.88 +- 1.07 | 48.4 |
| 2 x 4 | 20 | -3.02 +- 3.37 | -1.95 +- 0.96 | 51.9 |
| **2 x 4** | **30** | **-2.79 +- 3.09** | **-1.68 +- 0.93** | 52.8 |
| 2 x 4 | 50 | -3.15 +- 3.10 | -1.48 +- 0.89 | 53.4 |
| 2 x 4 | 100 | -2.06 +- 2.93 | -1.10 +- 0.87 | 54.6 |
| 3 x 2 | 0 | -15.59 +- 4.19 | -6.28 +- 1.11 | 43.0 |
| 3 x 2 | 10 | -5.80 +- 3.42 | -2.35 +- 1.10 | 50.9 |
| 2 x 3 | 10 | -7.03 +- 3.57 | -2.83 +- 1.04 | 50.0 |

Every row is below Normal. A margin of 0 (the best mean wins) is much worse: two worlds are mostly noise, and following the noise
drops the declarations from 56 to 30 percent. From 20 to 100 the rows are within each other's noise, and the closer the margin
gets to "never override", the closer the player gets to Normal. Margin 30, the first of that plateau, was fixed; it was chosen
before the decision range was played, and a larger margin would only have moved the player towards Normal.

## The head-to-head numbers

Release, `BagiMahjongArena --jobs 14`, `--focus normal,normal:searchWorlds=<S>,searchCandidates=4,searchMargin=30 --others normal`,
so the paired row is the search player against Normal on the same walls (4000 hands per player for 1000 seeds, 1000 for 250). Each
run was played once, as it stands, with the knobs and the budget fixed before. The wall time is that of the run on a 14-core
machine that was shared with other sessions. The full reports are the working files of the entry, not committed; every number
below is from them.

| range | worlds | seeds | points/hand (paired) | wins % (paired) | wall time |
|---|---|---|---|---|---|
| decision, `--first 30000` | 2 (1x) | 1000 | -3.07 +- 3.28 | -1.45 +- 0.95, significant | 45 s |
| confirmation, `--first 60000` | 2 (1x) | 1000 | -3.38 +- 3.44 | -1.55 +- 0.99, significant | 51 s |
| decision | 8 (4x) | 1000 | +0.56 +- 3.04 | +0.07 +- 0.86 | 181 s |
| confirmation | 8 (4x) | 1000 | -0.62 +- 2.77 | -0.23 +- 0.85 | 220 s |
| decision | 32 (16x) | 250 | -2.37 +- 6.31 | +0.20 +- 1.78 | 196 s |
| confirmation | 32 (16x) | 250 | +5.15 +- 5.78 | +0.50 +- 1.65 | 175 s |

Wins of the search player in its 4000 hands: 23.52 % (decision) and 23.43 % (confirmation) against Normal's 24.98 % (a fair share is
25); with 8 worlds 25.05 % and 24.75 %; with 32 worlds 25.10 % and 25.40 % (on 1000 hands each, interval about +-1.7). The
declared share of the search player at 1x was 52.75 % and 54.45 % against Normal's 54.50 % and 57.23 %.

**Against three random seats** (`--others random`, `--first 30000 --seeds 250`, 1x, 11 s), the condition that the search is not
worse than Normal there:

| player | wins % | points/hand |
|---|---|---|
| normal | 81.40 +- 2.51 | 158.38 +- 11.21 |
| search 2 x 4, margin 30 | 79.60 +- 2.57 | 149.16 +- 9.69 |
| paired difference | -1.80 +- 2.51 | -9.22 +- 11.47 |

Neither paired difference is significant.

## The rule applied

The rule was fixed in the plan of the entry before any number was seen. **Switch** needs, on the shipped budget: (1) on both the
decision range and the confirmation range the paired difference of points a hand and of wins % significant and positive; (2) pooled
over the two ranges at least +5 points a hand and +1.5 points of wins; (3) not significantly worse than Normal against three random
seats; (4) the Debug time rule. **Marginal** is significant on both ranges but under the pooled numbers; anything else is no switch.
The 4x and 16x runs are for the record, to say whether compute was the limit; they do not enter the rule.

1. Condition (1) fails. At the shipped worlds the points are not significant on either range (-3.07 +- 3.28 and -3.38 +- 3.44
   include 0) and the wins are significant but **negative** on both (-1.45 +- 0.95 and -1.55 +- 0.99).
2. Condition (2) fails. The pooled difference over the two ranges (the mean of the two, 1000 seeds each) is -3.2 points a hand and
   -1.5 points of wins, far from +5 and +1.5. (Pooled intervals are computed from the two rows, not an arena output:
   about +-2.4 points and +-0.7 wins, so the pooled loss is itself outside 0.)
3. Condition (3) holds: -9.22 +- 11.47 points and -1.80 +- 2.51 wins against three random seats, neither significant.
4. Condition (4) holds on the observed worst of 248 ms (the smaller of two runs of each of 727 decisions), with the caveat above.
5. "Marginal" needs a significant positive result on both ranges; there is none. So the outcome is **no switch**, not marginal.

No range was replayed with other knobs, and no range or budget was chosen after a result.

## What the numbers do and do not show

- At the budget the time rule allows, search is **a little worse than Normal**, and the wins are significantly so on both ranges.
  This is not noise: the exploration range shows the same sign for every margin and every shape tried.
- **More compute does not show a gain at the intervals reached.** At 4 times the worlds the pooled paired difference is about
  0.0 +- 2.1 points and -0.1 +- 0.6 wins; the +5 points of the rule is far outside it. At 16 times, on 250 seeds a range, the
  two ranges disagree (-2.37 and +5.15 points) and the pooled difference is about +1.4 +- 4.3 points and +0.4 +- 1.2 wins (computed
  from the two rows, not an arena output). That interval **cannot exclude** a gain of up to about +5 points a hand or +1.5 wins, the size the rule asks, nor a
  loss of several points: 500 seeds are too few to tell. Halving that interval takes four times the seeds; at 16 times
  the worlds a run costs about 200 s per 250 seeds on 14 cores, about 13 minutes per 1000 seeds.
- The one trend in the data is the one to expect from the margin: with few worlds the player is Normal plus noise, and more worlds
  bring it back from -3 to about 0. It is not seen above 0.
- Why is not shown by these runs. The probable reasons, which this entry did not test: Normal's lowest-shanten-then-acceptance
  play is already near a local optimum for speed in these rules (the win base is 20 and most wins score 20 to 39, the earlier
  Hard-lever measurements found the same), the rollout policy is Normal itself, so a rollout can only prefer what Normal would
  have done later, and the sample of the opponents is wrong where it matters (the hands of declared opponents, the pile order).
- The cost is real: 9.87 ms against 74 us a decision in Release, and 248 ms worst in Debug at 8 rollouts, which is the whole time
  budget of a step. A stronger search at a budget a frame can afford is not in sight on this evidence.

**What would make search win** (for the owner, not built): a rollout policy or an evaluation that differs from the policy being
improved (the search over Normal plays Normal against itself); a sample of the opponents' hands that knows the waits of declared
hands exactly; a win-rate objective compared with the points one; and many more rollouts, which is only possible off the frame
(a worker thread) or with a faster rollout. Each of these is a separate entry; none was tried here.

## Reproducing

```bash
cmake --preset macos-release && cmake --build --preset macos-release --target BagiMahjongArena
A=build/ClangmacOS/BagiMahjongArena
$A --first 30000 --seeds 1000 --jobs 14 --others normal \
   --focus normal,normal:searchWorlds=2,searchCandidates=4,searchMargin=30         # decision range (45 s)
$A --first 60000 --seeds 1000 --jobs 14 --others normal --focus normal,normal:searchWorlds=2,searchCandidates=4,searchMargin=30
$A --first 30000 --seeds 250  --jobs 14 --others random --focus normal,normal:searchWorlds=2,searchCandidates=4,searchMargin=30
```

`searchWorlds=8` and `searchWorlds=32` are 4 times and 16 times (the 32-world runs used `--seeds 250` on both ranges). A Release build overwrites the Debug executables
(`docs/bot-arena.md`). The Debug time rule needs a Debug arena at `--jobs 1`.

## What was decided

Outcome B of the plan: **no `Hard` level.** `ComputerLevel` stays Easy and Normal, the match log and the control API are unchanged
(a level `hard` is still refused). The search stays in the tree as the three knobs of `LevelProfile` (`searchWorlds`,
`searchCandidates`, `searchMargin`), off in every shipped level, which the arena can set from the command line
(`normal:searchWorlds=8`) for the next experiment. `kSearchWorlds`, `kSearchCandidates` and `kSearchMargin` (`ComputerLevel.h`) are
the budget that fitted the time rule and are used by no level. The Hint advice stays Normal. A search seat plays a whole 4-hand match
legally in `ComputerMatchTest`, so the code is kept working. The ideas that might make it win are listed under "What would make
search win" above; none is built.

## Levels

The entry `mahjong-search-levels` fills the scale 1..10 (`ComputerLevel.h`) and measures it. This section is its record, added as the
work goes; the verdict above is the first entry's and stays as history. Rules fixed before any number was seen: the opponents are
three Normal seats (`--others 5`); the **knobs are tuned only on the exploration range** (`--first 4000`, 1000 seeds); the decision
range is `--first 100000` and the confirmation range `--first 130000` (1000 seeds each, played once as they stand); a level is
"stronger than the level below" only when, on **both** the decision and the confirmation range, the paired difference of points a
hand and of wins % is significant and positive and the pooled (mean of the two) difference is positive; anything else is reported as
"not significantly above". All runs below are Release, `--jobs 14`, `A=build/ClangmacOS/BagiMahjongArena`, on a machine shared with
other sessions (the wall times are indicative).

### Levels 1..5 against three Normal seats (step 1: the scale today)

```bash
$A --first 4000   --seeds 1000 --jobs 14 --others 5 --focus 1,2,3,4,5      # exploration (6.3 s)
$A --first 100000 --seeds 1000 --jobs 14 --others 5 --focus 1,2,3,4,5      # decision (5.6 s)
$A --first <r> --seeds 1000 --jobs 14 --others 5 --focus 1,2               # the adjacent pairs, also 2,3  3,4  4,5
```

Wins % and points a hand with the 95 % interval (4000 hands per level); the interval of Normal is 0 on points by the mirror identity
(on the decision range one of the 1000 walls was drawn for all four rotations, so its wins are 24.98 %, not 25.00):

| level | exploration wins % | exploration points/hand | decision wins % | decision points/hand |
|---|---|---|---|---|
| 1 | 7.33 +- 0.78 | -47.92 +- 2.85 | 7.17 +- 0.75 | -47.47 +- 3.10 |
| 2 | 13.47 +- 0.95 | -31.52 +- 3.37 | 13.68 +- 0.94 | -31.59 +- 3.22 |
| 3 | 17.45 +- 1.02 | -19.92 +- 3.46 | 18.57 +- 1.05 | -17.26 +- 3.60 |
| 4 | 21.95 +- 1.02 | -8.24 +- 3.51 | 21.77 +- 1.04 | -8.99 +- 3.69 |
| 5 (Normal) | 25.00 +- 0.00 | 0.00 +- 0.00 | 24.98 +- 0.05 | 0.00 +- 0.00 |

The numbers the entry quoted (6.7 / 12.8 / 17.8 / 23.3 / 25 %) were 300 seeds; on 1000 seeds the wins are 7.3 / 13.5 / 17.5 / 22.0 /
25.0 on the exploration range and 7.2 / 13.7 / 18.6 / 21.8 / 25.0 on the decision range. The paired difference of each level to the one
below (`--focus <k>,<k+1>`, same walls; every row significant):

| step | exploration points/hand | exploration wins % | decision points/hand | decision wins % |
|---|---|---|---|---|
| 1 to 2 | +16.39 +- 3.01 | +6.15 +- 0.90 | +15.88 +- 3.35 | +6.50 +- 0.91 |
| 2 to 3 | +11.60 +- 3.20 | +3.98 +- 0.99 | +14.33 +- 3.16 | +4.90 +- 0.98 |
| 3 to 4 | +11.68 +- 3.49 | +4.50 +- 1.03 | +8.26 +- 3.13 | +3.20 +- 0.94 |
| 4 to 5 | +8.24 +- 3.51 | +3.05 +- 1.02 | +8.99 +- 3.69 | +3.20 +- 1.04 |

Each step is significant on both ranges (the confirmation range is not played for levels 1..5 in this step: WP 4 plays the shipped
scale on it). The steps shrink towards Normal (16, 12-14, 8-12, 8-9 points). The skip-knob levels differ from Normal visibly: at
level 1 declared hands are 18 % of the hands against 56 % of Normal (the declaration skip is the biggest knob), 32 % at level 2, 40-42
% at level 3, 47-50 % at level 4. Their drawn share is 0.1 to 0.6 %, like Normal's.

### The search as a weakening of Normal (step 2: exploration only)

`5:searchWorlds=W,searchCandidates=4,searchMargin=M` for W in {2, 4, 8} and M in {0, 10, 30}: the search of
`docs/bot-search.md` ("The design") with the Normal rollout, played against three Normal seats, each row paired with Normal (the
first focus player, same walls). Candidates are 4 as in `kSearchCandidates`. Exploration range, 1000 seeds, the three margins of a
shape in one run:

```bash
for W in 2 4 8; do
  $A --first 4000 --seeds 1000 --jobs 14 --others 5 --focus \
     5,5:searchWorlds=$W,searchCandidates=4,searchMargin=0,5:searchWorlds=$W,searchCandidates=4,searchMargin=10,5:searchWorlds=$W,searchCandidates=4,searchMargin=30
done                                                                       # 180 s, 340 s, 586 s of wall time
```

| worlds | margin | wins % | points/hand | declared % | drawn % | avg win score | paired points/hand | paired wins % |
|---|---|---|---|---|---|---|---|---|
| 2 | 0 | 11.75 +- 0.95 | -34.73 +- 3.32 | 29.68 | 0.38 | 45.62 | -34.73 +- 3.32 sig. | -13.25 +- 0.95 sig. |
| 2 | 10 | 21.12 +- 1.07 | -6.79 +- 3.90 | 48.40 | 0.28 | 47.18 | -6.79 +- 3.90 sig. | -3.88 +- 1.07 sig. |
| 2 | 30 | 23.32 +- 0.93 | -2.79 +- 3.09 | 52.83 | 0.05 | 45.76 | -2.79 +- 3.09 | -1.68 +- 0.93 sig. |
| 4 | 0 | 11.95 +- 0.96 | -31.51 +- 4.08 | 28.23 | 0.47 | 50.13 | -31.51 +- 4.08 sig. | -13.05 +- 0.96 sig. |
| 4 | 10 | 20.68 +- 1.12 | -10.25 +- 3.71 | 45.48 | 0.25 | 45.83 | -10.25 +- 3.71 sig. | -4.33 +- 1.12 sig. |
| 4 | 30 | 24.15 +- 0.84 | -1.70 +- 2.82 | 55.12 | 0.10 | 45.03 | -1.70 +- 2.82 | -0.85 +- 0.84 sig. |
| 8 | 0 | 12.20 +- 1.00 | -30.00 +- 3.92 | 29.90 | 0.20 | 51.11 | -30.00 +- 3.92 sig. | -12.80 +- 1.00 sig. |
| 8 | 10 | 19.52 +- 1.09 | -11.57 +- 3.77 | 45.55 | 0.10 | 46.13 | -11.57 +- 3.77 sig. | -5.47 +- 1.09 sig. |
| 8 | 30 | 24.30 +- 0.84 | -1.63 +- 2.66 | 55.38 | 0.07 | 45.22 | -1.63 +- 2.66 | -0.70 +- 0.84 |

(Normal: 25.00 %, 0 points, 55.77 % declared, 45.46 average win score.) The 2 x 4 rows are those of the table "The margin" above
(the same command line and range, so the same bytes); all nine variants are listed, none was left out. Nine rows are nine tries on
one range, so one marginal "significant" in them is not news; none is read alone.

Reading, for the ladder (not yet a decision):

- **The margin is the dial; the worlds are not.** At margin 0 more worlds move the points from -34.7 to -30.0 and the wins not at
  all (-13.3, -13.1, -12.8); at 10 more worlds do not help, the points drift down (-6.8, -10.3, -11.6, within each other's intervals). Only at margin 30 do more worlds
  bring the player to Normal (-2.8, -1.7, -1.6 points), which is "mostly overrides nothing". So the strength of a search player of
  this kind is set by how often it overrides Normal at all, and the extra worlds only cost time.
- **Strength against the skip levels** (wins % and points, exploration): margin 0 (12 %, -30 to -35) lies at level 2 (13.5 %, -31.5);
  margin 10 (19.5 to 21.1 %, -6.8 to -11.6) lies at level 4 (22.0 %, -8.2), above level 3 (17.5 %, -19.9); margin 30 (23.3 to 24.3
  %, -1.6 to -2.8) lies between level 4 and Normal, and its paired difference to Normal is not significant on points at any W. No
  variant lies at level 3, or between 1 and 2; margin 5 or 20 were not tried (the dial between 0 and 10 and between 10 and 30 is
  untried, by the rule that nothing is added after a result).
- **Not the same kind of weakness.** The skip levels get weaker by skipped declarations, passed claims and a worse tile
  (`declared %` 18 / 32 / 40 / 47 against 56). The search variants at margin 0 also declare rarely (28 to 30 %, like level 2) and win
  with a higher average score at 4 and 8 worlds (50 to 51 against 45): the noise drops declarations too, so a person who counts
  declarations sees the same thing as at level 2. At margin 10 and 30 the declared share is 45 to 55 %, close to Normal's, and the
  weakness is in the discard and claim choices, which are not visible as a missing declaration: these two margins differ in kind
  from the skip knobs, and are the candidates for a level 4 (margin 10) and an extra step between 4 and 5 (margin 30, whose
  paired points interval includes 0 so its order against Normal is not established by this range).
- **The time rule decides the shape.** Only 2 x 4 (8 rollouts a decision) fits the 0.3 s Debug rule of "The budget" (248 ms); 4 x 4
  and 8 x 4 are 469 and 757 ms there. 4 and 8 worlds are for the record only, and the 2 x 4 rows are the only candidates for a
  level: M=10 for a level 4 (about its strength), M=30 for a step between 4 and 5, M=0 as a level 2 whose weakness is not a coin
  but is as visible as level 2's in declarations. Whether any of them is a better ladder than the skip levels (smoother, each
  step significant) is not known from this range: WP 3 decides, and every step it uses is measured by WP 4 on the decision and
  confirmation ranges.

### Above Normal (step 3: is there a search profile inside the time rule that beats Normal?)

The question of the top of the scale (levels 6..10). Fixed before any number was seen: shapes worlds x candidates in {2 x 4, 4 x 2, 3 x 3} (at most 12 rollouts a decision), margin in {30, 60, 100}, nine variants against three Normal seats on the exploration range, each paired with Normal; the Debug time of each shape (a shape whose slowest decision is over 0.3 s is out); the best one or two by the paired points a hand go to the decision range; the confirmation range is played only if the decision range is significantly positive on points **and** wins. Every variant is listed, none was left out.

```bash
for S in "2 4" "4 2" "3 3"; do set -- $S                        # W=$1, C=$2
  $A --first 4000 --seeds 1000 --jobs 14 --others 5 --focus \
     5,5:searchWorlds=$1,searchCandidates=$2,searchMargin=30,5:searchWorlds=$1,searchCandidates=$2,searchMargin=60,5:searchWorlds=$1,searchCandidates=$2,searchMargin=100
done                                                            # 177 s, 190 s, 183 s of wall time
```

Exploration range, 1000 seeds (Normal: 25.00 %, 0 points, 55.77 % declared, 45.46 average win score); "sig." is the arena's `significant`:

| worlds x cand. | margin | wins % | points/hand | declared % | paired points/hand | paired wins % |
|---|---|---|---|---|---|---|
| 2 x 4 | 30 | 23.32 +- 0.93 | -2.79 +- 3.09 | 52.83 | -2.79 +- 3.09 | -1.68 +- 0.93 sig. |
| 2 x 4 | 60 | 23.52 +- 0.88 | -3.29 +- 3.07 | 53.60 | -3.29 +- 3.07 sig. | -1.48 +- 0.88 sig. |
| 2 x 4 | 100 | 23.90 +- 0.87 | -2.06 +- 2.93 | 54.60 | -2.06 +- 2.93 | -1.10 +- 0.87 sig. |
| 4 x 2 | 30 | 24.35 +- 0.83 | -1.51 +- 2.73 | 55.12 | -1.51 +- 2.73 | -0.65 +- 0.83 |
| 4 x 2 | 60 | 24.32 +- 0.81 | -1.43 +- 2.58 | 55.27 | -1.43 +- 2.58 | -0.68 +- 0.81 |
| 4 x 2 | 100 | 24.35 +- 0.81 | -1.38 +- 2.58 | 55.35 | -1.38 +- 2.58 | -0.65 +- 0.81 |
| 3 x 3 | 30 | 24.32 +- 0.85 | -0.85 +- 2.89 | 54.85 | -0.85 +- 2.89 | -0.68 +- 0.85 |
| 3 x 3 | 60 | 24.38 +- 0.82 | -0.94 +- 2.66 | 55.23 | -0.94 +- 2.66 | -0.62 +- 0.82 |
| 3 x 3 | 100 | 24.32 +- 0.81 | -1.06 +- 2.66 | 55.30 | -1.06 +- 2.66 | -0.68 +- 0.81 |

All nine are below Normal on points and on wins; none is above 0 even by its sign. The 2 x 4 / 30 row is the same bytes as in WP 1's table. The margin does not matter from 30 on (the rows of a shape are within each other's noise; they only move towards Normal's declared share); the shape matters a little (4 x 2 and 3 x 3 are closer to Normal than 2 x 4, whose wins are significantly below).

**Debug time of the shapes** (Debug arena at `--jobs 1 --time`, `--others 5`, seeds 4000 .. 4015, margin 30, each decision timed with a temporary stderr print that was not committed; two runs, the smaller time of each decision, as in "The budget"; the machine was otherwise quiet):

```bash
$A --first 4000 --seeds 16 --jobs 1 --time --others 5 --focus 5:searchWorlds=<W>,searchCandidates=<C>,searchMargin=30     # twice
```

| worlds x candidates | decisions | worst decision | mean decision |
|---|---|---|---|
| 2 x 4 | 726 | 237 ms | 65 ms |
| 4 x 2 | 704 | 246 ms | 74 ms |
| 3 x 3 | 710 | 261 ms | 75 ms |

All three are under 0.3 s on this sample, so none is out by the rule as written. (The single raw worst of a run was 238 / 300 ms for 2 x 4 and 285 / 274 and 267 / 261 ms for the others; "The budget" measured 314 and 319 ms for 4 x 2 and 3 x 3 under a loaded machine and with margin 0. 261 ms is a worst observed, not a bound: see "The budget".)

**The best two by the paired points** are 3 x 3 / 30 (-0.85) and 3 x 3 / 60 (-0.94). They, and for the record 8 worlds x 4 candidates / 30 (over the time rule: 469 to 757 ms in Debug, "The budget"), were played once on the decision range, 1000 seeds:

```bash
$A --first 100000 --seeds 1000 --jobs 14 --others 5 --focus \
   5,5:searchWorlds=3,searchCandidates=3,searchMargin=30,5:searchWorlds=3,searchCandidates=3,searchMargin=60,5:searchWorlds=8,searchCandidates=4,searchMargin=30     # 376 s
```

| variant | wins % | points/hand | paired points/hand | paired wins % |
|---|---|---|---|---|
| 3 x 3 / 30 | 25.15 +- 0.89 | 2.01 +- 2.92 | +2.01 +- 2.92 | +0.17 +- 0.89 |
| 3 x 3 / 60 | 25.15 +- 0.85 | 1.15 +- 2.86 | +1.15 +- 2.86 | +0.17 +- 0.86 |
| 8 x 4 / 30 (record, over the time rule) | 24.98 +- 0.86 | 1.08 +- 2.93 | +1.08 +- 2.93 | +0.00 +- 0.87 |

(Normal on this range: 24.98 % wins, 0 points.)

**Verdict: no search profile inside the time rule is stronger than Normal.** Neither of the two on the decision range is significant on points or on wins, so by the rule written first the confirmation range is not played (a result that is not positive on the decision range cannot pass "both ranges"). The decision-range signs are positive, the exploration-range signs of the same variants were negative; the mean of the two ranges is +0.6 points (3 x 3 / 30) and +0.1 (3 x 3 / 60), with intervals of about +-2: this is Normal's play with noise, which the first entry found. The 8 x 4 record row says the same: 2.7 times the worlds and a third more candidates than 3 x 3 (32 rollouts against 9, about 3.6 times) give +1.08 +- 2.93 and 0.00 wins, so compute inside a reasonable budget (a few times the rollouts) is not what limits the search (the rollout policy is Normal itself: "What the numbers do and do not show"). Nothing the knobs cannot say was needed; no code was changed. Consequence for the plan: levels 6..10 have no stronger profile to take, and the proposal for the top of the scale is the owner's ("Left for the owner" of the plan: leave 6..10 as Normal, re-map so that Normal is 10, or the background-search entry).

### The profiles in the code (step 4: what `ComputerLevel.h` holds)

Decided by the rule written before the numbers (plan, WP 3), from the two sections above and nothing newly measured:

- **Level 5 is `kNormalProfile`**, unchanged (the golden scores of `ComputerLevelTest`, seeds 700..703, did not move).
- **Levels 6..10 stay `kNormalProfile`.** "Above Normal" found no search profile inside the time rule that is stronger than Normal, so no
  stronger profile is invented; they are placeholders (the owner's options for the top of the scale are in the plan's "Left for the
  owner").
- **Levels 1..4 stay the skip-knob profiles** (`kEasyProfile`, `kLevel2Profile`..`kLevel4Profile`). The rule re-cuts them from the
  search dials only if a search-weakening variant is a clearly better ladder: smoother steps, or a weakness a person cannot see through.
  Neither holds. (a) The steps: the skip ladder steps by about 16 / 12-14 / 8-12 / 8-9 points a hand, each significant on both ranges;
  the search dial is the margin (the worlds change nothing, "The search as a weakening of Normal"), and along it the strength is
  -34.7 (margin 0), -6.8 (10), -2.8 (30) points: a cliff and then a plateau, with nothing at level 3 and nothing between 0 and 10
  (margins 5 and 20 were not in the grid, and by the rule nothing is added after a result). The margin 30 variant is not even
  significantly below Normal on points. (b) The invisibility: margin 0 declares 28-30 % of the hands like level 2 (visible in the
  same way), and margin 10 declares 45-48 % like level 4; only a ladder that is not steep could use the margins that declare like
  Normal, and those lie within the noise of Normal. (c) The cost: a search seat spends about 65 ms a decision in Debug (a decision of
  8 rollouts), the skip levels nothing, and the browser build is not measured. So no profile is re-cut and no number of 2..4 was
  retuned.
- **No level searches**, so there is no search level whose Debug decision time the rule would bound; the time rule stays what bounds
  a future profile (`searchWorlds x searchCandidates` of at most 12 rollouts, the slowest decision at most 0.3 s: 237 to 261 ms for
  the three shapes of "Above Normal"). `ComputerSearchTest` pins that no level searches, and `ComputerLevelTest` that the skip
  profiles get weaker strictly along 1..4 and levels 5..10 are Normal's.

### The whole scale measured (step 5: the shipped table 1..10)

The table of `ComputerLevel.h` as it ships (WP 3: levels 1..4 skip knobs, 5..10 `kNormalProfile`), against three Normal seats
(`--others 5`), 1000 seeds on each of two ranges that nothing was tuned on, each played once as it stands. Release,
`--jobs 14`, `A=build/ClangmacOS/BagiMahjongArena`:

```bash
for R in 100000 130000; do                                                  # decision, confirmation
  $A --first $R --seeds 1000 --jobs 14 --others 5 --focus 1,2,3,4,5,6,7,8,9,10    # 13.7 s, 20.7 s of wall time
  for P in 1,2 2,3 3,4 4,5; do $A --first $R --seeds 1000 --jobs 14 --others 5 --focus $P; done   # the adjacent paired rows
  $A --first $R --seeds 1000 --jobs 14 --others 5 --focus 5,6,7,8,9,10      # 6..10 against 5
done
```

**Not significantly above the level below: levels 6, 7, 8, 9 and 10, each against the one below it (and each against 5).** They are
`kNormalProfile`, the same player as level 5, so on every wall they play the same hand to the same bytes and every paired difference
is exactly +0.00 +- 0.00 points and +0.00 +- 0.00 wins % on both ranges (the rows 5 to 10 of the first table below are one set of
numbers, measured once). By the rule they are not stronger; they are not meant to be: "Above Normal" found no search profile inside
the time rule that beats Normal, WP 3 shipped them as placeholders, and the retune round of the rule has nothing to retune (there is no
dial of a Normal-identical level; inventing a profile to make a step would be the fake "stronger" level the plan forbids). They go to
the owner ("Left for the owner" of the plan). Levels 2, 3, 4 and 5 are each significantly above the level below on both ranges; no
retune round was played and no code changed.

Wins % and points a hand with the 95 % interval (4000 hands per level). Level 5 against three level-5 seats is exactly 25 % and
0 points by the mirror identity; the 24.98 % and 24.95 % are the wins lost to the drawn hands (a drawn wall is drawn in all four rotations, so no seat wins it):

| level | decision wins % | decision points/hand | confirmation wins % | confirmation points/hand |
|---|---|---|---|---|
| 1 | 7.17 +- 0.75 | -47.47 +- 3.10 | 6.42 +- 0.73 | -52.87 +- 3.03 |
| 2 | 13.68 +- 0.94 | -31.59 +- 3.22 | 13.22 +- 0.97 | -33.54 +- 3.68 |
| 3 | 18.57 +- 1.05 | -17.26 +- 3.60 | 17.00 +- 1.00 | -22.05 +- 3.50 |
| 4 | 21.77 +- 1.04 | -8.99 +- 3.69 | 21.77 +- 1.06 | -8.56 +- 3.49 |
| 5 to 10 (Normal, one player) | 24.98 +- 0.05 | 0.00 +- 0.00 | 24.95 +- 0.07 | 0.00 +- 0.00 |

The paired difference of each level to the one below (`--focus <k>,<k+1>`, same walls), both ranges side by side and pooled (the
mean of the two):

| step | decision points/hand | decision wins % | confirmation points/hand | confirmation wins % | pooled points/hand | pooled wins % | verdict |
|---|---|---|---|---|---|---|---|
| 1 to 2 | +15.88 +- 3.35 | +6.50 +- 0.91 | +19.33 +- 3.41 | +6.80 +- 0.92 | +17.61 | +6.65 | above |
| 2 to 3 | +14.33 +- 3.16 | +4.90 +- 0.98 | +11.49 +- 3.74 | +3.77 +- 1.00 | +12.91 | +4.34 | above |
| 3 to 4 | +8.26 +- 3.13 | +3.20 +- 0.94 | +13.48 +- 3.30 | +4.78 +- 1.00 | +10.87 | +3.99 | above |
| 4 to 5 | +8.99 +- 3.69 | +3.20 +- 1.04 | +8.56 +- 3.49 | +3.17 +- 1.06 | +8.78 | +3.19 | above |
| 5 to 6 | +0.00 +- 0.00 | +0.00 +- 0.00 | +0.00 +- 0.00 | +0.00 +- 0.00 | 0 | 0 | not significantly above |
| 6 to 7, 7 to 8, 8 to 9, 9 to 10 | identical profiles: 0 and 0 as 5 to 6 (the five rows of `--focus 5,6,7,8,9,10` are all +0.00) | | | | 0 | 0 | not significantly above |

(Every "above" row is marked `significant` by the arena on points and on wins on both ranges, and the pooled values are positive.)
The steps shrink towards Normal as in step 1 (17.6, 12.9, 10.9, 8.8 points pooled; on the confirmation range 2 to 3 is 11.5 and 3 to 4
is 13.5, within each other's intervals), so the ladder 1 < 2 < 3 < 4 < 5 holds on three ranges (exploration of step 1, decision,
confirmation). The declared share runs 17 / 32 /
42 / 50 / 56 % on the decision range and 16 / 31 / 42 / 50 / 57 % on the confirmation range, the drawn share 0.1 to 0.6 % at every
level.

**Time.** There is no search level, so there is no Release decision time of a search level to record (the arena's `--time` is read at
`--jobs 1` for a search profile; the time rule and the Debug figures of the shapes that would fit it are in "Above Normal"). A run
of the whole table plays 5 distinct players; the wall time of one range at `--jobs 14` is above.
