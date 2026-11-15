#pragma once

#include <Mahjong/Arena/ArenaStats.h>

namespace BECore::Mahjong::Arena {

    struct ArenaReportOptions {
        /// The score-line tables.
        bool kinds = true;
    };

    /**
     * @brief The text of the report of `result`: the header, the table of the focus players, the table of how their hands end, the
     *        paired rows, the score-line tables and, with timing, the table of the time per decision.
     *
     * The tables and their columns are in `docs/bot-arena.md`. With `result.timing` the last table is wall clock, the one thing in the
     * report that is not deterministic.
     *
     * Fixed-width columns, numbers by `Format` with a fixed precision and no locale: the same result gives the same bytes on every
     * platform. A new column goes in a table of its own: the main table keeps its bytes.
     */
    [[nodiscard]] String FormatArenaReport(const ArenaResult& result, const ArenaReportOptions& options = {});

}  // namespace BECore::Mahjong::Arena
