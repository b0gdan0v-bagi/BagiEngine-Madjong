#pragma once

#include <Mahjong/Arena/ArenaLogs.h>
#include <Mahjong/Arena/ArenaTester.h>

namespace BECore::Mahjong::Arena {

    /// The first cell of a `--check` line.
    inline constexpr std::string_view kCheckOk = "ok";
    inline constexpr std::string_view kCheckBad = "bad";

    /**
     * @brief The text of `--check`: one line per file in the order of `logs`, `ok<TAB><name>` or `bad<TAB><name><TAB><reason>`, and
     *        nothing else (a tab or a line break in a name is written as a space). `tools/collect_logs.py` reads exactly this.
     */
    [[nodiscard]] eastl::string FormatCheckLines(const eastl::vector<CheckedLog>& logs);

    /**
     * @brief The text of the corpus summary: files read, replayed and dropped (per reason, and the dropped files with their reasons),
     *        matches complete and abandoned, hands, person and computer seats, seat setups, the platforms, the builds, the length of a
     *        match (hands and actions: smallest, mean, largest) and the computer seats at each level.
     *
     * Fixed-width text, numbers by `Format` with a fixed precision and no locale: the same corpus gives the same bytes everywhere.
     */
    [[nodiscard]] eastl::string FormatCorpusSummary(const LogCorpus& corpus);

    /// The groups of the real-game numbers: the computers met at level `1..kMaxLevel` (the place is the level minus 1), and the table
    /// whose computer seats are not all at one level (`kMixedGroup`).
    inline constexpr size_t kMixedGroup = static_cast<size_t>(kMaxLevel);
    inline constexpr size_t kRealGroupCount = kMixedGroup + 1;

    /// The logged results of one match for the seats of its people, as `ArenaTally` counts a focus player's seat-hands: `hands`,
    /// `wins`, `points` (the person's `deltas`), `drawn`, `declared` (the seat discarded with `declareCalling` in the hand) and
    /// `winScore` (the winner's rounded score). A hand without a `Settlement` (a match given up) is not counted.
    struct RealMatch {
        /// The table had at least one person and one computer seat and at least one hand was settled; else nothing of it is counted.
        bool counted = false;
        size_t group = 0;
        ArenaTally tally;
    };

    /// Reads `events` (a log that replays) for the numbers of the people's seats. The events alone, no positions.
    [[nodiscard]] RealMatch RealGameOfMatch(const eastl::vector<MatchLogEvent>& events);

    /// One group of the real-game table: the pooled counts and, with the **match** as the unit, the mean over the matches of each
    /// match's own number with its 95 % interval.
    struct RealGroup {
        int matches = 0;
        ArenaTally tally;
        MeanInterval winPercent;
        MeanInterval pointsPerHand;
        MeanInterval drawnPercent;
        MeanInterval declaredPercent;
    };
    using RealGame = eastl::array<RealGroup, kRealGroupCount>;

    /// The groups over `matches` (in name order): the numbers of the uncounted ones are left out. The same matches give the same table.
    [[nodiscard]] RealGame MergeRealGame(const eastl::vector<RealMatch>& matches);

    /// Everything the log mode computes beyond the corpus summary.
    struct LogAnalysis {
        /// The agreement of the people with the bots and the differences with the first bot (`ArenaTester`).
        TesterResult tester;
        /// The matches that replayed but whose positions could not be built: one text each (it names the log), in name order. They are
        /// counted here and not in `tester`; their real-game numbers come from the events alone and are counted.
        eastl::vector<eastl::string> notAnalysed;
        RealGame realGame;
    };

    /**
     * @brief Analyses the logs of `logs` that replay (`verdict.ok`), by `bots` (`ValidateTesterBots` is empty).
     *
     * `jobs` threads take whole matches into slots of their own and the slots are merged in name order, so the result, and the text
     * made of it, are the same bytes at any `jobs`. A bot sees a `TableView` and nothing else (`AnalyzeTesterDecision`).
     */
    [[nodiscard]] LogAnalysis AnalyseLogs(const eastl::vector<CheckedLog>& logs, const eastl::vector<ArenaPlayer>& bots, int jobs);

    /// The default of `--differences`: how many differences the report lists.
    inline constexpr int kDefaultDifferences = 20;

    /**
     * @brief The text after the corpus summary: the agreement table per bot, the real-game table by the level of the computers met
     *        and the differences (the first `maxDifferences` of them; the count of all is always printed).
     *
     * Fixed-width text, fixed precision, no locale: the same analysis gives the same bytes everywhere. Only logged numbers are printed;
     * the agreement of testers measures how people play, not what is best.
     */
    [[nodiscard]] eastl::string FormatLogAnalysis(const LogAnalysis& analysis, int maxDifferences);

}  // namespace BECore::Mahjong::Arena
