# The bot arena

`BagiMahjongArena` is a command-line program that plays many hands of the computer players with no window and prints a table with
confidence intervals. It is the ruler for every change of a computer player (a level, a knob of `LevelProfile`, a stronger Hard, a
coach): "is it better, and by how much" is answered by a number with an interval, not by a feeling.

## What it is for

`ComputerStrengthTest` plays 240 hands, and a few hundred hands are what a gtest can afford in Debug. The noise of such a count is
large: the arena reads about 16 points a hand of half-width (the 95 percent interval) for Normal against three random seats on those
60 seeds, and about 4.7 on 1000 seeds, while the effects a lever of a computer player has are +1 to +4 points a hand (the Hard-lever
measurements of the entry `mahjong-computer-difficulty`: even an oracle that cheats gains only about that much against three Normal
seats). A Release build plays 1000 seeds (4000 hands for a focus player) in about 4 seconds, and the paired rows (below) make a
difference of a few points visible.

The program only measures. It reads `src/Mahjong/Game/` and changes nothing there; the numbers of a level are the owner's decision.

## Build and run

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"          # the reflection generator needs jinja2
cmake --preset macos-release
cmake --build --preset macos-release --target BagiMahjongArena
build/ClangmacOS/BagiMahjongArena --help
```

**A Release build overwrites the Debug executables and libraries in `build/ClangmacOS/`** (the same folder for every preset). Before the
next `MahjongTests` run: delete `build/ClangmacOS/lib/*.a` and build again with `macos-debug`. Run the arena in Release: a Debug
build is much slower. The program is not part of `ctest`; the library it is made of (`src/Mahjong/Arena/`) is tested by
`ArenaPlayerTest`, `ArenaRunTest`, `ArenaStatsTest` and `ArenaCliTest` in `MahjongTests`.

Commands (`A` is `build/ClangmacOS/BagiMahjongArena`; every run prints the same bytes twice, `--time` apart):

```bash
A --focus normal --others random --first 4000 --seeds 1000         # one player against three random seats
A --focus easy --others normal --seeds 1000                        # head to head: easy against three normal seats
A --focus normal,easy,hard --others random --seeds 1000            # several players on the same walls, with the paired rows
A --focus normal,normal:skipClaimPercent=20 --others normal        # a profile with knobs against the plain Normal
A --focus normal --seeds 100 --time                                # add the time per decision
```

Options: `--focus LIST` (default `normal`), `--others SPEC` (default `random`), `--first N` (default 4000), `--seeds COUNT` (default 250: 1000
hands per focus player), `--time`, `--help`. Exit code 0 is done, 1 is an action the rules refused (a defect: the seed and the seat are
printed), 2 is a bad argument (printed with the usage).

A player is `random`, `easy`, `normal`, or a level with knobs of `LevelProfile`, `<level>:<knob>=<value>,...` (the knobs are
`skipAcceptancePercent`, `skipDeclarationPercent`, `skipClaimPercent` (0..100) and `takeValueClaimsAtEqualShanten` (0 or 1); the level's
profile is the start and the knobs are set on it). In `--focus` a part with an `=` and no `:` goes on the knobs of the player before it,
so `normal,easy:skipClaimPercent=20,skipDeclarationPercent=10` is two players. (Hard was removed on the owner's decision; it
comes back as the search player of `mahjong-bot-search`.) `random` is the "seek win" seat of `ComputerStrengthTest` (wins when it can, declares when it can, builds a set
when it can, else any legal action).

## The conventions

They are those of `ComputerStrengthTest`, so the old numbers come out exactly:

- **Walls.** The seed `s` is the wall `MakeWall(MahjongRng(s), default settings, east)` with `east = s % 4`, round wind East, flowers
  off, one hand per seed. Every focus player plays the same walls, so any two of them can be compared hand by hand.
- **Seats.** For each seed the focus player plays each of the four seats in turn, against the "others" on the other three: four
  hands (one per seat, the rotations) per seed, so `--seeds 1000` is 4000 hands per focus player. A computer on seat `i` is
  `ComputerPlayer(SeatSeed(seed, i), level)`; the random seats share one `MahjongRng(seed * 13 + 5)`.
- **Seeds.** `--first 4000 --seeds 1000` is the seeds 4000 .. 4999. The seeds are a range you give, not a random draw, so a run is
  reproducible; the numbers below are for 4000 .. 4999.
- **Points** are the change of the focus seat in the hand (`deltas` of `SettleHand`); a drawn hand pays nothing.

## How to read the interval

- **The seed is the unit.** The four rotations of a seed play the same wall, so the four hands are not independent samples. A number
  is the mean over the seeds of the per-seed value (the four rotations divided by four) and `+-` is 1.96 standard errors of that mean,
  a 95 percent interval (a normal approximation: fine from about 100 seeds, rough below).
- **Two plain intervals that overlap do not mean "no difference".** The players play the same walls, so most of the luck of a wall is
  the same for both. Read the **paired difference** rows (`paired difference against <the first focus player>`): they take the
  difference of the two players seed by seed and the interval of that, which is much narrower. `significant` means that interval
  excludes 0.
- **The interval shrinks with the square root of the number of seeds:** to make a half-width ten times smaller you need a hundred
  times the seeds. A difference of 1 point a hand needs thousands of seeds, a few seconds of Release each thousand.
- **Do not pick the range after you have seen the result** (nor the knob value, nor the other player): decide the seeds first, or take
  a second range (`--first 20000`) to confirm what the first one suggests.
- **One `significant` row among twenty tried is expected by chance** (95 percent is one in twenty). Every knob value you try is one more
  try.
- Wins %, drawn % and declared % are per-seed means in the same way, not a binomial interval over the hands.

## The first check

The first check reproduces the numbers of the Hard-lever measurements of `mahjong-computer-difficulty` (made with a throw-away harness
that the arena replaces). Seeds 4000 .. 4999, Release, on the machine it was written on (one thread): the run of
1000 seeds takes 3.9 s for one focus player (60 seeds 0.26 s, 250 seeds 1.0 s, three focus players on 1000 seeds 12 s), so the program
has no `--jobs`.

**(1) Normal against three random seats**, `A --focus normal --others random --first 4000 --seeds 1000`:

```
player                    hands            wins %           points/hand     drawn %  declared %   avg win score
normal                     4000     82.20 +- 1.20        155.16 +- 4.67       10.68       95.80           43.30
  others (per seat)        4000              2.38                -51.72       10.68       11.60           55.86
```

That is 3288 wins of 4000 (82.20 %), 620620 points (155.155 a hand), 427 drawn hands (10.68 %) and an average winning score of 43,
exactly the numbers of the old harness. The 60-seed run (`--seeds 60`) gives 186 wins of 240 (77.50 %) and +31730 points (132.21 a
hand), the numbers of `ComputerStrengthTest`.

**(2) Normal against three Normal seats**, `A --focus normal --others normal --first 4000 --seeds 1000`:

```
player                    hands            wins %           points/hand     drawn %  declared %   avg win score
normal                     4000     25.00 +- 0.00          0.00 +- 0.00        0.00       55.77           45.46
  others (per seat)        4000             25.00                  0.00        0.00       55.77           45.46
```

Exactly 1000 wins of 4000, 0 points, an interval of 0 (the mirror identity: the four rotations of a seed replay one game with the
seats turned, so the focus player wins exactly once in each of them). The 0 drawn hands is a fact of these 1000 walls and of
four Normal players (they all build a hand, and not one of the 1000 games ran the wall out), not a property of the arena: against
random seats 427 of the same walls are drawn. It is the same as the old harness found ("drawn hands 0 in that setup").

**(3) Easy, new numbers** (not expected from before): `A --focus easy --others random ...` and `A --focus easy --others normal ...`

```
easy against three random seats
player                    hands            wins %           points/hand     drawn %  declared %   avg win score
easy                       4000     40.45 +- 1.50         77.34 +- 4.91       45.42       59.55           47.93
  others (per seat)        4000              4.71                -25.78       45.42       19.50           50.44

easy against three Normal seats
easy                       4000      7.33 +- 0.78        -47.92 +- 2.85        0.40       18.27           44.51
  others (per seat)        4000             30.76                 15.97        0.40       62.32           43.74
```

Easy wins about half of Normal's wins against random seats (1618 wins against 3288) and earns half the points, and against three
Normal seats it wins 293 of its 4000 hands (a fair share would be 1000) and loses 48 points a hand. With both on the same walls
(`--focus normal,easy --others random`) the paired row is -77.82 +- 6.30 points a hand and -41.75 +- 1.90 wins %, both `significant`.

**(4) One of the Hard levers, the suit focus** (lever A of the entry's measurements: aim at one suit when the hand holds at least 5 tiles
of it, with a tolerance of 6 copies of acceptance, off-suit claims not passed). The lever is not in the tree: it was applied in a
scratch state (`experiments.diff` and `ComputerAim.*` of the measurements, with three rows added to `kProfileKnobs`), run, and
removed again. Normal and the lever on the same walls, seeds 4000 .. 4999 (the name of the lever's row is shortened and the
unused columns are cut):

```
others: random
normal                                                  4000     82.20 +- 1.20        155.16 +- 4.67       10.68       95.80           43.30
lever A (flushAimTiles=5,flushAcceptanceTolerance=6)    4000     77.78 +- 1.25        163.16 +- 6.01       14.43       92.97           48.04
paired difference                                       points/hand +8.01 +- 5.73 significant       wins % -4.42 +- 1.46 significant

others: normal
normal                                                  4000     25.00 +- 0.00          0.00 +- 0.00        0.00       55.77           45.46
lever A                                                 4000     22.23 +- 1.11         -2.94 +- 3.75        0.17       49.83           48.14
paired difference                                       points/hand -2.94 +- 3.75                   wins % -2.77 +- 1.11 significant
```

That is 3111 wins and +652650 points against random seats and 889 wins and -11740 points against three Normal seats, again the numbers of
the old harness. The reading is the one of the entry: the lever earns about 8 more points a hand against random seats at 4 percent fewer
wins, and against equal seats it wins fewer hands (the points are not significantly below Normal's here, the wins are), so it is no
stronger than Normal head to head.

## Adding a knob for an experiment

A knob is a field of `LevelProfile` (`src/Mahjong/Game/ComputerLevel.h`) that the choosers read. To make it a thing the arena can set from
the command line, add one row to `kProfileKnobs` in `src/Mahjong/Arena/ArenaPlayer.h` (the name of the field, a pointer to it, the largest
value) and raise `kProfileFieldCount`; the structured binding in `ArenaPlayer.cpp` that names every field stops compiling until it
lists the new one, and `ArenaPlayerTest` fails when a row does not move its own field. Then `--focus normal,normal:<knob>=<value>`
measures it against Normal, paired.

## What is not built

A line-up of four different players (round robin) or two against two: one against three covers the measurements so far, and a shell
loop over `--others` covers the rest. A Windows build of the arena is untried (the target is plain C++).
