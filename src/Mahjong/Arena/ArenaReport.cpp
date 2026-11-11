#include "ArenaReport.h"

namespace BECore::Mahjong::Arena {

    namespace {

        /// The digits after the point of every number of the report.
        constexpr int kPrecision = 2;
        constexpr int kNameMinWidth = 22;
        constexpr int kHandsWidth = 7;
        /// "100.00 +- 100.00", "-1234.56 +- 123.45".
        constexpr int kWinsCellWidth = 16;
        constexpr int kPointsCellWidth = 20;
        constexpr int kPercentWidth = 10;
        constexpr int kScoreWidth = 14;
        /// "self-draw % of wins", "-1234.56 +- 123.45", "points/lost hand".
        constexpr int kSelfDrawWidth = 19;
        constexpr int kStandingCellWidth = 18;
        constexpr int kLostPointsWidth = 16;
        constexpr int kDecisionsWidth = 12;
        constexpr int kMicrosecondsWidth = 15;
        constexpr int kShareMinWidth = 10;
        constexpr int kKindMinWidth = 24;
        constexpr const char* kColumnGap = "  ";
        constexpr const char* kIndent = "  ";
        constexpr std::string_view kSignificantWord = "significant";
        constexpr int kSignificantWidth = static_cast<int>(kSignificantWord.size());
        constexpr std::string_view kOthersLabel = "others (per seat)";
        constexpr double kNanosecondsPerMicrosecond = 1000.0;
        constexpr size_t kOthersSeats = kSeatCount - 1;

        double PercentOf(int64_t part, int64_t whole) {
            return whole > 0 ? kPercent * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
        }

        double AverageOf(int64_t sum, int64_t count) {
            return count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
        }

        /// Ends the line: the spaces at its right end go, a newline comes.
        void EndLine(eastl::string& text, size_t lineStart) {
            size_t end = text.size();
            while (end > lineStart && text[end - 1] == ' ') {
                --end;
            }
            text.resize(end);
            text.push_back('\n');
        }

        void AddLeft(eastl::string& text, const eastl::string& cell, int width) {
            text.append(Format("{:<{}}", cell, width));
        }

        void AddRight(eastl::string& text, const eastl::string& cell, int width) {
            text.append(Format("{}{:>{}}", kColumnGap, cell, width));
        }

        eastl::string Plain(double value) {
            return Format("{:.{}f}", value, kPrecision);
        }

        eastl::string WithInterval(const MeanInterval& interval) {
            if (!interval.hasInterval) {
                return Plain(interval.mean);
            }
            return Format("{:.{}f} +- {:.{}f}", interval.mean, kPrecision, interval.half, kPrecision);
        }

        eastl::string SignedWithInterval(const MeanInterval& interval) {
            if (!interval.hasInterval) {
                return Format("{:+.{}f}", interval.mean, kPrecision);
            }
            return Format("{:+.{}f} +- {:.{}f}", interval.mean, kPrecision, interval.half, kPrecision);
        }

        int NameWidth(const ArenaResult& result) {
            int width = kNameMinWidth;
            for (const FocusResult& focus : result.focus) {
                width = eastl::max(width, static_cast<int>(focus.player.name.size()));
            }
            return width;
        }

        /// The rule of the interval, and what it needs.
        eastl::string IntervalRule(const ArenaResult& result) {
            if (result.count < static_cast<int>(kMinSeedsForInterval)) {
                return Format("{} seed: no interval (it needs at least {} seeds)", result.count, kMinSeedsForInterval);
            }
            return Format("+- is {} standard errors over the seeds (a 95 percent interval); the {} rotations of a seed play one wall, so a seed is one sample", kIntervalZ, kSeatCount);
        }

        void AddHeader(eastl::string& text, const ArenaResult& result) {
            const int64_t last = static_cast<int64_t>(result.first) + result.count - 1;
            text.append(Format("Bot arena: seeds {} .. {} ({} seeds, {} hands per focus player)\n", result.first, last, result.count, result.count * static_cast<int>(kSeatCount)));
            text.append("focus: ");
            for (size_t index = 0; index < result.focus.size(); ++index) {
                text.append(Format("{}{}", index == 0 ? "" : ", ", result.focus[index].player.name));
            }
            text.append(Format("; others: {}\n", result.others.name));
            text.append(Format("{}\n", IntervalRule(result)));
        }

