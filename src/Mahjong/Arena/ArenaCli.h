#pragma once

#include <Mahjong/Arena/ArenaLogs.h>
#include <Mahjong/Arena/ArenaLogsReport.h>
#include <Mahjong/Arena/ArenaReport.h>
#include <Mahjong/Arena/ArenaTable.h>

namespace BECore::Mahjong::Arena {

    /// The exit codes of `BagiMahjongArena`.
    inline constexpr int kExitOk = 0;
    /// The model refused an action of a player (a defect of the rules or of a player; the error names the seed and the seat).
    inline constexpr int kExitRefusedAction = 1;
    /// The path of `--logs` cannot be read (the error names it). The same number as a refused action: both are "the work could not be done".
    inline constexpr int kExitCannotRead = 1;
    /// The arguments are wrong (the error and the usage are printed).
    inline constexpr int kExitBadArguments = 2;

    /// The player of `--focus` when it is left out, and of `--others`.
    inline constexpr std::string_view kDefaultFocusSpec = "normal";
    inline constexpr std::string_view kDefaultOthersSpec = "random";

    /// What the command line asks for.
    struct ArenaArgs {
        /// The seeds, the players and `timing`, valid for `RunArena` (`ValidateArenaConfig` is empty).
        ArenaConfig config;
        /// `--help` was given: print the usage and play nothing.
        bool help = false;
        /// `--logs PATH`: read the match logs of a folder or a file instead of playing (`config` is then only `focus`, the bots of the
        /// analysis); `--check` prints the verdicts only.
        bool logMode = false;
        eastl::string logsPath;
        bool check = false;
        /// `--differences N`: how many of the differences between the people and the first bot the log report lists.
        int differences = kDefaultDifferences;
        /// `--table-stats` was given: play four Normal seats over `config.first` / `config.count` / `config.jobs` and print `FormatTableStats`
        /// (`config` holds the defaults of the other fields; `--focus`, `--others` and `--time` are refused with it).
        bool tableStats = false;
    };

    /// The text of `--help`: the options with their defaults, the exit codes and the interval rule in two lines.
    [[nodiscard]] eastl::string ArenaUsage();

    /**
     * @brief Reads the arguments (without the name of the program).
     *
     * `--focus LIST` (players separated by commas; a part with an `=` and no `:` goes on the knobs of the player before it, so
     * `easy:skipClaimPercent=20,skipDeclarationPercent=10,normal` is two players), `--others SPEC`, `--first N`, `--seeds COUNT`,
     * `--jobs N` (1 to `kMaxJobs` threads in all, for the seeds and then the worlds of a search decision; 0 is the hardware concurrency, at least 1), `--time`, `--table-stats` (the measurement of the
     * own-draws value's constants, `docs/own-draws-ev.md`; it refuses `--focus`, `--others`, `--time` and `--logs`) and `--help` (which wins over everything after it), `--logs PATH` (the log mode:
     * `--first`, `--seeds`, `--others` and `--time` are refused with it, and so is a `random` in `--focus`, which names the bots the people are compared with), `--check` and
     * `--differences N` (the log mode only; `--jobs` then plays whole matches in threads). A bad argument is an error text that names it: an unknown option, a missing or malformed
     * value, a bad player spec, a range of seeds that `ValidateArenaConfig` refuses, a word that is no option.
     */
    [[nodiscard]] std::expected<ArenaArgs, eastl::string> ParseArenaArgs(eastl::span<const std::string_view> args);

    /// What the program prints: the report or the usage to `out`, an error and the usage to `err`.
    struct ArenaCliOutput {
        eastl::string out;
        eastl::string err;
    };

    /**
     * @brief The whole program: parses `args`, plays, formats and returns the exit code.
     *
     * `--help` prints the usage to `out` (`kExitOk`); bad arguments print the error and the usage to `err` and play nothing
     * (`kExitBadArguments`); a refused action of the model prints its error to `err` (`kExitRefusedAction`); else the report of
     * `FormatArenaReport` goes to `out`. The same arguments give the same bytes, `--time` apart.
     */
    [[nodiscard]] int RunArenaCli(eastl::span<const std::string_view> args, ArenaCliOutput& output);

    /**
     * @brief The same program with a reader of files, which `--logs` needs (the overload above has none: `--logs` is then `kExitCannotRead`).
     *
     * `--logs PATH --check` prints `FormatCheckLines` (exit `kExitOk` even when files are bad); `--logs PATH` prints `FormatCorpusSummary`
     * of the logs that replay and then `FormatLogAnalysis` (the agreement with the bots of `--focus`, the real-game numbers, the differences); a path the source cannot read prints its error to `err`
     * (`kExitCannotRead`). The same files give the same bytes, whatever order the source lists them in.
     */
    [[nodiscard]] int RunArenaCli(eastl::span<const std::string_view> args, const LogSource& source, ArenaCliOutput& output);

}  // namespace BECore::Mahjong::Arena
