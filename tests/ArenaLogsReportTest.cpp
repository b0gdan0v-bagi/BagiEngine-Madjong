#include "ArenaLogTestSupport.h"

#include <Mahjong/Arena/ArenaLogsReport.h>
#include <gtest/gtest.h>

/**
 * The report of the arena's log mode (`Arena/ArenaLogsReport.h`) beyond the corpus summary: the real-game numbers by the level of the
 * computers met (from the settlements and the logged `declareCalling`, the match the unit of the intervals), the analysis of a corpus
 * in threads (the same bytes at any `--jobs`) and the text. Pure logic: logs are played in memory, a person is a `ComputerPlayer` of a
 * known level.
 */
namespace BECore::Mahjong::Arena {

    namespace {

        using namespace BECore::Mahjong::Tests;

        constexpr uint32_t kSeedA = 41;
        constexpr uint32_t kSeedB = 77;
        constexpr uint32_t kSeedC = 123;
        constexpr uint32_t kSeedD = 5;
        constexpr int kLevelThree = 3;
        constexpr int kShownDifferences = 2;
        constexpr double kTolerance = 1e-9;

        // The analysis of the known corpus (bot `normal`, two differences listed). The real-game rows were checked against the settlements of
        // the five matches (`Oracle` counts them again, a game of seed 41 given up after hand 1 is 1 of the 9 seat-hands of level 5, the mixed
        // table has 2 wins in 8 hands); the agreement numbers are those of `ArenaTester`.
        constexpr const char* kPinnedAnalysis = R"GOLDEN(
Agreement of the people with the bots (the logged action is the same as the bot's choice)
matches analysed 5, not analysed 0
+- is 1.96 standard errors over the matches (a 95 percent interval); the match is the sample, not the decision
Testers are not experts: the share says how people play, not what is best
bot                     phase            decisions           equal %
normal                  discard turn           456    75.82 +- 16.93
                        claim window           116     93.15 +- 8.25
                        both                   572    79.27 +- 15.40

Real games by the level of the computers met (the logged results of the people's seat-hands at tables with a computer)
A table whose computers are not all at one level is `mixed`; a hand not finished is not counted; the match is the unit of the intervals
level     matches   seat-hands            wins %           points/hand           drawn %        declared %   avg win score
1               2           22     7.69 +- 15.08       -32.61 +- 23.20    24.79 +- 26.80     26.50 +- 8.38           40.00
5               2            9     6.25 +- 12.25       -53.12 +- 52.68      0.00 +- 0.00    18.75 +- 36.75           40.00
mixed           1            8             25.00                 23.75              0.00             37.50           75.00

Differences from normal: 150 in all, 2 listed (log order)
  a.jsonl hand 1 action 16 seat 0: logged discard Gd, bot chose discard Rd
    --log a.jsonl --hand 1 --action 16
  a.jsonl hand 2 action 3 seat 0: logged discard 9m, bot chose discard 1p
    --log a.jsonl --hand 2 --action 3
)GOLDEN";

        ArenaPlayer Bot(const char* spec) {
            const std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(spec);
            EXPECT_TRUE(player.has_value());
            return player.has_value() ? *player : ArenaPlayer{};
        }

        LogSpec Played(uint32_t seed, SeatSetup seats, const SeatLevels& levels, int stopAfterHands = 0) {
            LogSpec spec;
            spec.seed = seed;
            spec.seats = seats;
            spec.levels = levels;
            spec.stopAfterHands = stopAfterHands;
            return spec;
        }

        LogSource SourceOf(eastl::vector<LogFile> files) {
            return [files](std::string_view) -> std::expected<eastl::vector<LogFile>, eastl::string> { return files; };
        }

        /// The known corpus: a Normal person among Normal computers, one among Easy ones, one among Easy and Normal computers (mixed), a
        /// match given up after a hand, and a version 4 log (no origin) of an Easy table.
        eastl::vector<LogFile> KnownCorpus() {
            const SeatLevels mixed{ComputerLevel::Normal, ComputerLevel::Easy, ComputerLevel::Normal, ComputerLevel::Normal};
            return {{"d.jsonl", LogText(PlayLog(Played(kSeedA, kLogPersonFirst, UniformLevels(ComputerLevel::Normal), 1)))},
                    {"a.jsonl", LogText(PlayLog(Played(kSeedA, kLogPersonFirst, UniformLevels(ComputerLevel::Normal))))},
                    {"c.jsonl", LogText(PlayLog(Played(kSeedC, kLogPersonFirst, mixed)))},
                    {"e.jsonl", LogTextVersionFour(PlayLog(Played(kSeedD, kLogPersonFirst, UniformLevels(ComputerLevel::Easy))))},
                    {"b.jsonl", LogText(PlayLog(Played(kSeedB, kLogPersonFirst, UniformLevels(ComputerLevel::Easy))))}};
        }

        eastl::vector<CheckedLog> Checked(const eastl::vector<LogFile>& files) {
            const auto logs = ReadLogs(SourceOf(files), ".");
            EXPECT_TRUE(logs.has_value());
            return logs.has_value() ? *logs : eastl::vector<CheckedLog>{};
        }

        /// The numbers of the people's seats counted again from the settlements, the way a reader of the log would.
        ArenaTally Oracle(const eastl::vector<MatchLogEvent>& events) {
            ArenaTally tally;
            eastl::array<bool, kSeatCount> declared{};
            for (const MatchLogEvent& event : events) {
                if (event.kind == MatchLogKind::Action && event.action.kind == ActionKind::Discard && event.action.declareCalling) {
                    declared[static_cast<size_t>(event.seat)] = true;
                }
                if (event.kind != MatchLogKind::Settlement) {
                    continue;
                }
                for (size_t seat = 0; seat < kSeatCount; ++seat) {
                    if (events.front().seats[seat] != SeatKind::Person) {
                        continue;
                    }
                    ++tally.hands;
                    tally.wins += event.settlement.winner == static_cast<Seat>(seat) ? 1 : 0;
                    tally.winScore += event.settlement.winner == static_cast<Seat>(seat) ? event.settlement.scores[seat] : 0;
                    tally.points += event.settlement.deltas[seat];
                    tally.drawn += event.settlement.kind == ResultKind::Drawn ? 1 : 0;
                    tally.declared += declared[seat] ? 1 : 0;
                }
                declared.fill(false);
            }
            return tally;
        }

        bool Contains(const eastl::string& text, std::string_view part) {
            return std::string_view(text.data(), text.size()).find(part) != std::string_view::npos;
        }

        RealMatch Counted(size_t group, int64_t hands, int64_t wins, int64_t points) {
            RealMatch match;
            match.counted = true;
            match.group = group;
            match.tally.hands = hands;
            match.tally.wins = wins;
            match.tally.points = points;
            return match;
        }

    }  // namespace

