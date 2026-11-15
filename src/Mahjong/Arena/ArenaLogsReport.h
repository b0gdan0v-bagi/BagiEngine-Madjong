#pragma once

#include <Mahjong/Arena/ArenaLogs.h>
#include <Mahjong/Arena/ArenaTester.h>

namespace BECore::Mahjong::Arena {

    inline constexpr StringView kCheckOk = "ok";
    inline constexpr StringView kCheckBad = "bad";

    /**
     * @brief The text of `--check`: `ok<TAB><name>` or `bad<TAB><name><TAB><reason>` per file, nothing else.
     *
     * **`tools/collect_logs.py` relies on these bytes:** do not change the format.
     */
    [[nodiscard]] String FormatCheckLines(const Vector<CheckedLog>& logs);

    /**
     * @brief The text of the corpus summary (`LogCorpus`).
     *
     * Fixed-width text, fixed precision, no locale: the same corpus gives the same bytes everywhere.
     */
    [[nodiscard]] String FormatCorpusSummary(const LogCorpus& corpus);

    /// The groups of the real-game numbers: the level of the computers met (the place is the level minus 1), and `kMixedGroup` for a
    /// table whose computer seats differ.
    inline constexpr size_t kMixedGroup = static_cast<size_t>(kMaxLevel);
    inline constexpr size_t kRealGroupCount = kMixedGroup + 1;

    /// The logged results of one match for the seats of its people, counted as `ArenaTally` counts a focus player's seat-hands (`declared`
    /// is the seat's logged `declareCalling` in the hand). A hand without a `Settlement` is not counted.
    struct RealMatch {
        /// Needs a person, a computer seat and a settled hand.
        bool counted = false;
        size_t group = 0;
        ArenaTally tally;
    };

    /// From the events alone, so it still counts a match whose positions cannot be built.
    [[nodiscard]] RealMatch RealGameOfMatch(const Vector<MatchLogEvent>& events);

    /// The pooled counts and, with the **match** as the unit, the mean over the matches of each match's own share with its interval.
    struct RealGroup {
        int matches = 0;
        ArenaTally tally;
        MeanInterval winPercent;
        MeanInterval pointsPerHand;
        MeanInterval drawnPercent;
        MeanInterval declaredPercent;
    };
    using RealGame = Array<RealGroup, kRealGroupCount>;

    /// `matches` in name order; the uncounted ones are left out.
    [[nodiscard]] RealGame MergeRealGame(const Vector<RealMatch>& matches);

    struct LogAnalysis {
        TesterResult tester;
        /// The matches that replayed but whose positions could not be built, one text each, in name order: counted here and not in
        /// `tester`, while their real-game numbers still count.
        Vector<String> notAnalysed;
        RealGame realGame;
    };

    /**
     * @brief Analyses the logs that replay (`verdict.ok`) by `bots` (`ValidateTesterBots` is empty).
     *
     * `jobs` threads take whole matches into slots of their own, merged in name order: the same bytes at any `jobs`. The match is the
     * unit of the intervals.
     */
    [[nodiscard]] LogAnalysis AnalyseLogs(const Vector<CheckedLog>& logs, const Vector<ArenaPlayer>& bots, int jobs);

    inline constexpr int kDefaultDifferences = 20;

    /**
     * @brief The text after the corpus summary: the agreement per bot, the real-game table and the first `maxDifferences` differences
     *        (the count of all is always printed).
     *
     * Fixed-width text, fixed precision, no locale: the same analysis gives the same bytes everywhere. The agreement of testers
     * measures how people play, not what is best.
     */
    [[nodiscard]] String FormatLogAnalysis(const LogAnalysis& analysis, int maxDifferences);

}  // namespace BECore::Mahjong::Arena
