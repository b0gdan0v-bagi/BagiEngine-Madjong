#pragma once

#include <Mahjong/Arena/ArenaChartsRun.h>

namespace BECore::Mahjong::Arena {

    /**
     * @brief The text of the charts: the header (range, worlds, counters, `kFewRows`, the caveat), the main table (kind x shanten before
     *        x stage of the wall) and the one-way tables (kind, tile group, shanten change, East, open sets).
     *
     * Every cell is a line `rows seeds gain +- half` with the word `significant` or `few`, followed by its `Normal takes` and `Normal
     * passes` lines. Fixed width, fixed precision, no locale: the same result gives the same bytes on every platform. A cell without
     * rows prints blanks where a number would be, never `nan`.
     */
    [[nodiscard]] String FormatChartsReport(const ChartsResult& result);

    /// The same cells as markdown tables, for `docs/charts-claims.md`: the numbers of the doc are pasted from this, never typed.
    [[nodiscard]] String FormatChartsMarkdown(const ChartsResult& result);

    /**
     * @brief The text of the discard charts: the header (range, worlds, counters, the gain rule, the caveat), the main table (value held
     *        x shanten after Normal's discard x cost of the valued discard) and the one-way tables (stage, open sets, East, shanten,
     *        cost, value held).
     *
     * Every cell is a line `rows seeds gain +- half` with the word `significant` or `few`; there are no `Normal takes` / `Normal
     * passes` lines, Normal always takes its own discard. The same fixed width and precision as the claim report, no locale, no `nan`.
     */
    [[nodiscard]] String FormatDiscardChartsReport(const DiscardChartsResult& result);

    /// The same cells as markdown tables, for `docs/charts-discards.md`: the numbers of the doc are pasted from this, never typed.
    [[nodiscard]] String FormatDiscardChartsMarkdown(const DiscardChartsResult& result);

}  // namespace BECore::Mahjong::Arena
