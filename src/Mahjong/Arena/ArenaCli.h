#pragma once

#include <Mahjong/Arena/ArenaLogs.h>
#include <Mahjong/Arena/ArenaLogsReport.h>
#include <Mahjong/Arena/ArenaReport.h>
#include <Mahjong/Arena/ArenaReportJson.h>
#include <Mahjong/Arena/ArenaTable.h>

namespace BECore::Mahjong::Arena {

    /// The exit codes of `BagiMahjongArena`.
    inline constexpr int kExitOk = 0;
    /// The model refused an action of a player: a defect of the rules or of a player.
    inline constexpr int kExitRefusedAction = 1;
    /// The path of `--logs` cannot be read. The same number as a refused action: both are "the work could not be done".
    inline constexpr int kExitCannotRead = 1;
    inline constexpr int kExitBadArguments = 2;

    /// The players when `--focus` / `--others` are left out.
    inline constexpr StringView kDefaultFocusSpec = "normal";
    inline constexpr StringView kDefaultOthersSpec = "random";

    struct ArenaArgs {
        /// Valid for `RunArena` (`ValidateArenaConfig` is empty).
        ArenaConfig config;
        bool help = false;
        /// With `--logs` nothing is played: `config` then holds only `focus`, the bots of the analysis.
        bool logMode = false;
        String logsPath;
        bool check = false;
        int differences = kDefaultDifferences;
        /// Plays four Normal seats over `config.first` / `count` / `jobs`; `config` holds the defaults of its other fields.
        bool tableStats = false;
        /// The report of the played seeds as the JSON document of `ArenaReportJson.h`, not the text.
        bool json = false;
    };

    /// The options of `--help`; `docs/bot-arena.md` has the same with examples.
    [[nodiscard]] String ArenaUsage();

    /**
     * @brief Reads the arguments (without the name of the program).
     *
     * The options are `ArenaUsage()` and `docs/bot-arena.md`. `--help` wins over everything after it. `--table-stats` (the constants of
     * the own-draws value, `docs/own-draws-ev.md`) refuses `--focus`, `--others`, `--time` and `--logs`; `--json` (the report of the
     * played seeds as a document) refuses `--logs`, `--check`, `--differences`, `--table-stats` and `--time`; the log mode (`--logs`)
     * refuses the options that play seeds and a `random` in `--focus` (the bots the people are compared with), and `--jobs` then
     * plays whole matches. A bad argument is an error text that names it.
     */
    [[nodiscard]] std::expected<ArenaArgs, String> ParseArenaArgs(Span<const StringView> args);

    /// What the program prints: the report or the usage to `out`, an error and the usage to `err`.
    struct ArenaCliOutput {
        String out;
        String err;
    };

    /**
     * @brief The whole program: parses `args`, plays, formats and returns the exit code.
     *
     * The report (with `--json` the document and a newline) and `--help` go to `out`; an error (bad arguments with the usage, a refused action) goes to `err` with its exit code
     * constant. The same arguments give the same bytes, `--time` apart.
     */
    [[nodiscard]] int RunArenaCli(Span<const StringView> args, ArenaCliOutput& output);

    /**
     * @brief The same program with the reader of files that `--logs` needs; the overload above has none, so there `--logs` is
     *        `kExitCannotRead`.
     *
     * `--check` exits `kExitOk` even when files are bad: `tools/collect_logs.py` reads the verdicts. The same files give the same
     * bytes, whatever order the source lists them in.
     */
    [[nodiscard]] int RunArenaCli(Span<const StringView> args, const LogSource& source, ArenaCliOutput& output);

}  // namespace BECore::Mahjong::Arena
