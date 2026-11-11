#include "HandTestSupport.h"

#include <BECore/Control/Json.h>
#include <Mahjong/Analyzer/AnalyzerCli.h>
#include <Mahjong/MatchLogJson.h>
#include <gtest/gtest.h>

/**
 * The command line of the analyzer (Analyzer/AnalyzerCli.h, the program `BagiMahjongAnalyzer`): the arguments (defaults, every option,
 * every refusal with its message), and the whole program on the text of a played match and on a hand-written position through a reader
 * of strings: the report of one decision, one report per decision of a seat, the same bytes at any `--jobs`, a log without its `Hand`
 * events, the exit codes. A handful of worlds so Debug stays fast. Pure logic: no engine services, no file.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using Args = eastl::vector<std::string_view>;

        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr int kHandLimit = 200;
        constexpr uint32_t kMatchSeed = 41;
        constexpr const char* kWorlds = "12";
        constexpr const char* kFewWorlds = "2";
        constexpr const char* kLogName = "match.jsonl";
        constexpr const char* kPositionName = "table.txt";
        constexpr int kOtherJobs = 3;

        // Seat 0 answers a 5p of seat 3 with a pung or a chow.
        constexpr const char* kClaimPosition = "seat 0\n"
                                               "phase claim\n"
                                               "hand 1p 2p 3p 4p 6p 4s 5s 6s 7m 8m 9m 5p 5p\n"
                                               "discards 3 Ww\n"
                                               "pending 5p from 3\n";

        /// The match log of four computers as the text of a log file.
        std::string PlayedLogText(bool withHandEvents) {
            Match match;
            TableSession session(match);
            MatchLog log;
            session.SetLogSink(&log);
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            EXPECT_TRUE(session.StartMatch(settings, kMatchSeed, kAllComputers, kDefaultLevel).has_value());
            for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kHandLimit; ++hands) {
                session.RunComputers();
                if (session.GetPhase() == SessionPhase::HandOver) {
                    EXPECT_TRUE(session.NextHand().has_value());
                }
            }
            std::string text;
            for (const MatchLogEvent& event : log.Events()) {
                if (withHandEvents || event.kind != MatchLogKind::Hand) {
                    text += EventToJson(event).dump() + "\n";
                }
            }
            return text;
        }

        const std::string& CompleteLog() {
            static const std::string text = PlayedLogText(true);
            return text;
        }

        /// A reader over a few named strings.
        Analyzer::FileReader ReaderOf(std::map<std::string, std::string> files) {
            return [files = std::move(files)](std::string_view path) -> std::expected<eastl::string, eastl::string> {
                const auto found = files.find(std::string(path));
                if (found == files.end()) {
                    return std::unexpected(eastl::string("no such file"));
                }
                return eastl::string(found->second.data(), found->second.size());
            };
        }

        Analyzer::FileReader StandardReader() {
            return ReaderOf({{kLogName, CompleteLog()}, {kPositionName, kClaimPosition}});
        }

        struct CliRun {
            int code = 0;
            std::string out;
            std::string err;
        };

        CliRun RunWith(const Analyzer::FileReader& reader, const Args& args) {
            Analyzer::AnalyzerCliOutput output;
            CliRun run;
            run.code = Analyzer::RunAnalyzerCli(eastl::span<const std::string_view>(args.data(), args.size()), reader, output);
            run.out = output.out.c_str();
            run.err = output.err.c_str();
            return run;
        }

        CliRun RunCli(const Args& args) {
            return RunWith(StandardReader(), args);
        }

        std::expected<Analyzer::AnalyzerArgs, eastl::string> Parse(const Args& args) {
            return Analyzer::ParseAnalyzerArgs(eastl::span<const std::string_view>(args.data(), args.size()));
        }

        std::string ErrorOf(const Args& args) {
            const std::expected<Analyzer::AnalyzerArgs, eastl::string> parsed = Parse(args);
            EXPECT_FALSE(parsed.has_value());
            return parsed.has_value() ? std::string() : std::string(parsed.error().c_str());
        }

        size_t Count(const std::string& text, const std::string& part) {
            size_t count = 0;
            for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) {
                ++count;
            }
            return count;
        }

        bool Has(const std::string& text, const std::string& part) {
            return text.find(part) != std::string::npos;
        }

        /// The rows of the table of the first report in `text`: the lines between the `action` header line and the next empty line.
        size_t TableRows(const std::string& text) {
            const size_t header = text.find("\naction ");
            EXPECT_NE(header, std::string::npos);
            const size_t begin = text.find('\n', header + 1);
            const size_t end = text.find("\n\n", begin);
            const std::string table = text.substr(begin + 1, (end == std::string::npos ? text.size() : end) - begin - 1);
            return Count(table, "\n") + (table.empty() || table.back() == '\n' ? 0 : 1);
        }

        /// The decisions of a seat in hand 1 of the played log.
        eastl::vector<Analyzer::LogPosition> DecisionsOf(Seat seat) {
            const std::expected<eastl::vector<MatchLogEvent>, eastl::string> events = ParseMatchLog(CompleteLog());
            EXPECT_TRUE(events.has_value());
            const std::expected<eastl::vector<Analyzer::LogPosition>, eastl::string> positions = Analyzer::PositionsOfSeat(*events, 1, seat);
            EXPECT_TRUE(positions.has_value());
            return positions.has_value() ? *positions : eastl::vector<Analyzer::LogPosition>();
        }

    }  // namespace

    TEST(AnalyzerCliTest, TheDefaultsAreTheDefaultsOfTheConfigAndEveryOptionIsRead) {
        const Args minimal{"--position", kPositionName};
        const std::expected<Analyzer::AnalyzerArgs, eastl::string> defaults = Parse(minimal);
        ASSERT_TRUE(defaults.has_value());
        EXPECT_EQ(defaults->positionFile, kPositionName);
        EXPECT_EQ(defaults->config.worlds, Analyzer::kDefaultWorlds);
        EXPECT_EQ(defaults->config.seed, Analyzer::kDefaultSeed);
        EXPECT_EQ(defaults->jobs, Analyzer::kDefaultJobs);
        EXPECT_FALSE(defaults->json);
        EXPECT_FALSE(defaults->help);

        const Args oneDecision{"--log", kLogName, "--hand", "2", "--action", "5", "--worlds", "30", "--seed", "9", "--jobs", "2", "--json"};
        const std::expected<Analyzer::AnalyzerArgs, eastl::string> decision = Parse(oneDecision);
        ASSERT_TRUE(decision.has_value()) << decision.error().c_str();
        EXPECT_EQ(decision->logFile, kLogName);
        EXPECT_EQ(decision->hand, 2);
        ASSERT_TRUE(decision->action.has_value());
        EXPECT_EQ(*decision->action, 5);
        EXPECT_FALSE(decision->seat.has_value());
        EXPECT_EQ(decision->config.worlds, 30);
        EXPECT_EQ(decision->config.seed, 9U);
        EXPECT_EQ(decision->jobs, 2);
        EXPECT_TRUE(decision->json);

        const std::expected<Analyzer::AnalyzerArgs, eastl::string> seat = Parse({"--log", kLogName, "--hand", "1", "--seat", "3"});
        ASSERT_TRUE(seat.has_value());
        ASSERT_TRUE(seat->seat.has_value());
        EXPECT_EQ(*seat->seat, 3);
        EXPECT_FALSE(seat->action.has_value());

        const std::expected<Analyzer::AnalyzerArgs, eastl::string> allCores = Parse({"--position", kPositionName, "--jobs", "0"});
        ASSERT_TRUE(allCores.has_value());
        EXPECT_GE(allCores->jobs, 1);
        EXPECT_LE(allCores->jobs, Analyzer::kMaxJobs);
    }

    TEST(AnalyzerCliTest, HelpWinsOverEverythingAfterItAndAnalyzesNothing) {
        const std::expected<Analyzer::AnalyzerArgs, eastl::string> parsed = Parse({"--help", "--bogus"});
        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(parsed->help);
        const CliRun run = RunCli({"--help"});
        EXPECT_EQ(run.code, Analyzer::kExitOk);
        EXPECT_TRUE(Has(run.out, "usage: BagiMahjongAnalyzer"));
        EXPECT_TRUE(Has(run.out, "--worlds"));
        EXPECT_TRUE(run.err.empty());
    }

    TEST(AnalyzerCliTest, EveryBadArgumentNamesTheProblemAndExitsWithTwo) {
        struct Case {
            Args args;
            const char* message;
        };
        const Case cases[] = {
            {{}, "give the position"},
            {{"--bogus"}, "unknown argument \"--bogus\""},
            {{"--position"}, "--position needs a value"},
            {{"--log", kLogName, "--position", kPositionName}, "cannot be combined"},
            {{"--log", kLogName, "--action", "0"}, "--log needs --hand"},
            {{"--log", kLogName, "--hand", "1"}, "needs --action I"},
            {{"--log", kLogName, "--hand", "1", "--action", "0", "--seat", "1"}, "--action and --seat cannot be combined"},
            {{"--position", kPositionName, "--hand", "1"}, "--hand is for --log"},
            {{"--position", kPositionName, "--action", "1"}, "--action is for --log"},
            {{"--position", kPositionName, "--seat", "1"}, "--seat is for --log"},
            {{"--log", kLogName, "--hand", "0", "--action", "0"}, "--hand: \"0\" is not a whole number from 1"},
            {{"--log", kLogName, "--hand", "x", "--action", "0"}, "--hand: \"x\""},
            {{"--log", kLogName, "--hand", "1", "--action", "-1"}, "--action: \"-1\""},
            {{"--log", kLogName, "--hand", "1", "--seat", "4"}, "--seat: \"4\" is not a whole number from 0 to 3"},
            {{"--position", kPositionName, "--worlds", "0"}, "--worlds: \"0\" is not a whole number from 1"},
            {{"--position", kPositionName, "--worlds", "2000000"}, "--worlds: \"2000000\""},
            {{"--position", kPositionName, "--seed", "-5"}, "--seed: \"-5\""},
            {{"--position", kPositionName, "--jobs", "100000"}, "--jobs: \"100000\""},
        };
        for (const Case& item : cases) {
            EXPECT_TRUE(Has(ErrorOf(item.args), item.message)) << item.message;
            const CliRun run = RunCli(item.args);
            EXPECT_EQ(run.code, Analyzer::kExitBadArguments) << item.message;
            EXPECT_TRUE(run.out.empty());
            EXPECT_TRUE(Has(run.err, item.message)) << run.err;
            EXPECT_TRUE(Has(run.err, "usage: BagiMahjongAnalyzer"));
        }
    }

    TEST(AnalyzerCliTest, OneLogDecisionGivesTheHeaderAndARowPerLegalAction) {
        const std::expected<eastl::vector<MatchLogEvent>, eastl::string> events = ParseMatchLog(CompleteLog());
        ASSERT_TRUE(events.has_value());
        const std::expected<Analyzer::LogPosition, eastl::string> position = Analyzer::PositionFromLog(*events, 1, 0);
        ASSERT_TRUE(position.has_value());

        const CliRun run = RunCli({"--log", kLogName, "--hand", "1", "--action", "0", "--worlds", kWorlds});
        EXPECT_EQ(run.code, Analyzer::kExitOk) << run.err;
        EXPECT_TRUE(run.err.empty());
        EXPECT_TRUE(Has(run.out, "EV of every legal action"));
        EXPECT_TRUE(Has(run.out, "not an equilibrium"));
        EXPECT_TRUE(Has(run.out, std::string("position: ") + kLogName + ", hand 1, action 0"));
        EXPECT_EQ(TableRows(run.out), position->view.legal.size());
        EXPECT_EQ(Count(run.out, "logged"), 1U);
        EXPECT_EQ(Count(run.out, "position:"), 1U);
    }

    TEST(AnalyzerCliTest, ASeatGivesOneReportPerDecisionAndASkipLineForASingleLegalAction) {
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const eastl::vector<Analyzer::LogPosition> decisions = DecisionsOf(seat);
            size_t compared = 0;
            for (const Analyzer::LogPosition& decision : decisions) {
                compared += decision.view.legal.size() > 1 ? 1 : 0;
            }
            const std::string seatText = std::to_string(seat);
            const CliRun text = RunCli({"--log", kLogName, "--hand", "1", "--seat", seatText, "--worlds", kFewWorlds});
            EXPECT_EQ(text.code, Analyzer::kExitOk) << text.err;
            EXPECT_EQ(Count(text.out, "position:"), compared) << "seat " << seat;
            EXPECT_EQ(Count(text.out, "skipped: "), decisions.size() - compared) << "seat " << seat;
            const CliRun json = RunCli({"--log", kLogName, "--hand", "1", "--seat", seatText, "--worlds", kFewWorlds, "--json"});
            EXPECT_EQ(json.code, Analyzer::kExitOk);
            EXPECT_EQ(Count(json.out, "\n"), decisions.size());
            EXPECT_EQ(Count(json.out, "\"skipped\":"), decisions.size() - compared);
        }
    }

    TEST(AnalyzerCliTest, TheJsonModeIsOneObjectPerLineThatParses) {
        const CliRun run = RunCli({"--log", kLogName, "--hand", "1", "--action", "0", "--worlds", kWorlds, "--json"});
        ASSERT_EQ(run.code, Analyzer::kExitOk) << run.err;
        ASSERT_EQ(Count(run.out, "\n"), 1U);
        const std::expected<Json, ControlError> json = ParseJson(eastl::string(run.out.c_str()));
        ASSERT_TRUE(json.has_value());
        EXPECT_EQ((*json)["version"].get<int>(), Analyzer::kAnalysisJsonVersion);
        EXPECT_EQ((*json)["source"].get<std::string>(), kLogName);
        EXPECT_EQ((*json)["hand"].get<int>(), 1);
        EXPECT_TRUE((*json)["actions"].is_array());
    }

    TEST(AnalyzerCliTest, TheBytesAreTheSameAtAnyNumberOfJobs) {
        const std::string jobs = std::to_string(kOtherJobs);
        for (const bool json : {false, true}) {
            Args one{"--log", kLogName, "--hand", "1", "--action", "2", "--worlds", kWorlds, "--seed", "5", "--jobs", "1"};
            Args several = one;
            several.back() = jobs;
            if (json) {
                one.push_back("--json");
                several.push_back("--json");
            }
            const CliRun first = RunCli(one);
            const CliRun second = RunCli(several);
            ASSERT_EQ(first.code, Analyzer::kExitOk) << first.err;
            ASSERT_EQ(second.code, Analyzer::kExitOk) << second.err;
            EXPECT_FALSE(first.out.empty());
            EXPECT_EQ(first.out, second.out) << (json ? "json" : "text");
            EXPECT_EQ(first.out, RunCli(one).out);
        }
        const CliRun claim = RunCli({"--position", kPositionName, "--worlds", kWorlds, "--jobs", "1"});
        EXPECT_EQ(claim.out, RunCli({"--position", kPositionName, "--worlds", kWorlds, "--jobs", jobs.c_str()}).out);
    }

    TEST(AnalyzerCliTest, ALogWithoutItsHandEventsGivesTheSameBytes) {
        const Analyzer::FileReader reader = ReaderOf({{"complete.jsonl", CompleteLog()}, {"bare.jsonl", PlayedLogText(false)}});
        ASSERT_NE(CompleteLog(), PlayedLogText(false));
        for (const char* seat : {"0", "2"}) {
            const CliRun complete = RunWith(reader, {"--log", "complete.jsonl", "--hand", "2", "--seat", seat, "--worlds", kFewWorlds});
            const CliRun bare = RunWith(reader, {"--log", "bare.jsonl", "--hand", "2", "--seat", seat, "--worlds", kFewWorlds});
            ASSERT_EQ(complete.code, Analyzer::kExitOk) << complete.err;
            ASSERT_EQ(bare.code, Analyzer::kExitOk) << bare.err;
            // The report names the file it came from: that is the one line allowed to differ.
            std::string normalized = bare.out;
            for (size_t at = normalized.find("bare.jsonl"); at != std::string::npos; at = normalized.find("bare.jsonl", at)) {
                normalized.replace(at, std::string("bare.jsonl").size(), "complete.jsonl");
            }
            EXPECT_EQ(complete.out, normalized) << "seat " << seat;
        }
    }

    TEST(AnalyzerCliTest, AHandWrittenPositionIsReadThroughTheReader) {
        const CliRun run = RunCli({"--position", kPositionName, "--worlds", kWorlds});
        EXPECT_EQ(run.code, Analyzer::kExitOk) << run.err;
        EXPECT_TRUE(Has(run.out, std::string("position: ") + kPositionName));
        EXPECT_TRUE(Has(run.out, "claim pung"));
        EXPECT_TRUE(Has(run.out, "pass"));
        EXPECT_FALSE(Has(run.out, "logged"));
    }

    TEST(AnalyzerCliTest, AFailureStopsWithExitOneAndNamesWhatFailed) {
        const Analyzer::FileReader reader = ReaderOf({{kLogName, CompleteLog()}, {"bad.txt", "seat 0\nhand 1p 2p 3p\nfrobnicate 3\n"}, {"empty.jsonl", ""}, {"junk.jsonl", "not json\n"}});
        const CliRun badText = RunWith(reader, {"--position", "bad.txt"});
        EXPECT_EQ(badText.code, Analyzer::kExitAnalysisFailed);
        EXPECT_TRUE(badText.out.empty());
        EXPECT_TRUE(Has(badText.err, "bad.txt: line ")) << badText.err;

        const CliRun missing = RunWith(reader, {"--position", "absent.txt"});
        EXPECT_EQ(missing.code, Analyzer::kExitAnalysisFailed);
        EXPECT_TRUE(Has(missing.err, "no such file"));

        const CliRun junk = RunWith(reader, {"--log", "junk.jsonl", "--hand", "1", "--action", "0"});
        EXPECT_EQ(junk.code, Analyzer::kExitAnalysisFailed);
        EXPECT_TRUE(Has(junk.err, "junk.jsonl"));

        const CliRun farHand = RunWith(reader, {"--log", kLogName, "--hand", "99", "--action", "0"});
        EXPECT_EQ(farHand.code, Analyzer::kExitAnalysisFailed);
        EXPECT_TRUE(Has(farHand.err, kLogName));
        const CliRun farAction = RunWith(reader, {"--log", kLogName, "--hand", "1", "--action", "99999"});
        EXPECT_EQ(farAction.code, Analyzer::kExitAnalysisFailed);
    }

}  // namespace BECore::Mahjong::Tests