    TEST(ArenaLogsReportTest, TheNumbersOfAMatchAreTheSettlementsOfThePeoplesSeats) {
        for (const uint32_t seed : {kSeedA, kSeedB, kSeedC}) {
            const eastl::vector<MatchLogEvent> events = PlayLog(Played(seed, kLogPersonFirst, UniformLevels(ComputerLevel::Normal)));
            const RealMatch real = RealGameOfMatch(events);
            const ArenaTally expected = Oracle(events);
            EXPECT_TRUE(real.counted) << seed;
            EXPECT_EQ(real.group, static_cast<size_t>(LevelNumber(ComputerLevel::Normal) - kMinLevel));
            EXPECT_EQ(real.tally.hands, CountEvents(events, MatchLogKind::Settlement)) << seed;
            EXPECT_EQ(real.tally.wins, expected.wins) << seed;
            EXPECT_EQ(real.tally.points, expected.points) << seed;
            EXPECT_EQ(real.tally.drawn, expected.drawn) << seed;
            EXPECT_EQ(real.tally.declared, expected.declared) << seed;
            EXPECT_EQ(real.tally.winScore, expected.winScore) << seed;
        }
    }

    TEST(ArenaLogsReportTest, TwoPersonsAreTwoSeatHandsOfAHand) {
        const eastl::vector<MatchLogEvent> events =
            PlayLog(Played(kSeedA, SeatSetup{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer}, UniformLevels(ComputerLevel::Normal)));
        const RealMatch real = RealGameOfMatch(events);
        EXPECT_EQ(real.tally.hands, 2 * CountEvents(events, MatchLogKind::Settlement));
        EXPECT_EQ(real.tally.points, Oracle(events).points);
    }

    TEST(ArenaLogsReportTest, AHandThatIsNotFinishedIsNotCounted) {
        // Given up after one hand: the second hand was dealt and played in part and has no settlement.
        const eastl::vector<MatchLogEvent> events = PlayLog(Played(kSeedA, kLogPersonFirst, UniformLevels(ComputerLevel::Normal), 1));
        ASSERT_GT(CountEvents(events, MatchLogKind::Hand), 1);
        const RealMatch real = RealGameOfMatch(events);
        EXPECT_EQ(real.tally.hands, 1);
        // A log cut before its first settlement counts nothing and is not a match of the table.
        eastl::vector<MatchLogEvent> early;
        for (const MatchLogEvent& event : events) {
            if (event.kind == MatchLogKind::Settlement) {
                break;
            }
            early.push_back(event);
        }
        EXPECT_FALSE(RealGameOfMatch(early).counted);
    }

