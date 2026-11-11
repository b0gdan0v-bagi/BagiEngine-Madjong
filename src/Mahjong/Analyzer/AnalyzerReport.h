#pragma once

#include <BECore/Control/Json.h>
#include <Mahjong/Analyzer/AnalyzerPosition.h>
#include <Mahjong/Analyzer/AnalyzerRun.h>

namespace BECore::Mahjong::Analyzer {

    /// The `version` of the JSON of one analysis. It grows when a field changes meaning or goes; a new field does not change it.
    inline constexpr int kAnalysisJsonVersion = 1;

    /// The digits after the point of every number of the report (text and JSON).
    inline constexpr int kReportPrecision = 2;

    /// Where a position came from. All of it may be empty: a position nobody named is printed without a source line.
    struct PositionLabel {
        /// The name of the log file as given on the command line, or the name of a hand-written text; empty = unnamed.
        eastl::string source;
        /// A position from a log: the numbers of `LogPosition` (hand 1-based, action 0-based) and the action the log's player took.
        /// `hand` 0 = not from a log.
        int hand = 0;
        int actionIndex = 0;
        bool hasLogged = false;
        HandAction logged;
    };

    /// The label of `position` of the log file `file`.
    [[nodiscard]] PositionLabel LabelOfLogPosition(eastl::string_view file, const LogPosition& position);

    /// An action as short text: `discard 5p`, `discard 5p declare`, `win`, `kong 5p concealed`, `upgrade 5p`, `claim chow 3s-4s-5s`
    /// (by `chowFirst`), `claim pung`, `claim kong`, `claim mahjong`, `pass`. The one place that writes an action for a person.
    [[nodiscard]] eastl::string ActionNotation(const HandAction& action);

    /**
     * @brief The text of one analysis: the header, the position and the table of the actions, best EV first.
     *
     * The header says in a few lines what the numbers are (opponents and play-on are Normal, hidden tiles are sampled uniformly, the
     * points are this hand's, the worlds asked / played / dropped, the seeds, the interval rule, "not an equilibrium"). The position is
     * written from `view`, which is the view the analysis was made on. The table has a row per legal action, sorted by the mean of the
     * EV, highest first, the order of `view.legal` (the order of `analysis.rows`) breaking a tie, with the columns
     * `action | EV | +- | vs best | +- | vs Normal | +- | note`. `note` names `best`, `Normal` and `logged` (the action `label` says
     * the log's player took) and, only where the interval of a difference excludes 0, `worse than best`, `better than Normal` or
     * `worse than Normal`. A cell without a number or without an interval is blank, never `nan`.
     *
     * Fixed-width columns, numbers with `kReportPrecision` digits and no locale: the same input gives the same bytes on every platform.
     */
    [[nodiscard]] eastl::string FormatAnalysisReport(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label);

    /**
     * @brief The analysis as a JSON object.
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
     * `actions` is in the order of the text (best EV first). `half` is null for a mean without an interval, `vsBest` / `vsNormal` are
     * null on the row that has no difference to itself. Every number is rounded to `kReportPrecision` digits before it is stored, so the
     * bytes do not depend on how a platform prints a double.
     */
    [[nodiscard]] Json AnalysisToJson(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label);

    /// `AnalysisToJson` as one line (no spaces, no newline).
    [[nodiscard]] eastl::string FormatAnalysisJson(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label);

}  // namespace BECore::Mahjong::Analyzer
