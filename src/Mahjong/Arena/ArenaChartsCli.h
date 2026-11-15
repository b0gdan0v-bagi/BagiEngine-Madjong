#pragma once

#include <Mahjong/Arena/ArenaChartsReport.h>
#include <Mahjong/Arena/ArenaCli.h>

namespace BECore::Mahjong::Arena {

    /// What the program measures: `Claims` is the default, so the old command lines and their bytes stand.
    CORE_ENUM(ChartKind, uint8_t, Claims, Discards)

    /// The word of `--kind`.
    [[nodiscard]] StringView ChartKindName(ChartKind kind) noexcept;

    struct ChartsArgs {
        /// Valid for `RunCharts` and `RunDiscardCharts` (`ValidateChartsConfig` is empty).
        ChartsConfig config;
        ChartKind kind = ChartKind::Claims;
        /// The tables as markdown (for the doc) instead of the fixed-width text.
        bool markdown = false;
        bool help = false;
    };

    /// The options of `--help`; `docs/charts-claims.md` and `docs/charts-discards.md` ("Build and run") have the same with an example.
    [[nodiscard]] String ChartsUsage();

    /**
     * @brief Reads the arguments (without the name of the program): `--kind claims|discards` (default `claims`), `--first`,
     *        `--seeds`, `--worlds`, `--jobs` (0 = hardware concurrency), `--markdown`, `--help`.
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
