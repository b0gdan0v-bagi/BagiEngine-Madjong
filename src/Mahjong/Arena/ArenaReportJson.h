#pragma once

#include <BECore/Control/Json.h>
#include <Mahjong/Arena/ArenaStats.h>

namespace BECore::Mahjong::Arena {

    /// The `"format"` member of the document.
    inline constexpr StringView kArenaReportJsonFormat = "bagi-arena-report";
    /// Grows when a member changes meaning or goes; a new member does not change it.
    inline constexpr int kArenaReportJsonVersion = 1;
    /// Digits after the point of every number in the document (the text report has `ArenaReport.cpp`'s two).
    inline constexpr int kArenaReportJsonPrecision = 6;

    /**
     * @brief The report of `result` as one JSON document: the numbers of `FormatArenaReport` for a program to read (`tools/mahjong_tune.py`).
     *
     * An object with `format`, `version`, `first`, `seeds`, `handsPerFocus`, `others` (the spec of the three other seats) and `focus`,
     * an array in the order of `--focus`. A focus entry has `name`, `hands`, then `winPercent` and `pointsPerHand` (each an interval
     * `{mean, half, hasInterval}`: the cells of the main table), `drawnPercent`, `declaredPercent`, `avgWinScore` (plain numbers),
     * `others` (the row "others (per seat)": `hands`, `winPercent`, `pointsPerHand`, `drawnPercent`, `declaredPercent`, `avgWinScore`,
     * plain), `ends` (the table "how the hands end": `gaveWinPercent` and `standingScore` as intervals, `selfDrawPercentOfWins` and
     * `pointsPerLostHand` plain, and `others` with the same four plain) and `paired`: `null` for the first focus player, else
     * `{against, pointsPerHand, winPercent, gaveWinPercent, standingScore}`, each `{mean, half, hasInterval, significant}` (the two paired
     * tables). The score-line tables and the timing are not in it.
     *
     * Every number is rounded to `kArenaReportJsonPrecision` digits, never a negative zero, and every number is finite (a ratio without
     * hands is 0, an interval without seeds has `half` 0). The bytes are a function of `result` alone (members in key order), so the
     * same result is the same bytes at any `jobs`; `result.timing` is not looked at.
     */
    [[nodiscard]] Json ArenaReportToJson(const ArenaResult& result);

    /// `ArenaReportToJson` on one line, no trailing newline.
    [[nodiscard]] String FormatArenaReportJson(const ArenaResult& result);

}  // namespace BECore::Mahjong::Arena
