#include "HandTestSupport.h"

#include <BECore/Control/Json.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/MatchLogJson.h>
#include <Mahjong/MatchLogWriter.h>
#include <gtest/gtest.h>
#include <regex>
#include <vector>

/**
 * The file of a match log (`MatchLogWriter.h`): a match is written as JSON lines into a file of its own, the file read back and replayed
 * gives the match, and the game wires the writer to its session. Also the global environment that keeps every test of the binary from
 * leaving a `logs/` folder behind: the writer is pointed at a temporary folder for the whole process.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;
    namespace fs = std::filesystem;

    namespace {

        constexpr uint32_t kSeed = 41;
        constexpr int kHandLimitPerMatch = 200;
        constexpr int kSeedsToSearch = 200;
        constexpr int kStartsInARow = 3;
        constexpr int kControlStepLimit = 400;
        constexpr const char* kFolderStem = "bagi-mahjong-match-logs";
        /// A stamp and a name of the shape the writer makes, for the length of their parts only.
        constexpr std::string_view kExampleStamp = "2026-11-09T14-03-55";
        constexpr std::string_view kExampleStem = "2026-11-09T14-03-55_seed41";
        /// `2026-11-09T14-03-55_seed41.jsonl`, with `-2` ... before the extension for a second file of the same second.
        constexpr const char* kFileNamePattern = R"(\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}_seed41(-\d+)?\.jsonl)";

        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        /// The folder under the temporary directory that every test of the process writes its logs into (and that goes at the end).
        fs::path& SessionFolder() {
            static fs::path folder;
            return folder;
        }

        /// Points the writer at a fresh temporary folder for the whole run of the test binary, so that the many tests that start a
        /// `MahjongGame` (and so write a log) leave nothing in the build folder or the repository, and removes it at the end.
        class MatchLogFolderEnvironment final : public ::testing::Environment {
        public:
            void SetUp() override {
                _before = MatchLogDirectory();
                SessionFolder() = TestTempPath(kFolderStem);
                SetMatchLogDirectory(SessionFolder() / "default");
            }

            void TearDown() override {
                std::error_code error;
                fs::remove_all(SessionFolder(), error);
                SetMatchLogDirectory(_before);
            }

        private:
            fs::path _before;
        };

        [[maybe_unused]] ::testing::Environment* const kMatchLogFolderEnvironment = ::testing::AddGlobalTestEnvironment(new MatchLogFolderEnvironment);

        std::string ReadFile(const fs::path& path) {
            std::ifstream file(path);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        /// The files of `folder`, by name.
        std::vector<fs::path> FilesIn(const fs::path& folder) {
            std::vector<fs::path> files;
            std::error_code error;
            for (fs::directory_iterator it(folder, error); !error && it != fs::directory_iterator(); it.increment(error)) {
                files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        std::vector<std::string> LinesOf(const std::string& text) {
            std::vector<std::string> lines;
            std::istringstream stream(text);
            for (std::string line; std::getline(stream, line);) {
                lines.push_back(line);
            }
            return lines;
        }

        /// Sends the events to two sinks: the writer under test and an in-memory log to compare it with.
        class Tee final : public IMatchLogSink {
        public:
            explicit Tee(IMatchLogSink& writer) : _writer(writer) {}

            void OnEvent(const MatchLogEvent& event) override {
                _log.OnEvent(event);
                _writer.OnEvent(event);
            }

            const MatchLog& Log() const {
                return _log;
            }

        private:
            IMatchLogSink& _writer;
            MatchLog _log;
        };

        MahjongSettings ShortMatch() {
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            return settings;
        }

        /// A match, its session, the writer and the in-memory copy of what the writer was told.
        struct WrittenTable {
            Match match;
            TableSession session{match};
            MatchLogWriter writer;
            Tee tee{writer};

            WrittenTable() {
                session.SetLogSink(&tee);
            }

            void Start(uint32_t seed = kSeed) {
                ASSERT_TRUE(session.StartMatch(ShortMatch(), seed, kAllComputers).has_value());
            }

            /// The computers play every hand to the end.
            void PlayMatch() {
                for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kHandLimitPerMatch; ++hands) {
                    session.RunComputers();
                    if (session.GetPhase() == SessionPhase::HandOver) {
                        EXPECT_TRUE(session.NextHand().has_value());
                    }
                }
                EXPECT_EQ(session.GetPhase(), SessionPhase::MatchOver);
            }
        };

        void ExpectSameMatch(const Match& expected, const Match& actual) {
            ASSERT_EQ(expected.History().size(), actual.History().size());
            for (size_t hand = 0; hand < expected.History().size(); ++hand) {
                const HandSettlement& want = expected.History()[hand];
                const HandSettlement& got = actual.History()[hand];
                EXPECT_EQ(want.kind, got.kind) << "hand " << hand;
                EXPECT_EQ(want.winner, got.winner) << "hand " << hand;
                EXPECT_EQ(want.east, got.east) << "hand " << hand;
                EXPECT_EQ(want.scores, got.scores) << "hand " << hand;
                EXPECT_EQ(want.deltas, got.deltas) << "hand " << hand;
            }
            EXPECT_EQ(expected.Scores(), actual.Scores());
        }

        class MatchLogFileTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _before = MatchLogDirectory();
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                folder = SessionFolder() / info->name();
                SetMatchLogDirectory(folder);
            }

            void TearDown() override {
                SetMatchLogDirectory(_before);
            }

            fs::path folder;

        private:
            fs::path _before;
        };

        std::string Str(const Json& json) {
            return json.is_string() ? json.get<std::string>() : std::string("<not a string>");
        }

    }  // namespace

    TEST_F(MatchLogFileTest, TheEnvironmentPointsTheLogsAtATemporaryFolder) {
        EXPECT_FALSE(SessionFolder().empty());
        EXPECT_TRUE(folder.string().starts_with(SessionFolder().string()));
    }

    TEST_F(MatchLogFileTest, AMatchIsOneFileOfJsonLinesOneForEveryEvent) {
        WrittenTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        table.PlayMatch();

        const std::vector<fs::path> files = FilesIn(folder);
        ASSERT_EQ(files.size(), 1u);
        EXPECT_TRUE(std::regex_match(files[0].filename().string(), std::regex(kFileNamePattern))) << files[0].filename().string();
        EXPECT_EQ(files[0], table.writer.Path());
        EXPECT_FALSE(table.writer.IsOpen()) << "the file is closed at End";

        const std::vector<std::string> lines = LinesOf(ReadFile(files[0]));
        ASSERT_EQ(lines.size(), table.tee.Log().Events().size());
        for (size_t index = 0; index < lines.size(); ++index) {
            const auto parsed = ParseJson(lines[index].c_str());
            ASSERT_TRUE(parsed.has_value()) << "line " << index + 1;
            EXPECT_TRUE(parsed->is_object()) << "line " << index + 1;
            EXPECT_NE(FindJsonMember(*parsed, "event"), nullptr) << "line " << index + 1;
        }
        EXPECT_EQ(Str(*FindJsonMember(*ParseJson(lines.front().c_str()), "event")), "start");
        EXPECT_EQ(Str(*FindJsonMember(*ParseJson(lines.back().c_str()), "event")), "end");
    }

    TEST_F(MatchLogFileTest, TheFileReadBackAndReplayedGivesTheMatch) {
        WrittenTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        table.PlayMatch();
        ASSERT_EQ(table.match.History().size(), static_cast<size_t>(kShortMatchHands));

        const std::vector<fs::path> files = FilesIn(folder);
        ASSERT_EQ(files.size(), 1u);
        const auto events = ParseMatchLog(ReadFile(files[0]).c_str());
        ASSERT_TRUE(events.has_value()) << events.error().c_str();
        EXPECT_EQ(events->size(), table.tee.Log().Events().size());

        Match replayed;
        const ReplayStatus status = ReplayMatchLog(*events, replayed);
        ASSERT_TRUE(status.has_value()) << "event " << status.error().index << ": " << EnumUtils<ReplayProblem>::ToString(status.error().problem).data();
        ExpectSameMatch(table.match, replayed);
    }

    TEST_F(MatchLogFileTest, TheSameSeedInTheSameSecondGetsAFileOfItsOwn) {
        WrittenTable table;
        std::vector<fs::path> paths;
        for (int start = 0; start < kStartsInARow; ++start) {
            ASSERT_NO_FATAL_FAILURE(table.Start());
            paths.push_back(table.writer.Path());
        }
        const std::vector<fs::path> files = FilesIn(folder);
        EXPECT_EQ(files.size(), static_cast<size_t>(kStartsInARow));
        std::sort(paths.begin(), paths.end());
        EXPECT_EQ(std::unique(paths.begin(), paths.end()), paths.end()) << "two matches share a file";
        const std::regex fileName(kFileNamePattern);
        for (const fs::path& file : files) {
            EXPECT_TRUE(std::regex_match(file.filename().string(), fileName)) << file.filename().string();
        }
        // the suffix exists exactly for the files that share their second with an earlier one
        std::vector<std::string> stamps;
        for (const fs::path& file : files) {
            stamps.push_back(file.filename().string().substr(0, kExampleStamp.size()));
        }
        std::sort(stamps.begin(), stamps.end());
        const auto distinctStamps = static_cast<size_t>(std::unique(stamps.begin(), stamps.end()) - stamps.begin());
        const auto suffixed =
            static_cast<size_t>(std::count_if(files.begin(), files.end(), [](const fs::path& file) { return file.stem().string().find("-", kExampleStem.size()) != std::string::npos; }));
        EXPECT_EQ(suffixed, files.size() - distinctStamps);
    }

    TEST_F(MatchLogFileTest, AnEmptyFolderSettingWritesNothingAndTheMatchGoesOn) {
        SetMatchLogDirectory({});
        WrittenTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        table.PlayMatch();
        EXPECT_TRUE(table.writer.Path().empty());
        EXPECT_FALSE(table.writer.IsOpen());
        EXPECT_FALSE(fs::exists(folder));
        EXPECT_EQ(table.match.History().size(), static_cast<size_t>(kShortMatchHands));
    }

    TEST_F(MatchLogFileTest, AFolderThatCannotBeMadeDoesNotStopTheMatch) {
        std::error_code error;
        fs::create_directories(folder, error);
        const fs::path blocker = folder / "not-a-folder";
        std::ofstream(blocker) << "a regular file where the folder should be";
        SetMatchLogDirectory(blocker / "matches");

        WrittenTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        table.PlayMatch();
        EXPECT_TRUE(table.writer.Path().empty());
        EXPECT_FALSE(table.writer.IsOpen());
        EXPECT_EQ(table.match.History().size(), static_cast<size_t>(kShortMatchHands));
        EXPECT_EQ(FilesIn(folder).size(), 1u) << "only the blocker";

        // the next match, with a folder that works, is written again
        SetMatchLogDirectory(folder / "good");
        ASSERT_NO_FATAL_FAILURE(table.Start());
        EXPECT_TRUE(table.writer.IsOpen());
    }

    TEST_F(MatchLogFileTest, TheFileIsCompleteWhileTheMatchIsStillInProgress) {
        WrittenTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        table.session.RunComputers();
        ASSERT_EQ(table.session.GetPhase(), SessionPhase::HandOver);
        ASSERT_TRUE(table.writer.IsOpen());

        const auto events = ParseMatchLog(ReadFile(table.writer.Path()).c_str());
        ASSERT_TRUE(events.has_value()) << events.error().c_str();
        EXPECT_EQ(events->size(), table.tee.Log().Events().size()) << "every line is flushed when it is written";
        EXPECT_EQ(events->front().kind, MatchLogKind::Start);
        for (const MatchLogEvent& event : *events) {
            EXPECT_NE(event.kind, MatchLogKind::End);
        }
    }

    TEST_F(MatchLogFileTest, ANewMatchClosesTheFileOfTheOldOne) {
        WrittenTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kSeed));
        const fs::path first = table.writer.Path();
        table.session.RunComputers();
        ASSERT_NO_FATAL_FAILURE(table.Start(kSeed + 1));
        EXPECT_NE(table.writer.Path(), first);
        EXPECT_EQ(FilesIn(folder).size(), 2u);
        // the first file holds the start of its match and what was played, and no end
        const auto events = ParseMatchLog(ReadFile(first).c_str());
        ASSERT_TRUE(events.has_value());
        EXPECT_EQ(events->front().seed, kSeed);
        EXPECT_NE(events->back().kind, MatchLogKind::End);
    }

    namespace {

        class MahjongGameLogTest : public MatchLogFileTest {
        protected:
            /// A started match in which seat 0 holds East, so that the person at seat 0 acts first; returns its seed.
            uint32_t StartWithEastAtSeat0(MahjongGame& game) {
                game.SetSeats({SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer});
                for (uint32_t seed = 1; seed <= kSeedsToSearch; ++seed) {
                    EXPECT_TRUE(game.StartMatch(seed).has_value());
                    if (game.GetMatch().East() == kSeat0) {
                        return seed;
                    }
                }
                ADD_FAILURE() << "no seed gives East to seat 0";
                return 1;
            }
        };

    }  // namespace

    TEST_F(MahjongGameLogTest, EveryStartOfTheGameWritesAFileAndThePersonsActionIsInIt) {
        auto game = New<MahjongGame>();
        const uint32_t seed = StartWithEastAtSeat0(*game);
        ASSERT_TRUE(game->GetMatchLog().IsOpen());
        EXPECT_EQ(game->GetMatchLog().Path().parent_path(), folder);
        EXPECT_NE(game->GetMatchLog().Path().filename().string().find("_seed" + std::to_string(seed)), std::string::npos);

        // a control reset begins another match, with its own file
        const fs::path first = game->GetMatchLog().Path();
        ASSERT_TRUE(game->AsControllable()->Reset(ParseJson(R"({"seed": 5, "seats": "PCCC"})").value()).has_value());
        EXPECT_NE(game->GetMatchLog().Path(), first);

        const uint32_t again = StartWithEastAtSeat0(*game);
        const Json actions = game->AsControllable()->GetActions().value();
        ASSERT_FALSE(actions.empty());
        ASSERT_TRUE(game->AsControllable()->PerformAction(actions.front()).has_value());

        const auto events = ParseMatchLog(ReadFile(game->GetMatchLog().Path()).c_str());
        ASSERT_TRUE(events.has_value()) << events.error().c_str();
        EXPECT_EQ(events->front().kind, MatchLogKind::Start);
        EXPECT_EQ(events->front().seed, again);
        const auto person = std::find_if(events->begin(), events->end(), [](const MatchLogEvent& event) { return event.kind == MatchLogKind::Action && event.by == SeatKind::Person; });
        ASSERT_NE(person, events->end()) << "the person's action is not in the log";
        EXPECT_EQ(person->seat, kSeat0);
        EXPECT_EQ(ActionToJson(person->action), actions.front());
        // and the computers' answers follow it
        EXPECT_TRUE(std::any_of(person, events->end(), [](const MatchLogEvent& event) { return event.kind == MatchLogKind::Action && event.by == SeatKind::Computer; }));
    }

    TEST_F(MahjongGameLogTest, TheControlStateIsTheSameWithTheLogOnAndOff) {
        const uint32_t seed = StartWithEastAtSeat0(*New<MahjongGame>());  // the search starts matches, so the games below start only once
        auto logged = New<MahjongGame>();
        logged->SetSeats({SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer});
        ASSERT_TRUE(logged->StartMatch(seed).has_value());
        ASSERT_TRUE(logged->GetMatchLog().IsOpen());

        SetMatchLogDirectory({});
        auto silent = New<MahjongGame>();
        silent->SetSeats(logged->GetSeats());
        ASSERT_TRUE(silent->StartMatch(seed).has_value());
        EXPECT_FALSE(silent->GetMatchLog().IsOpen());

        // a whole hand: the person plays the first listed action until the hand is over
        for (int step = 0; step < kControlStepLimit; ++step) {
            const Json state = logged->AsControllable()->GetState().value();
            ASSERT_EQ(state, silent->AsControllable()->GetState().value()) << "step " << step;
            if (state["phase"] != "playing") {
                break;
            }
            const Json actions = logged->AsControllable()->GetActions().value();
            ASSERT_EQ(actions, silent->AsControllable()->GetActions().value());
            if (actions.empty()) {
                break;
            }
            const auto a = logged->AsControllable()->PerformAction(actions.front());
            const auto b = silent->AsControllable()->PerformAction(actions.front());
            ASSERT_TRUE(a.has_value());
            ASSERT_TRUE(b.has_value());
            EXPECT_EQ(*a, *b);
        }
        EXPECT_NE(logged->AsControllable()->GetState().value()["phase"], "playing") << "the hand was played to its end";
    }

    TEST(MatchLogDirectoryTest, TheDefaultFolderIsLogsMatchesAndNoneInTheBrowser) {
        EXPECT_EQ(DefaultMatchLogDirectory(false), fs::path("logs/matches"));
        EXPECT_TRUE(DefaultMatchLogDirectory(true).empty());
    }

}  // namespace BECore::Mahjong::Tests
