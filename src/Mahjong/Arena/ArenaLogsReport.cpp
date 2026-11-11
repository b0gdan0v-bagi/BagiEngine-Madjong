#include "ArenaLogsReport.h"

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kLabelWidth = 18;
        constexpr int kLevelWidth = 7;
        constexpr int kCountWidth = 7;
        constexpr int kMeanPrecision = 2;
        constexpr std::string_view kIndent = "  ";

        eastl::string OneCell(const eastl::string& text) {
            eastl::string cell = text;
            for (char& letter : cell) {
                if (letter == '\t' || letter == '\n' || letter == '\r') {
                    letter = ' ';
                }
            }
            return cell;
        }

        void AddLine(eastl::string& text, std::string_view label, const eastl::string& value) {
            text.append(Format("{:<{}}{}\n", label, kLabelWidth, value));
        }

        eastl::string Joined(const CountList& list) {
            eastl::string text;
            for (const auto& [key, count] : list) {
                text.append(Format("{}{} {}", text.empty() ? "" : ", ", key, count));
            }
            return text.empty() ? eastl::string("-") : text;
        }

        eastl::string SpreadText(const Spread& spread) {
            return Format("min {}, mean {:.{}f}, max {}", spread.min, spread.mean, kMeanPrecision, spread.max);
        }

        eastl::string DroppedText(const LogCorpus& corpus) {
            eastl::string text;
            for (size_t problem = 0; problem < corpus.dropped.size(); ++problem) {
                text.append(Format("{}{} {}", problem == 0 ? "" : ", ", EnumUtils<LogProblem>::ToString(static_cast<LogProblem>(problem)), corpus.dropped[problem]));
            }
            return text;
        }

        void AddPlatforms(eastl::string& text, const LogCorpus& corpus) {
            constexpr eastl::array<MatchPlatform, 3> kOrder{MatchPlatform::Desktop, MatchPlatform::Web, MatchPlatform::Unknown};
            constexpr eastl::array<std::string_view, 3> kNames{"desktop", "web", "unknown"};
            static_assert(kOrder.size() == EnumUtils<MatchPlatform>::Count());
            eastl::string value;
            for (size_t index = 0; index < kOrder.size(); ++index) {
                value.append(Format("{}{} {}", index == 0 ? "" : ", ", kNames[index], corpus.platforms[static_cast<size_t>(kOrder[index])]));
            }
            AddLine(text, "platforms", value);
        }

    }  // namespace

    eastl::string FormatCheckLines(const eastl::vector<CheckedLog>& logs) {
        eastl::string text;
        for (const CheckedLog& log : logs) {
            if (log.verdict.ok) {
                text.append(Format("{}\t{}\n", kCheckOk, OneCell(log.name)));
            } else {
                text.append(Format("{}\t{}\t{}\n", kCheckBad, OneCell(log.name), OneCell(log.verdict.reason)));
            }
        }
        return text;
    }

    eastl::string FormatCorpusSummary(const LogCorpus& corpus) {
        eastl::string text = "Match logs\n";
        AddLine(text, "files read", Format("{}", corpus.files));
        AddLine(text, "replayed", Format("{}", corpus.replayed));
        AddLine(text, "dropped", Format("{} ({})", corpus.files - corpus.replayed, DroppedText(corpus)));
        for (const auto& [name, reason] : corpus.droppedFiles) {
            text.append(Format("{}{}: {}\n", kIndent, OneCell(name), OneCell(reason)));
        }
        AddLine(text, "matches", Format("complete {}, abandoned {}", corpus.complete, corpus.abandoned));
        AddLine(text, "hands", Format("{}", corpus.hands));
        AddLine(text, "seats", Format("person {}, computer {}", corpus.personSeats, corpus.computerSeats));
        AddLine(text, "seat setups", Joined(corpus.setups));
        AddPlatforms(text, corpus);
        AddLine(text, "builds", Joined(corpus.builds));
        AddLine(text, "match hands", SpreadText(corpus.matchHands));
        AddLine(text, "match actions", SpreadText(corpus.matchActions));
        text.append("\ncomputer seats by level\n");
        text.append(Format("{:<{}}{:>{}}\n", "level", kLevelWidth, "seats", kCountWidth));
        for (size_t level = 0; level < corpus.computerLevels.size(); ++level) {
            text.append(Format("{:<{}}{:>{}}\n", level + static_cast<size_t>(kMinLevel), kLevelWidth, corpus.computerLevels[level], kCountWidth));
        }
        return text;
    }

}  // namespace BECore::Mahjong::Arena
