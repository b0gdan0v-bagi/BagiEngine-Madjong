# The EV analyzer

`BagiMahjongAnalyzer` is a command-line program that takes **one position of one seat** and prints, for every legal action of that
seat, the expected points at the end of the hand with a 95 percent interval, and the paired difference to the best action and to the
action a Normal player would take. It is a ruler for a decision, the way the bot arena (`docs/bot-arena.md`) is a ruler for a player:
"was this discard worse than that one, and by how much" is answered by a number with an interval, not by a feeling.

## What it is for, and its limit

The idea is the poker chart: a table of what each action is worth in this spot. The limit is that the chart is against a **known
pool**, not against an equilibrium. The opponents are three Normal computer players (level 5), the seat itself plays Normal after the
action it is asked about, and the tiles it cannot see are drawn uniformly at random. So a number says "against this table", never "the
best move in mahjong". A person and a stronger bot would leave different numbers, and a defence the Normal bots do not make is not
priced: a discard that is safe only against a careful table looks the same as a risky one against Normal bots.

The points are those of **this hand only** (the `SettleHand` change of the seat's score; a drawn hand pays 0). The match is not modeled:
no score situation, no last hands.

The program only measures. It reads `src/Mahjong/Game/` through `BeginSearchOver` (the seam of the search player) and changes nothing there.

## Build and run

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"          # the reflection generator needs jinja2
cmake --preset macos-release
cmake --build --preset macos-release --target BagiMahjongAnalyzer
A=build/macos-release/bin/BagiMahjongAnalyzer      # the folder is the preset's: build/<preset>/bin
$A --help
```

Run it in Release: a Debug build is many times slower. It is not part of `ctest`; the library it is made of (`src/Mahjong/Analyzer/`) is
tested by `AnalyzerRunTest`, `AnalyzerPositionTest`, `AnalyzerReportTest` and `AnalyzerCliTest` in `MahjongTests`.

```bash
$A --log logs/matches/<file>.jsonl --hand 1 --action 17          # one decision of a match the game wrote
$A --log logs/matches/<file>.jsonl --hand 1 --seat 0 --worlds 1000   # every decision of seat 0 in hand 1, a quick scan
$A --position table.txt --jobs 0                                  # a hand-written position; --position - reads the standard input
$A --position table.txt --json                                    # one JSON object on one line
```

Options: `--log FILE` with `--hand K` and `--action I` or `--seat S`; `--position FILE`; `--worlds N` (1 to 1,000,000; default 16000),
`--seed N` (default 1), `--jobs N` (1 to 256; 0 is one thread per core; default 1), `--json`, `--help`. A bad combination (`--log` with
`--position`, `--log` without `--hand`, `--action` with `--seat`) is a bad argument that names the problem. Exit code 0 is done, 1 a
position could not be read or analyzed (the error is printed; reports made before it stay), 2 bad arguments.

**Threads.** `--jobs N` plays the worlds on N threads (`WorldThreads`), and the output is the same bytes at any N: the values are summed
in world order. `--jobs 0` is the way to run it.

## Where a position comes from

**A match log** (`logs/matches/<time>_seed<seed>.jsonl`, written by the game; `src/Mahjong/CLAUDE.md`, "Match log"). `--hand K` is the
hand, counted **from 1**: hand K is what happens after K-1 settlements, so a log without `Hand` events is numbered the same, and a
replayed drawn hand has a number of its own. `--action I` is the decision, counted **from 0** among the `Action` events of that hand
in log order; the passes of a claim window are decisions too. `--seat S` (0..3) is every decision of that seat in the hand, one report
after another; a decision with a single legal action has nothing to compare and is printed as one `skipped:` line. The log is replayed to the
decision and the position is reduced to the **view of the seat that acted**: what the program uses is the seat's own tiles and the public
table, never the other hands, even though the log holds the deal. The report marks `logged` on the action the log's player took.

**A hand-written text** (`--position`). Lines, `#` starts a comment, words are separated by spaces, tiles are the notation of the game
(`5p 3s Ew Rd F1 T4`: dots `p`, bamboo `s`, characters `m`, winds `Ew Sw Ww Nw`, dragons `Rd Wd Gd`, flowers `F1..F4`, seasons `T1..T4`),
seats are 0..3 counter-clockwise.

| line | meaning |
|------|---------|
| `seat S` | the seat the position is for (default 0) |
| `east S` | the dealer; the seat winds follow (default: the seat itself) |
| `round East\|South\|West\|North` | the round wind (default East) |
| `phase discard\|claim` | the seat has to discard (default), or answers a discard |
| `hand <tiles>` | the concealed tiles of the seat; each meld counts three: 14 in `discard`, 13 in `claim` |
| `drew <tile>` / `drew none` | `discard` only: the tile just drawn (a self-draw win and an upgraded kong need it). Default: the last tile of the `hand` line |
| `meld S chow\|pung\|kong\|ckong <tile> [from T]` | an open chow (its lowest tile), pung or kong, or a concealed kong of seat S; `from T`: claimed from the discard of seat T |
| `discards S <tiles>` | the tiles of seat S still in the pile, in the order discarded (not the pending one) |
| `pending <tile> from S` | `claim` only, required: the discard to answer, by seat S |
| `calling S [first]` | seat S has declared its calling hand; `first` = with its first discard |
| `bonus S <tiles>` | flowers and seasons laid open by seat S; any such line plays with the 144-tile set |
| `wall N` | the live tiles left (default: what the named tiles leave, with the loose reserve full) |

Every key but `meld` is given once (`discards`, `calling` and `bonus` once per seat). Tiles that are not named are unseen: they fill the other
hands and the wall, in a fixed order, because the model needs a consistent fill; it never reaches the analysis. The model judges what can
exist: more than four copies of a tile, a wrong hand size or a meld that cannot be is an error that names the line. The legal actions
come from the model, not from the text. A robbed-kong window cannot be written by hand (a log position in that window works).

*A discard.* East has drawn the 9s and holds a pair of East winds:

```
hand 1p 2p 4p 5p 8p 3s 4s 7s 9s 2m 5m 8m Ew Ew
```

*A claim window.* Seat 3 discarded a 5p, seat 0 holds a pair of 5p and the 4p and 6p:

```
seat 0
phase claim
hand 1p 2p 4p 6p 9p 4s 5s 7s 7m 8m 9m 5p 5p
discards 3 Ww
pending 5p from 3
```

## Reading the report

The second position at 4000 worlds (the first lines are the header, which states the approximations in words):

```
action                         EV        +-    vs best        +-  vs Normal        +-  note
claim chow 4p-5p-6p         23.64      5.27                                            best, Normal
pass                        11.66      5.88     -11.99      7.42     -11.99      7.42  worse than best, worse than Normal
claim pung                  -3.48      4.45     -27.12      5.95     -27.12      5.95  worse than best, worse than Normal
```

- **EV** is the mean points of the seat at the end of the hand after the action, over the sampled worlds; **+-** is 1.96 standard
  errors (the half of the 95 percent interval, normal approximation). Rows are sorted best EV first.
- **The world is the unit.** A world is one sample of the hidden tiles together with the seed of its rollout. Every action is played in
  the **same** worlds with the same seeds (common random numbers), so the numbers of two rows are comparable world by world.
- **vs best** and **vs Normal** are the mean and the interval of the per-world difference to the best row and to the action Normal
  chooses on this position (a fixed seed breaks a tie of its choice; the seed is in the header). The row itself has none and prints
  blanks. These intervals are narrower than the ones of two independent means, but not by much: the rollouts of two different actions
  separate early, and the measurement below puts the paired interval at 85 to 95 percent of the unpaired one. Reading the difference is
  still the right habit, because it is the quantity one asks about.
- **note:** `best`, `Normal`, `logged`, and `worse than best`, `better than Normal`, `worse than Normal` only where that interval excludes 0.
- **Many rows are many chances.** With 13 rows and a 95 percent interval, one `worse than` among rows that are all alike is expected by
  chance (about one in 20 per row). Look at the size of the difference and at whether a second seed (`--seed 2`) says the same.
- **`vs best` is biased a little in favor of the others being worse**: the best row was picked by the same worlds, so its mean is
  the maximum of noisy means. It matters for rows close to the best and small worlds; a second run with another seed is an unbiased check.
- A row of a win is exact (the points of the win do not depend on the unseen tiles) and its interval is 0.
- No `nan` is ever printed: a cell without a number or without an interval is blank.
- A discard with and without `declare` are two rows; so are the three chows of a window.

## The JSON

`--json` prints one object per position, on one line (JSON lines). `version` is `kAnalysisJsonVersion` (1); it grows when a field changes meaning or goes, not when one is added. Fields: the label (`source`,
`hand`, `actionIndex`, `logged`: null when not from a log), the position (`seat`, `seatWind`, `roundWind`, `phase`, `own` in notation,
`pending`, `liveTiles`), the run (`worldsAsked`, `worldsPlayed`, `worldsDropped`, `seed`, `normalSeed`, `intervalZ`) and the facts of the header
(`opponents`, `opponentLevel`, `seatAfterAction`, `hiddenTiles`, `points`), and `actions`, in the order of the text: `{action, text, ev, half,
vsBest: {mean, half, significant} | null, vsNormal: {...} | null, best, normal, logged}`. `action` is the object of the control API
(`ActionToJson`, `ActionFromJson` reads it back). Every number is rounded to 0.01 before it is stored. A `--seat` run prints a decision with a
single legal action as `{"skipped": "single legal action", ...}`. A reader (a chart, a hand review, a coach) builds on these fields.

## The approximations

- The opponents are Normal players (level 5) and the seat itself plays Normal after the chosen action, its later plays included: the value
  of "discard 5p" is the value of discarding 5p **and then playing like Normal**.
- The hidden tiles are sampled uniformly (the approximations of `ComputerWorld.h`: the opponents' hands are not conditioned on what
  their discards say beyond what the world sampler keeps).
- One hand's points; no defence is modeled beyond what Normal does; a drawn hand pays 0.
- Worlds that cannot be sampled or played are dropped and counted in the header (`dropped`).

## How many worlds

Measured in Release on an Apple M4 Pro (14 cores), `--jobs 0`, `--seed 1`, eight positions: six decisions of hand 1 of one match log
(the first seat's early discard, a late discard, a discard that can win, a chow window, a pung window, a late chow window) and two
written by hand (a window with a chow and a pung, a declared hand). The figure is the half-width (the `+-` column) of the **median
row** of each table, in points; the exact row of the win in the position with a win has 0. The procedure is the program itself with
`--json` at each size, read by a script.

EV, median `+-`:

| position (actions) | 250 | 1000 | 4000 | 16000 |
|---|---|---|---|---|
| early discard, 14 tiles (13) | 13.8 | 7.0 | 3.6 | 1.8 |
| late discard, 36 live tiles (10) | 16.5 | 8.2 | 3.8 | 1.9 |
| discard with a win (5) | 16.6 | 9.8 | 4.6 | 2.2 |
| claim: chow or pass (2) | 12.5 | 6.5 | 3.3 | 1.6 |
| claim: pung or pass (2) | 17.2 | 8.4 | 4.3 | 2.2 |
| late claim: chow or pass (2) | 20.8 | 10.1 | 4.8 | 2.5 |
| claim: chow, pung, pass (3) | 20.5 | 10.4 | 5.3 | 2.6 |
| declared hand (13) | 68.6 | 36.0 | 18.4 | 9.1 |

Difference to the best row, median `+-`:

| position (actions) | 250 | 1000 | 4000 | 16000 |
|---|---|---|---|---|
| early discard, 14 tiles (13) | 17.9 | 8.4 | 4.3 | 2.1 |
| late discard, 36 live tiles (10) | 26.2 | 10.8 | 5.1 | 2.4 |
| discard with a win (5) | 17.7 | 9.8 | 4.7 | 2.3 |
| claim: chow or pass (2) | 16.6 | 8.6 | 4.4 | 2.1 |
| claim: pung or pass (2) | 21.6 | 11.0 | 5.5 | 2.8 |
| late claim: chow or pass (2) | 26.1 | 12.4 | 6.0 | 3.0 |
| claim: chow, pung, pass (3) | 25.5 | 13.4 | 6.7 | 3.3 |
| declared hand (13) | 95.2 | 49.8 | 24.6 | 12.3 |

Wall time in seconds (14 threads):

| position (actions) | 250 | 1000 | 4000 | 16000 |
|---|---|---|---|---|
| early discard, 14 tiles (13) | 0.9 | 3.2 | 12.7 | 50.0 |
| late discard, 36 live tiles (10) | 0.2 | 0.6 | 2.2 | 8.9 |
| discard with a win (5) | 0.1 | 0.3 | 1.1 | 4.2 |
| claim: chow or pass (2) | 0.1 | 0.5 | 1.9 | 7.6 |
| claim: pung or pass (2) | 0.1 | 0.2 | 0.7 | 2.9 |
| late claim: chow or pass (2) | 0.0 | 0.1 | 0.3 | 1.1 |
| claim: chow, pung, pass (3) | 0.2 | 0.6 | 2.4 | 9.7 |
| declared hand (13) | 0.6 | 2.5 | 9.6 | 37.1 |

The half-width falls with the square root of the worlds (four times the worlds, half the width) and the time is linear in worlds
times actions. **The default is 16000 worlds**: it gives about 2 points on a typical action (1.6 to 2.6 in seven of the eight
positions), at 1 to 50 seconds on 14 cores. A scan of a whole hand wants `--worlds 1000` (about 7 points of half-width: enough to
see a blunder of 20 points, not one of 3) and a second look at the interesting decisions at the default. The declared hand, which
is a hand of many points, has a spread ten times larger and needs ten times the width (or a hundred times the worlds) for the same
claim; the half-width scales with the standard deviation of the points. A larger count is
a longer wait, not a different answer: the numbers above 16000 are the same experiment with a smaller error.

Checks of the bytes: the same arguments twice give the same bytes; `--jobs 1`, `--jobs 0` and `--jobs 5` give the same bytes (the 16000
worlds of a claim of two rows take 8 s at `--jobs 0` and 80 s at `--jobs 1`).