        void AddMainTable(eastl::string& text, const ArenaResult& result, int nameWidth) {
            size_t start = text.size();
            AddLeft(text, "player", nameWidth);
            AddRight(text, "hands", kHandsWidth);
            AddRight(text, "wins %", kWinsCellWidth);
            AddRight(text, "points/hand", kPointsCellWidth);
            AddRight(text, "drawn %", kPercentWidth);
            AddRight(text, "declared %", kPercentWidth);
            AddRight(text, "avg win score", kScoreWidth);
            EndLine(text, start);
            for (const FocusResult& focus : result.focus) {
                start = text.size();
                AddLeft(text, focus.player.name, nameWidth);
                AddRight(text, Format("{}", focus.focus.hands), kHandsWidth);
                AddRight(text, WithInterval(MetricInterval(focus, SeedMetric::WinPercent)), kWinsCellWidth);
                AddRight(text, WithInterval(MetricInterval(focus, SeedMetric::PointsPerHand)), kPointsCellWidth);
                AddRight(text, Plain(PercentOf(focus.focus.drawn, focus.focus.hands)), kPercentWidth);
                AddRight(text, Plain(PercentOf(focus.focus.declared, focus.focus.hands)), kPercentWidth);
                AddRight(text, Plain(AverageOf(focus.focus.winScore, focus.focus.wins)), kScoreWidth);
                EndLine(text, start);

                const ArenaTally& others = focus.others;
                start = text.size();
                AddLeft(text, Format("{}{}", kIndent, kOthersLabel), nameWidth);
                AddRight(text, Format("{}", others.hands / static_cast<int64_t>(kOthersSeats)), kHandsWidth);
                AddRight(text, Plain(PercentOf(others.wins, others.hands)), kWinsCellWidth);
                AddRight(text, Plain(AverageOf(others.points, others.hands)), kPointsCellWidth);
                AddRight(text, Plain(PercentOf(others.drawn, others.hands)), kPercentWidth);
                AddRight(text, Plain(PercentOf(others.declared, others.hands)), kPercentWidth);
                AddRight(text, Plain(AverageOf(others.winScore, others.wins)), kScoreWidth);
                EndLine(text, start);
            }
        }

        /// The table "how the hands end": per focus player and for its others, how often the seat gave the win, how often a win of the
        /// seat was a self-draw, and what the seat's hands cost it when another seat won. The columns with intervals are the two that
        /// have a per-seed value (`GaveWinPercent`, `StandingScore`); the others are ratios of the totals.
        void AddStyleTable(eastl::string& text, const ArenaResult& result, int nameWidth) {
            text.append("\nhow the hands end\n");
            size_t start = text.size();
            AddLeft(text, "player", nameWidth);
            AddRight(text, "gave win %", kWinsCellWidth);
            AddRight(text, "self-draw % of wins", kSelfDrawWidth);
            AddRight(text, "standing score", kStandingCellWidth);
            AddRight(text, "points/lost hand", kLostPointsWidth);
            EndLine(text, start);
            for (const FocusResult& focus : result.focus) {
                start = text.size();
                AddLeft(text, focus.player.name, nameWidth);
                AddRight(text, WithInterval(MetricInterval(focus, SeedMetric::GaveWinPercent)), kWinsCellWidth);
                AddRight(text, Plain(PercentOf(focus.focus.selfDraws, focus.focus.wins)), kSelfDrawWidth);
                AddRight(text, WithInterval(MetricInterval(focus, SeedMetric::StandingScore)), kStandingCellWidth);
                AddRight(text, Plain(AverageOf(focus.focus.lostPoints, focus.focus.lost)), kLostPointsWidth);
                EndLine(text, start);

                const ArenaTally& others = focus.others;
                start = text.size();
                AddLeft(text, Format("{}{}", kIndent, kOthersLabel), nameWidth);
                AddRight(text, Plain(PercentOf(others.gaveWin, others.hands)), kWinsCellWidth);
                AddRight(text, Plain(PercentOf(others.selfDraws, others.wins)), kSelfDrawWidth);
                AddRight(text, Plain(AverageOf(others.standingScore, others.lost)), kStandingCellWidth);
                AddRight(text, Plain(AverageOf(others.lostPoints, others.lost)), kLostPointsWidth);
                EndLine(text, start);
            }
            text.append("gave win %: the hands another seat won on the seat's discard (or by robbing its kong), of all its hands\n");
            text.append("lost hand: a hand another seat won; standing score: the seat's mean score over its lost hands; points/lost hand: its mean change of points over them\n");
        }

        void AddPairedCell(eastl::string& text, const MeanInterval& interval, int cellWidth) {
            AddRight(text, SignedWithInterval(interval), cellWidth);
            AddRight(text, IsSignificant(interval) ? Format("{}", kSignificantWord) : eastl::string(), kSignificantWidth);
        }

        void AddPairedTable(eastl::string& text, const ArenaResult& result, int nameWidth) {
            const FocusResult& baseline = result.focus.front();
            text.append(Format("\npaired difference against {} (the same seeds)\n", baseline.player.name));
            size_t start = text.size();
            AddLeft(text, "player", nameWidth);
            AddRight(text, "points/hand", kPointsCellWidth);
            AddRight(text, "", kSignificantWidth);
            AddRight(text, "wins %", kWinsCellWidth);
            EndLine(text, start);
            for (size_t index = 1; index < result.focus.size(); ++index) {
                const FocusResult& focus = result.focus[index];
                start = text.size();
                AddLeft(text, focus.player.name, nameWidth);
                AddPairedCell(text, PairedDifference(focus, baseline, SeedMetric::PointsPerHand), kPointsCellWidth);
                AddPairedCell(text, PairedDifference(focus, baseline, SeedMetric::WinPercent), kWinsCellWidth);
                EndLine(text, start);
            }
        }

