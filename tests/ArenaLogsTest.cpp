#include "ArenaLogTestSupport.h"

#include <Mahjong/Arena/ArenaLogsReport.h>
#include <gtest/gtest.h>

/**
 * The match logs of the arena's log mode (`Arena/ArenaLogs.h`, `ArenaLogsReport.h`): the verdict on one log with its reason, the files
 * through the source seam (sorted by name), the counts of a corpus and the bytes of `--check` and of the summary. Pure logic: logs are
 * played in memory, no disk.
 */
namespace BECore::Mahjong::Arena {

    namespace {

        using namespace BECore::Mahjong::Tests;

        constexpr uint32_t kSeedA = 41;
        constexpr uint32_t kSeedB = 77;
        constexpr uint32_t kSeedC = 123;
        constexpr int kGivenUpAfter = 1;
        constexpr int kOlderVersion = 4;

        std::string_view View(const eastl::string& text) {
            return std::string_view(text.data(), text.size());
        }

        LogSpec PersonMatch(uint32_t seed, MatchPlatform platform, const char* build) {
            LogSpec spec;
            spec.seed = seed;
            spec.seats = kLogPersonFirst;
            spec.origin = {platform, build, "0.1.0"};
            return spec;
        }

        size_t FirstOf(const eastl::vector<MatchLogEvent>& events, MatchLogKind kind) {
            for (size_t index = 0; index < events.size(); ++index) {
                if (events[index].kind == kind) {
                    return index;
                }
            }
            return events.size();
        }

        /// A log of a format before the origin: version 4, no `platform`, `build` or `gameVersion`.
        eastl::string VersionFourText(const eastl::vector<MatchLogEvent>& events) {
            Json start = EventToJson(events.front());
            start["version"] = kOlderVersion;
            start.erase("platform");
            start.erase("build");
            start.erase("gameVersion");
            eastl::string text = DumpJson(start);
            text.push_back('\n');
            eastl::vector<MatchLogEvent> rest(events.begin() + 1, events.end());
            text.append(LogText(rest));
            return text;
        }

        LogFile FileOf(const char* name, const eastl::string& text) {
            return {name, text};
        }

        LogSource SourceOf(eastl::vector<LogFile> files) {
            return [files](std::string_view) -> std::expected<eastl::vector<LogFile>, eastl::string> { return files; };
        }

    }  // namespace

