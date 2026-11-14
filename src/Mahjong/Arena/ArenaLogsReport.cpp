#include "ArenaLogsReport.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Analyzer/AnalyzerReport.h>
#include <Mahjong/MatchLogJson.h>
#include <thread>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kLabelWidth = 18;
        constexpr int kLevelWidth = 7;
        constexpr int kCountWidth = 7;
        constexpr int kMeanPrecision = 2;
        constexpr StringView kIndent = "  ";
        constexpr int kPrecision = 2;
        constexpr int kBotNameMinWidth = 22;
        constexpr int kPhaseWidth = 14;
        constexpr int kDecisionsWidth = 10;
        constexpr int kShareWidth = 16;
        constexpr int kGroupWidth = 7;
        constexpr int kMatchesWidth = 8;
        constexpr int kSeatHandsWidth = 11;
        constexpr int kWinsCellWidth = 16;
        constexpr int kPointsCellWidth = 20;
        constexpr int kPercentCellWidth = 16;
        constexpr int kScoreWidth = 14;
        constexpr StringView kMixedLabel = "mixed";
        constexpr StringView kColumnGap = "  ";
        constexpr StringView kNone = "-";

        void AddLine(String& text, StringView label, const String& value) {
            text.append(Format("{:<{}}{}\n", label, kLabelWidth, value));
        }

        String Joined(const CountList& list) {
            String text;
            for (const auto& [key, count] : list) {
                text.append(Format("{}{} {}", text.empty() ? "" : ", ", key, count));
            }
            return text.empty() ? String("-") : text;
        }

        String SpreadText(const Spread& spread) {
            return Format("min {}, mean {:.{}f}, max {}", spread.min, spread.mean, kMeanPrecision, spread.max);
        }

        String DroppedText(const LogCorpus& corpus) {
            String text;
            for (size_t problem = 0; problem < corpus.dropped.size(); ++problem) {
                text.append(Format("{}{} {}", problem == 0 ? "" : ", ", EnumUtils<LogProblem>::ToString(static_cast<LogProblem>(problem)), corpus.dropped[problem]));
            }
            return text;
        }

        void AddPlatforms(String& text, const LogCorpus& corpus) {
            constexpr Array<MatchPlatform, 3> kOrder{MatchPlatform::Desktop, MatchPlatform::Web, MatchPlatform::Unknown};
            static_assert(kOrder.size() == EnumUtils<MatchPlatform>::Count());
            String value;
            for (size_t index = 0; index < kOrder.size(); ++index) {
                value.append(Format("{}{} {}", index == 0 ? "" : ", ", PlatformName(kOrder[index]), corpus.platforms[static_cast<size_t>(kOrder[index])]));
            }
            AddLine(text, "platforms", value);
        }

        double PercentOf(int64_t part, int64_t whole) {
            return whole > 0 ? kPercent * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
        }

        String Plain(double value) {
            return Format("{:.{}f}", value, kPrecision);
        }

        String WithInterval(const MeanInterval& interval) {
            if (interval.n == 0) {
                return String(kNone.data(), kNone.size());
            }
            if (!interval.hasInterval) {
                return Plain(interval.mean);
            }
            return Format("{:.{}f} +- {:.{}f}", interval.mean, kPrecision, interval.half, kPrecision);
        }

        void AddCell(String& text, const String& cell, int width) {
            text.append(Format("{}{:>{}}", kColumnGap, cell, width));
        }

        /// The people's seats of a table, and its computer seats' levels; empty `levels` when it has none.
        struct Table {
            Array<bool, kSeatCount> person{};
            int persons = 0;
            Vector<int> levels;
        };

        Table TableOf(const MatchLogEvent& start) {
            Table table;
            for (size_t seat = 0; seat < start.seats.size(); ++seat) {
                if (start.seats[seat] == SeatKind::Person) {
                    table.person[seat] = true;
                    ++table.persons;
                } else {
                    table.levels.push_back(LevelNumber(start.levels[seat]));
                }
            }
            return table;
        }

        /// The place of a table in the groups: its one level, or the mixed row.
        size_t GroupOf(const Vector<int>& levels) {
            const bool one = eastl::all_of(levels.begin(), levels.end(), [&](int level) { return level == levels.front(); });
            return one ? static_cast<size_t>(levels.front() - kMinLevel) : kMixedGroup;
        }

        void AddSeatHand(ArenaTally& tally, const HandSettlement& settlement, Seat seat, bool declared) {
            ++tally.hands;
            tally.points += settlement.deltas[static_cast<size_t>(seat)];
            tally.drawn += settlement.kind == ResultKind::Drawn ? 1 : 0;
            tally.declared += declared ? 1 : 0;
            if (settlement.winner == seat) {
                ++tally.wins;
                tally.winScore += settlement.scores[static_cast<size_t>(seat)];
            }
        }

        /// One match of the corpus, once analysed: the same slot is written by one thread only.
        struct MatchSlot {
            std::expected<TesterMatch, String> tester;
            RealMatch real;
        };

        void AnalyseMatch(const CheckedLog& log, const Vector<ArenaPlayer>& bots, MatchSlot& slot) {
            slot.real = RealGameOfMatch(log.verdict.events);
            slot.tester = AnalyzeTesterMatch(log.name, log.verdict.events, bots);
        }

        String BotRows(const BotAgreement& bot, int nameWidth) {
            String text;
            for (size_t row = 0; row < kTesterRowCount; ++row) {
                const StringView phase = row == kTesterBothRow ? "both" : (row == static_cast<size_t>(TesterPhase::DiscardTurn) ? "discard turn" : "claim window");
                text.append(Format("{:<{}}", row == 0 ? bot.player.name.c_str() : "", nameWidth));
                text.append(Format("{}{:<{}}", kColumnGap, phase, kPhaseWidth));
                AddCell(text, Format("{}", bot.decisions[row]), kDecisionsWidth);
                AddCell(text, WithInterval(bot.share[row]), kShareWidth);
                text.push_back('\n');
            }
            return text;
        }

        void AddAgreement(String& text, const LogAnalysis& analysis) {
            const TesterResult& tester = analysis.tester;
            text.append("\nAgreement of the people with the bots (the logged action is the same as the bot's choice)\n");
            text.append(Format("matches analysed {}, not analysed {}\n", tester.matches, analysis.notAnalysed.size()));
            for (const String& reason : analysis.notAnalysed) {
                text.append(Format("{}{}\n", kIndent, OneLine(reason)));
            }
            text.append(Format("+- is {} standard errors over the matches (a 95 percent interval); the match is the sample, not the decision\n", kIntervalZ));
            text.append("Testers are not experts: the share says how people play, not what is best\n");
            int nameWidth = kBotNameMinWidth;
            for (const BotAgreement& bot : tester.bots) {
                nameWidth = eastl::max(nameWidth, static_cast<int>(bot.player.name.size()));
            }
            text.append(Format("{:<{}}{}{:<{}}", "bot", nameWidth, kColumnGap, "phase", kPhaseWidth));
            AddCell(text, "decisions", kDecisionsWidth);
            AddCell(text, "equal %", kShareWidth);
            text.push_back('\n');
            for (const BotAgreement& bot : tester.bots) {
                text.append(BotRows(bot, nameWidth));
            }
        }

        String GroupLabel(size_t group) {
            return group == kMixedGroup ? String(kMixedLabel.data(), kMixedLabel.size()) : Format("{}", group + static_cast<size_t>(kMinLevel));
        }

        void AddRealGame(String& text, const LogAnalysis& analysis) {
            text.append("\nReal games by the level of the computers met (the logged results of the people's seat-hands at tables with a computer)\n");
            text.append("A table whose computers are not all at one level is `mixed`; a hand not finished is not counted; the match is the unit of the intervals\n");
            text.append(Format("{:<{}}", "level", kGroupWidth));
            AddCell(text, "matches", kMatchesWidth);
            AddCell(text, "seat-hands", kSeatHandsWidth);
            AddCell(text, "wins %", kWinsCellWidth);
            AddCell(text, "points/hand", kPointsCellWidth);
            AddCell(text, "drawn %", kPercentCellWidth);
            AddCell(text, "declared %", kPercentCellWidth);
            AddCell(text, "avg win score", kScoreWidth);
            text.push_back('\n');
            bool any = false;
            for (size_t group = 0; group < kRealGroupCount; ++group) {
                const RealGroup& row = analysis.realGame[group];
                if (row.matches == 0) {
                    continue;
                }
                any = true;
                text.append(Format("{:<{}}", GroupLabel(group), kGroupWidth));
                AddCell(text, Format("{}", row.matches), kMatchesWidth);
                AddCell(text, Format("{}", row.tally.hands), kSeatHandsWidth);
                AddCell(text, WithInterval(row.winPercent), kWinsCellWidth);
                AddCell(text, WithInterval(row.pointsPerHand), kPointsCellWidth);
                AddCell(text, WithInterval(row.drawnPercent), kPercentCellWidth);
                AddCell(text, WithInterval(row.declaredPercent), kPercentCellWidth);
                AddCell(text, row.tally.wins > 0 ? Plain(static_cast<double>(row.tally.winScore) / static_cast<double>(row.tally.wins)) : String(kNone.data(), kNone.size()), kScoreWidth);
                text.push_back('\n');
            }
            if (!any) {
                text.append("no match with a person and a computer seat was settled\n");
            }
        }

        void AddDifferences(String& text, const LogAnalysis& analysis, int maxDifferences) {
            const TesterResult& tester = analysis.tester;
            const size_t shown = eastl::min(tester.differences.size(), static_cast<size_t>(eastl::max(0, maxDifferences)));
            text.append(Format("\nDifferences from {}: {} in all, {} listed (log order)\n", tester.bots.empty() ? "-" : tester.bots.front().player.name.c_str(), tester.differences.size(), shown));
            for (size_t index = 0; index < shown; ++index) {
                const TesterDifference& difference = tester.differences[index];
                text.append(Format("{}{} hand {} action {} seat {}: logged {}, bot chose {}\n", kIndent, OneLine(difference.log), difference.hand, difference.actionIndex, difference.seat,
                                   Analyzer::ActionNotation(difference.logged).c_str(), Analyzer::ActionNotation(difference.chosen).c_str()));
                text.append(Format("{}{}--log {} --hand {} --action {}\n", kIndent, kIndent, OneLine(difference.log), difference.hand, difference.actionIndex));
            }
        }

    }  // namespace

    String FormatCheckLines(const Vector<CheckedLog>& logs) {
        String text;
        for (const CheckedLog& log : logs) {
            if (log.verdict.ok) {
                text.append(Format("{}\t{}\n", kCheckOk, OneLine(log.name)));
            } else {
                text.append(Format("{}\t{}\t{}\n", kCheckBad, OneLine(log.name), OneLine(log.verdict.reason)));
            }
        }
        return text;
    }

    String FormatCorpusSummary(const LogCorpus& corpus) {
        String text = "Match logs\n";
        AddLine(text, "files read", Format("{}", corpus.files));
        AddLine(text, "replayed", Format("{}", corpus.replayed));
        AddLine(text, "dropped", Format("{} ({})", corpus.files - corpus.replayed, DroppedText(corpus)));
        for (const auto& [name, reason] : corpus.droppedFiles) {
            text.append(Format("{}{}: {}\n", kIndent, OneLine(name), OneLine(reason)));
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

    RealMatch RealGameOfMatch(const Vector<MatchLogEvent>& events) {
        RealMatch real;
        if (events.empty() || events.front().kind != MatchLogKind::Start) {
            return real;
        }
        const Table table = TableOf(events.front());
        if (table.persons == 0 || table.levels.empty()) {
            return real;
        }
        real.group = GroupOf(table.levels);
        Array<bool, kSeatCount> declared{};
        for (const MatchLogEvent& event : events) {
            if (event.kind == MatchLogKind::Action && event.action.kind == ActionKind::Discard && event.action.declareCalling && event.seat != kNoSeat) {
                declared[static_cast<size_t>(event.seat)] = true;
            } else if (event.kind == MatchLogKind::Settlement) {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    if (table.person[static_cast<size_t>(seat)]) {
                        AddSeatHand(real.tally, event.settlement, seat, declared[static_cast<size_t>(seat)]);
                    }
                }
                declared.fill(false);
            }
        }
        real.counted = real.tally.hands > 0;
        return real;
    }

    RealGame MergeRealGame(const Vector<RealMatch>& matches) {
        RealGame game;
        Array<Vector<double>, kRealGroupCount> wins;
        Array<Vector<double>, kRealGroupCount> points;
        Array<Vector<double>, kRealGroupCount> drawn;
        Array<Vector<double>, kRealGroupCount> declared;
        for (const RealMatch& match : matches) {
            if (!match.counted) {
                continue;
            }
            RealGroup& group = game[match.group];
            const ArenaTally& tally = match.tally;
            ++group.matches;
            group.tally.hands += tally.hands;
            group.tally.wins += tally.wins;
            group.tally.points += tally.points;
            group.tally.drawn += tally.drawn;
            group.tally.declared += tally.declared;
            group.tally.winScore += tally.winScore;
            wins[match.group].push_back(PercentOf(tally.wins, tally.hands));
            points[match.group].push_back(static_cast<double>(tally.points) / static_cast<double>(tally.hands));
            drawn[match.group].push_back(PercentOf(tally.drawn, tally.hands));
            declared[match.group].push_back(PercentOf(tally.declared, tally.hands));
        }
        for (size_t group = 0; group < kRealGroupCount; ++group) {
            game[group].winPercent = MeanCi(wins[group]);
            game[group].pointsPerHand = MeanCi(points[group]);
            game[group].drawnPercent = MeanCi(drawn[group]);
            game[group].declaredPercent = MeanCi(declared[group]);
        }
        return game;
    }

    LogAnalysis AnalyseLogs(const Vector<CheckedLog>& logs, const Vector<ArenaPlayer>& bots, int jobs) {
        Vector<const CheckedLog*> replayed;
        for (const CheckedLog& log : logs) {
            if (log.verdict.ok) {
                replayed.push_back(&log);
            }
        }
        // One slot per match: a thread writes only the slots of the matches it took, and the merge below runs in name order.
        Vector<MatchSlot> slots(replayed.size());
        std::atomic<size_t> next{0};
        const auto work = [&] {
            for (;;) {
                const size_t index = next.fetch_add(1);
                if (index >= replayed.size()) {
                    return;
                }
                AnalyseMatch(*replayed[index], bots, slots[index]);
            }
        };
        const size_t threadCount = eastl::min(static_cast<size_t>(eastl::max(jobs, 1)), replayed.size());
        Vector<std::thread> threads;
        for (size_t thread = 1; thread < threadCount; ++thread) {
            threads.emplace_back(work);
        }
        work();
        for (std::thread& thread : threads) {
            thread.join();
        }

        LogAnalysis analysis;
        Vector<TesterMatch> testerMatches;
        Vector<RealMatch> realMatches;
        for (const MatchSlot& slot : slots) {
            realMatches.push_back(slot.real);
            if (slot.tester.has_value()) {
                testerMatches.push_back(*slot.tester);
            } else {
                analysis.notAnalysed.push_back(slot.tester.error());
            }
        }
        analysis.tester = MergeTesterMatches(bots, testerMatches);
        analysis.realGame = MergeRealGame(realMatches);
        return analysis;
    }

    String FormatLogAnalysis(const LogAnalysis& analysis, int maxDifferences) {
        String text;
        AddAgreement(text, analysis);
        AddRealGame(text, analysis);
        AddDifferences(text, analysis, maxDifferences);
        return text;
    }

}  // namespace BECore::Mahjong::Arena