    TEST(ArenaLogsReportTest, ATableNeedsAPersonAndAComputer) {
        EXPECT_FALSE(RealGameOfMatch(PlayLog(Played(kSeedA, kLogAllComputers, UniformLevels(ComputerLevel::Normal)))).counted);
        const SeatSetup people{SeatKind::Person, SeatKind::Person, SeatKind::Person, SeatKind::Person};
        EXPECT_FALSE(RealGameOfMatch(PlayLog(Played(kSeedA, people, UniformLevels(ComputerLevel::Normal)))).counted);
        EXPECT_FALSE(RealGameOfMatch({}).counted);
    }

    TEST(ArenaLogsReportTest, TheGroupIsTheLevelOfTheComputersOrMixed) {
        // The level of a person's seat is not the computers' level: only the computer seats decide.
        SeatLevels levels = UniformLevels(ComputerLevel::Easy);
        levels[0] = ComputerLevel{kLevelThree};
        EXPECT_EQ(RealGameOfMatch(PlayLog(Played(kSeedA, kLogPersonFirst, levels))).group, static_cast<size_t>(LevelNumber(ComputerLevel::Easy) - kMinLevel));
        levels[2] = ComputerLevel{kLevelThree};
        EXPECT_EQ(RealGameOfMatch(PlayLog(Played(kSeedA, kLogPersonFirst, levels))).group, kMixedGroup);
        levels = UniformLevels(ComputerLevel{kMaxLevel});
        EXPECT_EQ(RealGameOfMatch(PlayLog(Played(kSeedA, kLogPersonFirst, levels))).group, static_cast<size_t>(kMaxLevel - kMinLevel));
    }

    TEST(ArenaLogsReportTest, TheMatchIsTheUnitOfTheIntervals) {
        // A long match with 1 win in 8 and a short one with 1 in 2: the mean of the two shares, not the pooled share.
        const eastl::vector<RealMatch> matches{Counted(0, 8, 1, 80), Counted(0, 2, 1, -20), RealMatch{}};
        const RealGame game = MergeRealGame(matches);
        EXPECT_EQ(game[0].matches, 2);
        EXPECT_EQ(game[0].tally.hands, 10);
        EXPECT_EQ(game[0].winPercent.n, size_t{2});
        EXPECT_NEAR(game[0].winPercent.mean, (12.5 + 50.0) / 2.0, kTolerance);
        EXPECT_NEAR(game[0].pointsPerHand.mean, (10.0 - 10.0) / 2.0, kTolerance);
        EXPECT_TRUE(game[0].winPercent.hasInterval);
        // The other groups are empty, and the uncounted match is left out.
        EXPECT_EQ(game[1].matches, 0);
        EXPECT_EQ(game[1].winPercent.n, size_t{0});
    }

    TEST(ArenaLogsReportTest, TheAnalysisHoldsTheThreeParts) {
        const eastl::vector<CheckedLog> logs = Checked(KnownCorpus());
        const LogAnalysis analysis = AnalyseLogs(logs, {Bot("normal")}, 1);
        EXPECT_TRUE(analysis.notAnalysed.empty());
        EXPECT_EQ(analysis.tester.matches, 5);
        EXPECT_GT(analysis.tester.bots[0].decisions[kTesterBothRow], 0);
        EXPECT_EQ(analysis.realGame[static_cast<size_t>(LevelNumber(ComputerLevel::Normal) - kMinLevel)].matches, 2);
        EXPECT_EQ(analysis.realGame[static_cast<size_t>(LevelNumber(ComputerLevel::Easy) - kMinLevel)].matches, 2);
        EXPECT_EQ(analysis.realGame[kMixedGroup].matches, 1);
        // The pooled counts are the oracle's over the matches of the group.
        ArenaTally easy;
        for (const char* name : {"b.jsonl", "e.jsonl"}) {
            for (const CheckedLog& log : logs) {
                if (log.name == name) {
                    const ArenaTally one = Oracle(log.verdict.events);
                    easy.hands += one.hands;
                    easy.wins += one.wins;
                    easy.points += one.points;
                }
            }
        }
        const ArenaTally& pooled = analysis.realGame[static_cast<size_t>(LevelNumber(ComputerLevel::Easy) - kMinLevel)].tally;
        EXPECT_EQ(pooled.hands, easy.hands);
        EXPECT_EQ(pooled.wins, easy.wins);
        EXPECT_EQ(pooled.points, easy.points);
    }

    TEST(ArenaLogsReportTest, TheSameBytesAtAnyNumberOfThreads) {
        const eastl::vector<CheckedLog> logs = Checked(KnownCorpus());
        const eastl::string single = FormatLogAnalysis(AnalyseLogs(logs, {Bot("normal"), Bot("easy")}, 1), kShownDifferences);
        EXPECT_FALSE(single.empty());
        for (const int jobs : {2, 5, 64}) {
            EXPECT_EQ(FormatLogAnalysis(AnalyseLogs(logs, {Bot("normal"), Bot("easy")}, jobs), kShownDifferences), single) << "jobs " << jobs;
        }
    }

