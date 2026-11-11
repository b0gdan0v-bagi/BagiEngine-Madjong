# Arena/ — measuring the computer players

Namespace `BECore::Mahjong::Arena`, plain C++, no engine services. The program is `BagiMahjongArena` (`tools/arena/main.cpp` only forwards `argv`); usage and examples: `docs/bot-arena.md`.

| File | What |
|------|------|
| `ArenaPlayer` | `ParseArenaPlayer(spec)`: `random`, a level `1` .. `10` (or `easy` = 1, `normal` = 5), `<level>:knob=value,...`; `kProfileKnobs` is the one place a `LevelProfile` knob gets a name (a new field without a row fails the build). `ChooseSeekWin` is the random seats' chooser (the same draws as `HandTestSupport.h`) |
| `ArenaHand` | `PlayArenaHand(seed, players, timing, worlds)` with the conventions of `ComputerStrengthTest` (wall, seat seeds, random-seat rng), so the old numbers come out; a non-null `WorldLoop*` plays a search decision's worlds through it (`SearchRun::RunWorlds`), the record is the same, the timed decision includes its worlds |
| `ArenaRun` | `RunArena(config)`: the same walls for every focus player, deterministic and **the same bytes at any `jobs`** (threads take whole seeds into slots of their own, merged in seed order; the timing sums the threads; a failure reports the lowest seed that failed); **the seed (its four seat rotations) is the independent unit**. Two levels of threads: `min(jobs, count)` seed threads, each with its own `WorldThreads(jobs / seedThreads)` for the worlds of its search decisions (it appears only with fewer seeds than `jobs`; the total never exceeds `jobs`). `RunArena(config, ArenaHandPlayer)` is the seam for a test that makes a hand fail; the player gets the seed thread's `WorldLoop` |
| `ArenaWorlds` | `WorldThreads(n)`: a persistent pool (`n - 1` threads plus the caller) behind `ForEach(count, body)` / `AsLoop()`, the `WorldLoop` of `SearchRun`; one call at a time, the body must not throw; with `n` 1 a plain loop, no thread |
| `ArenaStats` | `MeanCi` (95 %, `kIntervalZ` 1.96), per-seed series, `PairedDifference` on the same seeds, `IsSignificant` (the interval excludes 0) |
| `ArenaReport` | fixed-width text, fixed precision, no locale: the same result is the same bytes on every platform |
| `ArenaTable` | `--table-stats`: four Normal seats play each seed once, a `HandObserver` of `PlayArenaHand` (read-only, outside the timed decision) collects every own turn with the number of declared opponents, and `AddHand` tallies how often another seat wins in the following round (the source of `kOpponentWinPerRoundPermille`), the scores of winners and of standing seats, the share of wins on a discard; integers per seed slot, the same bytes at any `jobs` (`docs/own-draws-ev.md`) |
| `ArenaCli` | `ParseArenaArgs` (`--focus`, `--others`, `--first`, `--seeds`, `--jobs` (0 = hardware concurrency), `--time`, `--table-stats` (refuses `--focus`, `--others`, `--time`), `--help`), exit codes 0 / 1 refused action / 2 bad arguments |

A change of a player is judged by its paired row. The reference numbers (Normal against random seats, the seeds that reproduce `ComputerStrengthTest`): `docs/bot-arena.md`; the search player's measurements and verdict: `docs/bot-search.md`.
