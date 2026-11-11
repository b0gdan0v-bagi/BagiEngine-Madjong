#pragma once

#include <Mahjong/Arena/ArenaStats.h>

namespace BECore::Mahjong::Arena {

    /// What the text of a report holds besides its tables of numbers.
    struct ArenaReportOptions {
        /// The table of the share of each score line among the won hands, one per focus player.
        bool kinds = true;
    };

    /**
     * @brief The text of the report of `result`: the header, the table of the focus players, the table of how their hands end, the
     *        paired rows, the score-line tables and, with timing, the table of the time per decision.
     *
     * The header names the seeds, the hands, the focus players and the others, and says the interval rule in a line. The table has a
     * row per focus player (`hands`, `wins %` and `points/hand` with their intervals, `drawn %`, `declared %`, the average winning score)
     * and under it the row `others (per seat)` of the same numbers without intervals. Right after it the table "how the hands end"
     * has the same rows: `gave win %` and `standing score` with intervals, `self-draw % of wins` and `points/lost hand` as ratios of
     * the totals, and a legend with the definitions. With two or more focus players a second table
     * holds the paired difference of each against the first one (points a hand and wins %, each with its interval and the word
     * `significant` when the interval excludes 0), and a third one the same for `gave win %` and `standing score`. A table per focus player gives the share of each score line among the won hands,
     * only for the lines that occur (focus and others side by side); with `result.timing` a last table gives the decisions and the microseconds per decision, marked as wall clock, which is the one
     * thing in the report that is not deterministic.
     *
     * Fixed-width columns, numbers by `Format` with a fixed precision and no locale: the same result gives the same bytes on every
     * platform.
     */
    [[nodiscard]] eastl::string FormatArenaReport(const ArenaResult& result, const ArenaReportOptions& options = {});

}  // namespace BECore::Mahjong::Arena
