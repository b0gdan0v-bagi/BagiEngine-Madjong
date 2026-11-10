# Arena/ — measuring the computer players

Namespace `BECore::Mahjong::Arena`, plain C++, no engine services. The program is `BagiMahjongArena` (`tools/arena/main.cpp` only forwards `argv`); usage and examples: `docs/bot-arena.md`.

| File | What |
|------|------|
| `ArenaPlayer` | `ParseArenaPlayer(spec)`: `random`, `easy`, `normal`, `<level>:knob=value,...`; `kProfileKnobs` is the one place a `LevelProfile` knob gets a name (a new field without a row fails the build). `ChooseSeekWin` is the random seats' chooser (the same draws as `HandTestSupport.h`) |
| `ArenaHand` | `PlayArenaHand(seed, players, timing)` with the conventions of `ComputerStrengthTest` (wall, seat seeds, random-seat rng), so the old numbers come out |
| `ArenaRun` | `RunArena(config)`: the same walls for every focus player, single-threaded, deterministic; **the seed (its four seat rotations) is the independent unit** |
| `ArenaStats` | `MeanCi` (95 %, `kIntervalZ` 1.96), per-seed series, `PairedDifference` on the same seeds, `IsSignificant` (the interval excludes 0) |
| `ArenaReport` | fixed-width text, fixed precision, no locale: the same result is the same bytes on every platform |
| `ArenaCli` | `ParseArenaArgs` (`--focus`, `--others`, `--first`, `--seeds`, `--time`, `--help`), exit codes 0 / 1 refused action / 2 bad arguments |

Reference numbers: Normal against three random seats on seeds 4000..4999 wins 3288 of 4000 hands, +620620 points; seeds 4000..4059 reproduce `ComputerStrengthTest` (186 / 27 / 27 / +31730). A change of a player is judged by its paired row.
