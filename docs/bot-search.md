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
