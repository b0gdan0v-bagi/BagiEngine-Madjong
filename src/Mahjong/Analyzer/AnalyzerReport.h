#pragma once

#include <BECore/Control/Json.h>
#include <Mahjong/Analyzer/AnalyzerPosition.h>
#include <Mahjong/Analyzer/AnalyzerRun.h>

namespace BECore::Mahjong::Analyzer {

    /// Grows when a field changes meaning or goes; a new field does not change it.
    inline constexpr int kAnalysisJsonVersion = 1;

    /// Digits after the point of every number, text and JSON.
    inline constexpr int kReportPrecision = 2;

    /// Where a position came from; all of it may be empty (no source line is printed then).
    struct PositionLabel {
        /// As given on the command line.
        String source;
        /// The numbers of `LogPosition`; `hand` 0 means not from a log.
        int hand = 0;
        int actionIndex = 0;
        bool hasLogged = false;
        HandAction logged;
    };

    [[nodiscard]] PositionLabel LabelOfLogPosition(StringView file, const LogPosition& position);

    /// The one place that writes an action for a person (`discard 5p declare`, `claim chow 3s-4s-5s`, `pass`).
    [[nodiscard]] String ActionNotation(const HandAction& action);

    /**
     * @brief The text of one analysis: the header, the position (written from `view`, the view the analysis was made on) and a row
     * per legal action, best EV first, `view.legal` order breaking a tie. Columns and notes: `docs/ev-analyzer.md`, "Reading the
     * report". The header says the opponents are Normal bots and the numbers are not an equilibrium. A cell without a number is blank,
     * never `nan`.
     *
     * Fixed-width columns, `kReportPrecision` digits and no locale: the same input gives the same bytes on every platform.
     */
    [[nodiscard]] String FormatAnalysisReport(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label);

    /**
     * @brief The analysis as a JSON object (`kAnalysisJsonVersion`).
     *
     * ```
     * {"version", "source", "hand", "actionIndex"          the label (null when not from a log / unnamed)
     *  "logged": action object | null,
     *  "seat", "seatWind", "roundWind", "phase", "own": "1p 2p ...", "pending": "5p" | null, "liveTiles",
     *  "worldsAsked", "worldsPlayed", "worldsDropped", "seed", "normalSeed", "intervalZ",
     *  "opponents": "normal", "opponentLevel": 5, "seatAfterAction": "normal", "hiddenTiles": "uniform", "points": "this hand",
     *  "actions": [{"action": ActionToJson, "text", "ev", "half", "vsBest": {"mean", "half", "significant"} | null,
     *                "vsNormal": {...} | null, "best", "normal", "logged"} ...]}
     * ```
     * `actions` is in the order of the text. `half` is null for a mean without an interval, `vsBest` / `vsNormal` on the row with no
     * difference to itself. Numbers are rounded before they are stored, so the bytes do not depend on how a platform prints a double.
     */
    [[nodiscard]] Json AnalysisToJson(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label);

    /// One line, no spaces.
    [[nodiscard]] String FormatAnalysisJson(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label);

}  // namespace BECore::Mahjong::Analyzer