        void AddStylePairedTable(eastl::string& text, const ArenaResult& result, int nameWidth) {
            const FocusResult& baseline = result.focus.front();
            text.append(Format("\npaired difference against {} (the same seeds), how the hands end\n", baseline.player.name));
            size_t start = text.size();
            AddLeft(text, "player", nameWidth);
            AddRight(text, "gave win %", kWinsCellWidth);
            AddRight(text, "", kSignificantWidth);
            AddRight(text, "standing score", kStandingCellWidth);
            EndLine(text, start);
            for (size_t index = 1; index < result.focus.size(); ++index) {
                const FocusResult& focus = result.focus[index];
                start = text.size();
                AddLeft(text, focus.player.name, nameWidth);
                AddPairedCell(text, PairedDifference(focus, baseline, SeedMetric::GaveWinPercent), kWinsCellWidth);
                AddPairedCell(text, PairedDifference(focus, baseline, SeedMetric::StandingScore), kStandingCellWidth);
                EndLine(text, start);
            }
        }

        void AddKindTable(eastl::string& text, const FocusResult& focus) {
            eastl::vector<size_t> kinds;
            for (size_t kind = 0; kind < kScoreKindCount; ++kind) {
                if (focus.focus.winsByKind[kind] > 0 || focus.others.winsByKind[kind] > 0) {
                    kinds.push_back(kind);
                }
            }
            text.append(Format("\nscore lines in the won hands, % of the won hands ({}: {} wins, others: {} wins)\n", focus.player.name, focus.focus.wins, focus.others.wins));
            if (kinds.empty()) {
                text.append("no won hand\n");
                return;
            }
            int kindWidth = kKindMinWidth;
            for (const size_t kind : kinds) {
                kindWidth = eastl::max(kindWidth, static_cast<int>(EnumUtils<ScoreKind>::ToString(static_cast<ScoreKind>(kind)).size()));
            }
            const int focusWidth = eastl::max(kShareMinWidth, static_cast<int>(focus.player.name.size()));
            size_t start = text.size();
            AddLeft(text, "line", kindWidth);
            AddRight(text, focus.player.name, focusWidth);
            AddRight(text, "others", kShareMinWidth);
            EndLine(text, start);
            for (const size_t kind : kinds) {
                start = text.size();
                AddLeft(text, Format("{}", EnumUtils<ScoreKind>::ToString(static_cast<ScoreKind>(kind))), kindWidth);
                AddRight(text, Plain(PercentOf(focus.focus.winsByKind[kind], focus.focus.wins)), focusWidth);
                AddRight(text, Plain(PercentOf(focus.others.winsByKind[kind], focus.others.wins)), kShareMinWidth);
                EndLine(text, start);
            }
        }

        void AddTimingRow(eastl::string& text, const eastl::string& name, const ArenaTally& tally, int nameWidth) {
            const size_t start = text.size();
            AddLeft(text, name, nameWidth);
            AddRight(text, Format("{}", tally.decisions), kDecisionsWidth);
            AddRight(text, Plain(AverageOf(tally.nanoseconds, tally.decisions) / kNanosecondsPerMicrosecond), kMicrosecondsWidth);
            EndLine(text, start);
        }

        void AddTimingTable(eastl::string& text, const ArenaResult& result, int nameWidth) {
            text.append("\ntime per decision (wall clock, not deterministic)\n");
            size_t start = text.size();
            AddLeft(text, "player", nameWidth);
            AddRight(text, "decisions", kDecisionsWidth);
            AddRight(text, "us per decision", kMicrosecondsWidth);
            EndLine(text, start);
            for (const FocusResult& focus : result.focus) {
                AddTimingRow(text, focus.player.name, focus.focus, nameWidth);
                AddTimingRow(text, Format("{}others ({} seats)", kIndent, kOthersSeats), focus.others, nameWidth);
            }
        }

    }  // namespace

    eastl::string FormatArenaReport(const ArenaResult& result, const ArenaReportOptions& options) {
        eastl::string text;
        const int nameWidth = NameWidth(result);
        AddHeader(text, result);
        text.append("\n");
        AddMainTable(text, result, nameWidth);
        AddStyleTable(text, result, nameWidth);
        if (result.focus.size() > 1) {
            AddPairedTable(text, result, nameWidth);
            AddStylePairedTable(text, result, nameWidth);
        }
        if (options.kinds) {
            for (const FocusResult& focus : result.focus) {
                AddKindTable(text, focus);
            }
        }
        if (result.timing) {
            AddTimingTable(text, result, nameWidth);
        }
        return text;
    }

}  // namespace BECore::Mahjong::Arena