    TEST(ArenaLogsReportTest, TheOrderOfTheSourceDoesNotChangeTheBytes) {
        eastl::vector<LogFile> files = KnownCorpus();
        const eastl::string one = FormatLogAnalysis(AnalyseLogs(Checked(files), {Bot("normal")}, 2), kShownDifferences);
        eastl::reverse(files.begin(), files.end());
        EXPECT_EQ(FormatLogAnalysis(AnalyseLogs(Checked(files), {Bot("normal")}, 2), kShownDifferences), one);
    }

    TEST(ArenaLogsReportTest, AMatchThatCannotBeAnalysedIsNamedAndCountedAndStillInTheRealNumbers) {
        eastl::vector<CheckedLog> logs = Checked(KnownCorpus());
        // It replayed in the check, but a position cannot be built from it: its first action is not the first one that was legal.
        for (CheckedLog& log : logs) {
            if (log.name != "a.jsonl") {
                continue;
            }
            for (MatchLogEvent& event : log.verdict.events) {
                if (event.kind == MatchLogKind::Action) {
                    event.action = HandAction::MakeDeclareWin();
                    break;
                }
            }
        }
        const LogAnalysis analysis = AnalyseLogs(logs, {Bot("normal")}, 2);
        ASSERT_EQ(analysis.notAnalysed.size(), size_t{1});
        EXPECT_EQ(std::string(analysis.notAnalysed[0].c_str()).rfind("a.jsonl", 0), size_t{0});
        EXPECT_EQ(analysis.tester.matches, 4);
        EXPECT_EQ(analysis.realGame[static_cast<size_t>(LevelNumber(ComputerLevel::Normal) - kMinLevel)].matches, 2);
        const eastl::string text = FormatLogAnalysis(analysis, kShownDifferences);
        EXPECT_TRUE(Contains(text, "matches analysed 4, not analysed 1\n  a.jsonl")) << text.c_str();
    }

    TEST(ArenaLogsReportTest, TheDifferencesAreCappedAndTheCountIsAlwaysPrinted) {
        const LogAnalysis analysis = AnalyseLogs(Checked(KnownCorpus()), {Bot("easy")}, 1);
        const size_t all = analysis.tester.differences.size();
        ASSERT_GT(all, size_t{3});
        const eastl::string capped = FormatLogAnalysis(analysis, kShownDifferences);
        EXPECT_TRUE(Contains(capped, Format("Differences from easy: {} in all, {} listed", all, kShownDifferences).c_str())) << capped.c_str();
        EXPECT_EQ(std::count(capped.begin(), capped.end(), '-') > 0, true);
        const eastl::string none = FormatLogAnalysis(analysis, 0);
        EXPECT_TRUE(Contains(none, Format("Differences from easy: {} in all, 0 listed (log order)\n", all).c_str()));
        EXPECT_FALSE(Contains(none, "--hand"));
        const eastl::string every = FormatLogAnalysis(analysis, static_cast<int>(all) + 5);
        EXPECT_TRUE(Contains(every, Format("{} in all, {} listed", all, all).c_str()));
    }

    TEST(ArenaLogsReportTest, ADifferenceHasTheLineThatOpensItInTheAnalyzer) {
        const LogAnalysis analysis = AnalyseLogs(Checked(KnownCorpus()), {Bot("easy")}, 1);
        ASSERT_FALSE(analysis.tester.differences.empty());
        const TesterDifference& first = analysis.tester.differences.front();
        const eastl::string text = FormatLogAnalysis(analysis, 1);
        EXPECT_TRUE(Contains(text, Format("    --log {} --hand {} --action {}\n", first.log.c_str(), first.hand, first.actionIndex).c_str())) << text.c_str();
    }

    TEST(ArenaLogsReportTest, ACorpusWithoutAMatchPrintsEmptyTablesAndNoError) {
        const eastl::string text = FormatLogAnalysis(AnalyseLogs({}, {Bot("normal")}, 3), 20);
        EXPECT_TRUE(Contains(text, "matches analysed 0, not analysed 0"));
        EXPECT_TRUE(Contains(text, "no match with a person and a computer seat was settled"));
        EXPECT_TRUE(Contains(text, "Differences from normal: 0 in all, 0 listed"));
    }

    TEST(ArenaLogsReportTest, TheTextOfAKnownCorpusIsPinned) {
        const eastl::string text = FormatLogAnalysis(AnalyseLogs(Checked(KnownCorpus()), {Bot("normal")}, 1), kShownDifferences);
        EXPECT_EQ(std::string(text.c_str()), std::string(kPinnedAnalysis));
    }

}  // namespace BECore::Mahjong::Arena
