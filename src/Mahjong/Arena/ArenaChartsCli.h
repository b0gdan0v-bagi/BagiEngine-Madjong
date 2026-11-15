#pragma once

#include <Mahjong/Arena/ArenaChartsReport.h>
#include <Mahjong/Arena/ArenaCli.h>

namespace BECore::Mahjong::Arena {

    struct ChartsArgs {
        /// Valid for `RunCharts` (`ValidateChartsConfig` is empty).
        ChartsConfig config;
        /// The tables as markdown (for the doc) instead of the fixed-width text.
        bool markdown = false;
        bool help = false;
    };

    /// The options of `--help`; `docs/charts-claims.md` ("Build and run") has the same with an example.
    [[nodiscard]] String ChartsUsage();

    /**
     * @brief Reads the arguments (without the name of the program): `--first`, `--seeds`, `--worlds`, `--jobs` (0 = hardware
     *        concurrency), `--markdown`, `--help`.
     *
     * `--help` wins over everything after it. A bad argument is an error text that names the option.
     */
    [[nodiscard]] std::expected<ChartsArgs, String> ParseChartsArgs(Span<const StringView> args);

    /**
     * @brief The whole program: parses `args`, runs, formats and returns the exit code of the arena (`kExitOk`, `kExitRefusedAction`,
     *        `kExitBadArguments`).
     *
     * The report and `--help` go to `output.out`; an error (bad arguments with the usage, a refused action) goes to `output.err`. The
     * same arguments give the same bytes.
     */
    [[nodiscard]] int RunChartsCli(Span<const StringView> args, ArenaCliOutput& output);

}  // namespace BECore::Mahjong::Arena
