# Arena/ — measuring the computer players

Namespace `BECore::Mahjong::Arena`, plain C++, no engine services. The program is `BagiMahjongArena` (`tools/arena/main.cpp` only forwards `argv`); usage and examples: `docs/bot-arena.md`.

| File | What |
|------|------|
| `ArenaPlayer` | `ParseArenaPlayer(spec)`: `random`, a level `1` .. `10` (or `easy` = 1, `normal` = 5), `<level>:knob=value,...`; `kProfileKnobs` is the one place a `LevelProfile` knob gets a name (a new field without a row fails the build). `ChooseSeekWin` is the random seats' chooser (the same draws as `HandTestSupport.h`) |
| `ArenaHand` | `PlayArenaHand(seed, players, timing)` with the conventions of `ComputerStrengthTest` (wall, seat seeds, random-seat rng), so the old numbers come out |
| `ArenaRun` | `RunArena(config)`: the same walls for every focus player, deterministic and **the same bytes at any `jobs`** (threads take whole seeds into slots of their own, merged in seed order; the timing sums the threads; a failure reports the lowest seed that failed); **the seed (its four seat rotations) is the independent unit**. `RunArena(config, ArenaHandPlayer)` is the seam for a test that makes a hand fail |
| `ArenaStats` | `MeanCi` (95 %, `kIntervalZ` 1.96), per-seed series, `PairedDifference` on the same seeds, `IsSignificant` (the interval excludes 0) |
| `ArenaReport` | fixed-width text, fixed precision, no locale: the same result is the same bytes on every platform |
| `ArenaCli` | `ParseArenaArgs` (`--focus`, `--others`, `--first`, `--seeds`, `--jobs` (0 = hardware concurrency), `--time`, `--help`), exit codes 0 / 1 refused action / 2 bad arguments |

A change of a player is judged by its paired row. The reference numbers (Normal against random seats, the seeds that reproduce `ComputerStrengthTest`): `docs/bot-arena.md`; the search player's measurements and verdict: `docs/bot-search.md`.