    TEST(ArenaLogsTest, ALogThatReachesItsEndIsOkAndComplete) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "a1b2c3d"));
        const LogVerdict verdict = CheckMatchLog(View(LogText(events)));
        EXPECT_TRUE(verdict.ok);
        EXPECT_TRUE(verdict.complete);
        EXPECT_TRUE(verdict.reason.empty());
        EXPECT_TRUE(verdict.events == events);
    }

    TEST(ArenaLogsTest, ALogThatStopsEarlyIsOkAndNotComplete) {
        LogSpec spec = PersonMatch(kSeedA, MatchPlatform::Web, "a1b2c3d");
        spec.stopAfterHands = kGivenUpAfter;
        const eastl::vector<MatchLogEvent> events = PlayLog(spec);
        ASSERT_NE(events.back().kind, MatchLogKind::End);
        const LogVerdict verdict = CheckMatchLog(View(LogText(events)));
        EXPECT_TRUE(verdict.ok);
        EXPECT_FALSE(verdict.complete);

        // Cut in the middle of a hand: still ok.
        eastl::vector<MatchLogEvent> cut(events.begin(), events.begin() + static_cast<ptrdiff_t>(FirstOf(events, MatchLogKind::Settlement)) - 1);
        const LogVerdict inHand = CheckMatchLog(View(LogText(cut)));
        EXPECT_TRUE(inHand.ok);
        EXPECT_FALSE(inHand.complete);
    }

    TEST(ArenaLogsTest, ALogOfAnOlderVersionIsOkWithAnUnknownPlatform) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonMatch(kSeedB, MatchPlatform::Web, "a1b2c3d"));
        const LogVerdict verdict = CheckMatchLog(View(VersionFourText(events)));
        ASSERT_TRUE(verdict.ok) << verdict.reason.c_str();
        EXPECT_EQ(verdict.events.front().origin.platform, MatchPlatform::Unknown);
        EXPECT_TRUE(verdict.events.front().origin.build.empty());
    }

    TEST(ArenaLogsTest, AnUnreadableLogNamesTheLine) {
        eastl::string text = LogText(PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "x")));
        text.append("{\"event\":\"action\",\"seat\":1}\n");
        const LogVerdict verdict = CheckMatchLog(View(text));
        EXPECT_FALSE(verdict.ok);
        EXPECT_EQ(verdict.problem, LogProblem::Unreadable);
        EXPECT_TRUE(verdict.reason.find("unreadable: line ") == 0) << verdict.reason.c_str();
        EXPECT_EQ(verdict.reason.find('\t'), eastl::string::npos);
        EXPECT_EQ(verdict.reason.find('\n'), eastl::string::npos);

        const LogVerdict notJson = CheckMatchLog("this is no log\n");
        EXPECT_FALSE(notJson.ok);
        EXPECT_EQ(notJson.problem, LogProblem::Unreadable);
        EXPECT_TRUE(notJson.reason.find("unreadable: line 1:") == 0) << notJson.reason.c_str();
    }

    TEST(ArenaLogsTest, AChangedActionDoesNotReplayAndTheEventIsNamed) {
        eastl::vector<MatchLogEvent> events = PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "x"));
        const size_t action = FirstOf(events, MatchLogKind::Action);
        events[action].action = HandAction::MakeDeclareWin();
        const LogVerdict verdict = CheckMatchLog(View(LogText(events)));
        EXPECT_FALSE(verdict.ok);
        EXPECT_EQ(verdict.problem, LogProblem::DoesNotReplay);
        EXPECT_EQ(std::string(verdict.reason.c_str()), "does not replay: event " + std::to_string(action) + " (ActionRefused)");
    }

    TEST(ArenaLogsTest, AChangedSettlementDoesNotReplayAndTheEventIsNamed) {
        eastl::vector<MatchLogEvent> events = PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "x"));
        const size_t settlement = FirstOf(events, MatchLogKind::Settlement);
        events[settlement].settlement.deltas[0] += 1;
        const LogVerdict verdict = CheckMatchLog(View(LogText(events)));
        EXPECT_FALSE(verdict.ok);
        EXPECT_EQ(verdict.problem, LogProblem::DoesNotReplay);
        EXPECT_EQ(std::string(verdict.reason.c_str()), "does not replay: event " + std::to_string(settlement) + " (SettlementMismatch)");
    }

    TEST(ArenaLogsTest, ALogWithoutAStartDoesNotReplay) {
        const LogVerdict empty = CheckMatchLog("");
        EXPECT_FALSE(empty.ok);
        EXPECT_EQ(std::string(empty.reason.c_str()), "does not replay: event 0 (NoStart)");
        eastl::vector<MatchLogEvent> events = PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "x"));
        events.erase(events.begin());
        const LogVerdict headless = CheckMatchLog(View(LogText(events)));
        EXPECT_FALSE(headless.ok);
        EXPECT_EQ(headless.problem, LogProblem::DoesNotReplay);
    }

    TEST(ArenaLogsTest, TheFilesAreSortedByNameWhateverTheSourceListsFirst) {
        const eastl::string good = LogText(PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "x")));
        const LogSource source = SourceOf({FileOf("b/two.jsonl", good), FileOf("a.jsonl", "garbage"), FileOf("b/one.jsonl", good)});
        const auto logs = ReadLogs(source, "anywhere");
        ASSERT_TRUE(logs.has_value());
        ASSERT_EQ(logs->size(), 3U);
        EXPECT_EQ((*logs)[0].name, "a.jsonl");
        EXPECT_FALSE((*logs)[0].verdict.ok);
        EXPECT_EQ((*logs)[1].name, "b/one.jsonl");
        EXPECT_EQ((*logs)[2].name, "b/two.jsonl");
        EXPECT_TRUE((*logs)[2].verdict.ok);
    }

    TEST(ArenaLogsTest, ThePathIsPassedToTheSourceAndItsErrorComesBack) {
        std::string seen;
        const LogSource source = [&seen](std::string_view path) -> std::expected<eastl::vector<LogFile>, eastl::string> {
            seen = std::string(path);
            return std::unexpected(eastl::string("no such folder"));
        };
        const auto logs = ReadLogs(source, "logs/matches");
        EXPECT_FALSE(logs.has_value());
        EXPECT_EQ(std::string(logs.error().c_str()), "no such folder");
        EXPECT_EQ(seen, "logs/matches");
    }

    TEST(ArenaLogsTest, TheCorpusCountsTheLogsThatReplay) {
        LogSpec mixed = PersonMatch(kSeedB, MatchPlatform::Web, "e5f6a7b");
        mixed.seats = kLogAllComputers;
        mixed.levels = {ComputerLevel{1}, ComputerLevel{3}, ComputerLevel{5}, ComputerLevel{10}};
        LogSpec given = PersonMatch(kSeedC, MatchPlatform::Desktop, "a1b2c3d");
        given.stopAfterHands = kGivenUpAfter;
        const eastl::vector<MatchLogEvent> first = PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "a1b2c3d"));
        const eastl::vector<MatchLogEvent> second = PlayLog(mixed);
        const eastl::vector<MatchLogEvent> third = PlayLog(given);
        const eastl::vector<MatchLogEvent> older = PlayLog(PersonMatch(kSeedC, MatchPlatform::Web, "z"));
        const LogSource source = SourceOf({FileOf("1.jsonl", LogText(first)), FileOf("2.jsonl", LogText(second)), FileOf("3.jsonl", LogText(third)), FileOf("4.jsonl", VersionFourText(older)),
                                           FileOf("5.jsonl", "nonsense"), FileOf("6.jsonl", "")});
        const auto logs = ReadLogs(source, ".");
        ASSERT_TRUE(logs.has_value());
        const LogCorpus corpus = SummariseLogs(*logs);

        EXPECT_EQ(corpus.files, 6);
        EXPECT_EQ(corpus.replayed, 4);
        EXPECT_EQ(corpus.dropped[static_cast<size_t>(LogProblem::Unreadable)], 1);
        EXPECT_EQ(corpus.dropped[static_cast<size_t>(LogProblem::DoesNotReplay)], 1);
        ASSERT_EQ(corpus.droppedFiles.size(), 2U);
        EXPECT_EQ(corpus.droppedFiles[0].first, "5.jsonl");
        EXPECT_EQ(corpus.droppedFiles[1].first, "6.jsonl");
        EXPECT_EQ(corpus.complete, 3);
        EXPECT_EQ(corpus.abandoned, 1);

        const int hands =
            CountEvents(first, MatchLogKind::Settlement) + CountEvents(second, MatchLogKind::Settlement) + CountEvents(third, MatchLogKind::Settlement) + CountEvents(older, MatchLogKind::Settlement);
        EXPECT_EQ(corpus.hands, hands);
        EXPECT_EQ(corpus.personSeats, 3);
        EXPECT_EQ(corpus.computerSeats, 13);
        ASSERT_EQ(corpus.setups.size(), 2U);
        EXPECT_EQ(corpus.setups[0], (eastl::pair<eastl::string, int>("CCCC", 1)));
        EXPECT_EQ(corpus.setups[1], (eastl::pair<eastl::string, int>("PCCC", 3)));

        // Levels: the three person matches are all Normal (5) at three computer seats each; the mixed table is 1, 3, 5, 10.
        EXPECT_EQ(corpus.computerLevels[0], 1);
        EXPECT_EQ(corpus.computerLevels[2], 1);
        EXPECT_EQ(corpus.computerLevels[4], 10);
        EXPECT_EQ(corpus.computerLevels[9], 1);
        EXPECT_EQ(corpus.computerLevels[1] + corpus.computerLevels[3] + corpus.computerLevels[5] + corpus.computerLevels[6] + corpus.computerLevels[7] + corpus.computerLevels[8], 0);

        EXPECT_EQ(corpus.platforms[static_cast<size_t>(MatchPlatform::Desktop)], 2);
        EXPECT_EQ(corpus.platforms[static_cast<size_t>(MatchPlatform::Web)], 1);
        EXPECT_EQ(corpus.platforms[static_cast<size_t>(MatchPlatform::Unknown)], 1) << "the version 4 log";
        ASSERT_EQ(corpus.builds.size(), 3U);
        EXPECT_EQ(corpus.builds[0], (eastl::pair<eastl::string, int>("a1b2c3d", 2)));
        EXPECT_EQ(corpus.builds[1], (eastl::pair<eastl::string, int>("e5f6a7b", 1)));
        EXPECT_EQ(corpus.builds[2], (eastl::pair<eastl::string, int>("unknown", 1)));

        const int shortest = eastl::min(eastl::min(CountEvents(first, MatchLogKind::Settlement), CountEvents(second, MatchLogKind::Settlement)),
                                        eastl::min(CountEvents(third, MatchLogKind::Settlement), CountEvents(older, MatchLogKind::Settlement)));
        const int longest = eastl::max(eastl::max(CountEvents(first, MatchLogKind::Settlement), CountEvents(second, MatchLogKind::Settlement)),
                                       eastl::max(CountEvents(third, MatchLogKind::Settlement), CountEvents(older, MatchLogKind::Settlement)));
        EXPECT_EQ(corpus.matchHands.min, shortest);
        EXPECT_EQ(corpus.matchHands.max, longest);
        EXPECT_EQ(CountEvents(third, MatchLogKind::Settlement), kGivenUpAfter);
        EXPECT_DOUBLE_EQ(corpus.matchHands.mean, static_cast<double>(hands) / 4.0);
        const int actions = CountEvents(first, MatchLogKind::Action) + CountEvents(second, MatchLogKind::Action) + CountEvents(third, MatchLogKind::Action) + CountEvents(older, MatchLogKind::Action);
        EXPECT_DOUBLE_EQ(corpus.matchActions.mean, static_cast<double>(actions) / 4.0);
        EXPECT_LE(corpus.matchActions.min, corpus.matchActions.max);
    }

    TEST(ArenaLogsTest, TheSameLogsGiveTheSameCorpusAndTheSameText) {
        const eastl::string text = LogText(PlayLog(PersonMatch(kSeedA, MatchPlatform::Desktop, "a1b2c3d")));
        const auto one = ReadLogs(SourceOf({FileOf("x.jsonl", text), FileOf("y.jsonl", "bad")}), ".");
        const auto other = ReadLogs(SourceOf({FileOf("y.jsonl", "bad"), FileOf("x.jsonl", text)}), ".");
        ASSERT_TRUE(one.has_value() && other.has_value());
        EXPECT_EQ(FormatCorpusSummary(SummariseLogs(*one)), FormatCorpusSummary(SummariseLogs(*other)));
        EXPECT_EQ(FormatCheckLines(*one), FormatCheckLines(*other));
    }

    TEST(ArenaLogsTest, TheCheckLinesAreExactlyTheFormatTheCollectingToolReads) {
        eastl::vector<CheckedLog> logs(3);
        logs[0].name = "a.jsonl";
        logs[0].verdict.ok = true;
        logs[1].name = "dir/b.jsonl";
        logs[1].verdict.reason = "unreadable: line 2: not JSON";
        logs[2].name = "tab\tname.jsonl";
        logs[2].verdict.reason = "does not replay: event 4 (ActionRefused)";
        EXPECT_EQ(std::string(FormatCheckLines(logs).c_str()), "ok\ta.jsonl\n"
                                                               "bad\tdir/b.jsonl\tunreadable: line 2: not JSON\n"
                                                               "bad\ttab name.jsonl\tdoes not replay: event 4 (ActionRefused)\n");
        EXPECT_TRUE(FormatCheckLines({}).empty());
    }

    TEST(ArenaLogsTest, TheSummaryTextIsPinned) {
        LogCorpus corpus;
        corpus.files = 4;
        corpus.replayed = 3;
        corpus.dropped = {1, 0};
        corpus.droppedFiles.emplace_back("bad.jsonl", "unreadable: line 2: not JSON");
        corpus.complete = 2;
        corpus.abandoned = 1;
        corpus.hands = 20;
        corpus.personSeats = 2;
        corpus.computerSeats = 10;
        corpus.setups = {{"CCCC", 1}, {"PCCC", 2}};
        corpus.computerLevels = {1, 0, 2, 0, 5, 0, 0, 0, 0, 2};
        corpus.platforms[static_cast<size_t>(MatchPlatform::Desktop)] = 1;
        corpus.platforms[static_cast<size_t>(MatchPlatform::Web)] = 1;
        corpus.platforms[static_cast<size_t>(MatchPlatform::Unknown)] = 1;
        corpus.builds = {{"a1b2c3d", 2}, {"unknown", 1}};
        corpus.matchHands = {3, 6.6666, 9};
        corpus.matchActions = {50, 120.5, 200};
        EXPECT_EQ(std::string(FormatCorpusSummary(corpus).c_str()), "Match logs\n"
                                                                    "files read        4\n"
                                                                    "replayed          3\n"
                                                                    "dropped           1 (Unreadable 1, DoesNotReplay 0)\n"
                                                                    "  bad.jsonl: unreadable: line 2: not JSON\n"
                                                                    "matches           complete 2, abandoned 1\n"
                                                                    "hands             20\n"
                                                                    "seats             person 2, computer 10\n"
                                                                    "seat setups       CCCC 1, PCCC 2\n"
                                                                    "platforms         desktop 1, web 1, unknown 1\n"
                                                                    "builds            a1b2c3d 2, unknown 1\n"
                                                                    "match hands       min 3, mean 6.67, max 9\n"
                                                                    "match actions     min 50, mean 120.50, max 200\n"
                                                                    "\n"
                                                                    "computer seats by level\n"
                                                                    "level    seats\n"
                                                                    "1            1\n"
                                                                    "2            0\n"
                                                                    "3            2\n"
                                                                    "4            0\n"
                                                                    "5            5\n"
                                                                    "6            0\n"
                                                                    "7            0\n"
                                                                    "8            0\n"
                                                                    "9            0\n"
                                                                    "10           2\n");
        EXPECT_NE(FormatCorpusSummary(LogCorpus{}).find("files read        0"), eastl::string::npos);
    }

}  // namespace BECore::Mahjong::Arena
