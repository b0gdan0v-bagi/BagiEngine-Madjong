#include "ArenaLogs.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogJson.h>

namespace BECore::Mahjong::Arena {

    namespace {

        /// A reason is one cell of a tab separated line: no tab and no line break in it.
        eastl::string OneLine(eastl::string text) {
            for (char& letter : text) {
                if (letter == '\t' || letter == '\n' || letter == '\r') {
                    letter = ' ';
                }
            }
            return text;
        }

        LogVerdict Rejected(LogProblem problem, const eastl::string& reason) {
            LogVerdict verdict;
            verdict.problem = problem;
            verdict.reason = OneLine(reason);
            return verdict;
        }

        void Count(CountList& list, const eastl::string& key) {
            for (auto& entry : list) {
                if (entry.first == key) {
                    ++entry.second;
                    return;
                }
            }
            list.emplace_back(key, 1);
        }

        Spread SpreadOf(const eastl::vector<int>& values) {
            Spread spread;
            if (values.empty()) {
                return spread;
            }
            int64_t sum = 0;
            spread.min = values.front();
            spread.max = values.front();
            for (const int value : values) {
                spread.min = eastl::min(spread.min, value);
                spread.max = eastl::max(spread.max, value);
                sum += value;
            }
            spread.mean = static_cast<double>(sum) / static_cast<double>(values.size());
            return spread;
        }

        const MatchLogEvent* StartOf(const eastl::vector<MatchLogEvent>& events) {
            return !events.empty() && events.front().kind == MatchLogKind::Start ? &events.front() : nullptr;
        }

    }  // namespace

    LogVerdict CheckMatchLog(std::string_view text) {
        std::expected<eastl::vector<MatchLogEvent>, eastl::string> parsed = ParseMatchLog(text);
        if (!parsed.has_value()) {
            return Rejected(LogProblem::Unreadable, "unreadable: " + parsed.error());
        }
        Match match;
        if (const ReplayStatus replayed = ReplayMatchLog(*parsed, match); !replayed.has_value()) {
            return Rejected(LogProblem::DoesNotReplay, Format("does not replay: event {} ({})", replayed.error().index, EnumUtils<ReplayProblem>::ToString(replayed.error().problem)));
        }
        LogVerdict verdict;
        verdict.ok = true;
        verdict.complete = !parsed->empty() && parsed->back().kind == MatchLogKind::End;
        verdict.events = eastl::move(*parsed);
        return verdict;
    }

    std::expected<eastl::vector<CheckedLog>, eastl::string> ReadLogs(const LogSource& source, std::string_view path) {
        std::expected<eastl::vector<LogFile>, eastl::string> files = source(path);
        if (!files.has_value()) {
            return std::unexpected(files.error());
        }
        eastl::sort(files->begin(), files->end(), [](const LogFile& a, const LogFile& b) { return a.name != b.name ? a.name < b.name : a.text < b.text; });
        eastl::vector<CheckedLog> logs;
        logs.reserve(files->size());
        for (const LogFile& file : *files) {
            logs.push_back({file.name, CheckMatchLog(std::string_view(file.text.data(), file.text.size()))});
        }
        return logs;
    }

    LogCorpus SummariseLogs(const eastl::vector<CheckedLog>& logs) {
        LogCorpus corpus;
        eastl::vector<int> handCounts;
        eastl::vector<int> actionCounts;
        for (const CheckedLog& log : logs) {
            ++corpus.files;
            if (!log.verdict.ok) {
                ++corpus.dropped[static_cast<size_t>(log.verdict.problem)];
                corpus.droppedFiles.emplace_back(log.name, log.verdict.reason);
                continue;
            }
            ++corpus.replayed;
            ++(log.verdict.complete ? corpus.complete : corpus.abandoned);
            int hands = 0;
            int actions = 0;
            for (const MatchLogEvent& event : log.verdict.events) {
                hands += event.kind == MatchLogKind::Settlement ? 1 : 0;
                actions += event.kind == MatchLogKind::Action ? 1 : 0;
            }
            corpus.hands += hands;
            handCounts.push_back(hands);
            actionCounts.push_back(actions);
            const MatchLogEvent* start = StartOf(log.verdict.events);
            ASSERT(start != nullptr);  // a log that replays begins with its Start
            if (start == nullptr) {
                continue;
            }
            for (size_t seat = 0; seat < start->seats.size(); ++seat) {
                if (start->seats[seat] == SeatKind::Person) {
                    ++corpus.personSeats;
                } else {
                    ++corpus.computerSeats;
                    ++corpus.computerLevels[static_cast<size_t>(LevelNumber(start->levels[seat]) - kMinLevel)];
                }
            }
            Count(corpus.setups, SeatsToLetters(start->seats));
            ++corpus.platforms[static_cast<size_t>(start->origin.platform)];
            Count(corpus.builds, start->origin.build.empty() ? eastl::string(kNoBuild.data(), kNoBuild.size()) : start->origin.build);
        }
        eastl::sort(corpus.setups.begin(), corpus.setups.end());
        eastl::sort(corpus.builds.begin(), corpus.builds.end());
        corpus.matchHands = SpreadOf(handCounts);
        corpus.matchActions = SpreadOf(actionCounts);
        return corpus;
    }

}  // namespace BECore::Mahjong::Arena
