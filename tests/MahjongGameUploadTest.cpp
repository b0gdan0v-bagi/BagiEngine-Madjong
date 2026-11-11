#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/MatchLogJson.h>
#include <gtest/gtest.h>
#include <regex>

/**
 * The game owns the recorder of the tester upload (`MahjongGame::GetUpload()`, `MatchUpload.h`) in the chain of its session: with the
 * defaults nothing is recorded and the file log is what it was; with a sink and a consent of `Granted` a match played through the game
 * reaches the sink as pieces that assemble and replay, and `Stop()` gives a match up. The game is built directly (no scene).
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;
    namespace fs = std::filesystem;

    namespace {

        constexpr uint32_t kSeed = 7;
        constexpr uint32_t kOtherSeed = 8;
        constexpr int kHandLimitPerMatch = 200;
        constexpr const char* kFolderStem = "bagi-mahjong-game-upload";
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        class FakeSink final : public IMatchUploadSink {
        public:
            void Send(const UploadPiece& piece) override {
                pieces.push_back(piece);
            }

            eastl::vector<UploadPiece> pieces;
        };

        std::string ReadFile(const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        class MahjongGameUploadTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _logsBefore = MatchLogDirectory();
                folder = TestTempPath(kFolderStem);
                SetMatchLogDirectory(folder);
                game = New<MahjongGame>();
                game->SetSeats(kAllComputers);
                game->SetSettings(Short());
            }

            void TearDown() override {
                game.Reset();
                SetMatchLogDirectory(_logsBefore);
                std::error_code error;
                fs::remove_all(folder, error);
            }

            static MahjongSettings Short() {
                MahjongSettings settings;
                settings.handsPerMatch = kShortMatchHands;
                return settings;
            }

            void Upload() {
                game->GetUpload().SetSink(&sink);
                game->GetUpload().SetConsent(UploadConsent::Granted, false);
            }

            void PlayHand() {
                TableSession& session = game->GetSession();
                session.RunComputers();
                ASSERT_EQ(session.GetPhase(), SessionPhase::HandOver);
                ASSERT_TRUE(session.NextHand().has_value());
            }

            void PlayMatch() {
                TableSession& session = game->GetSession();
                for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kHandLimitPerMatch; ++hands) {
                    session.RunComputers();
                    if (session.GetPhase() == SessionPhase::HandOver) {
                        ASSERT_TRUE(session.NextHand().has_value());
                    }
                }
                ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);
            }

            static void ExpectReplays(const eastl::string& text) {
                const auto events = ParseMatchLog(std::string_view(text.c_str(), text.size()));
                ASSERT_TRUE(events.has_value()) << events.error().c_str();
                Match replayed;
                const ReplayStatus status = ReplayMatchLog(*events, replayed);
                EXPECT_TRUE(status.has_value()) << "event " << status.error().index;
            }

            IntrusivePtr<MahjongGame> game;
            FakeSink sink;
            fs::path folder;

        private:
            fs::path _logsBefore;
        };

    }  // namespace

    TEST_F(MahjongGameUploadTest, WithTheDefaultsTheGameSendsNothingAndItsFileLogIsTheWholeMatch) {
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        game->Stop();
        EXPECT_FALSE(game->GetUpload().IsRecording());
        EXPECT_EQ(game->GetUpload().Consent(), UploadConsent::Unasked);

        // even with a sink set, no consent is nothing
        game->GetUpload().SetSink(&sink);
        ASSERT_TRUE(game->StartMatch(kOtherSeed).has_value());
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        game->Stop();
        EXPECT_TRUE(sink.pieces.empty());

        const fs::path file = game->GetMatchLog().Path();
        ASSERT_FALSE(file.empty());
        const auto events = ParseMatchLog(ReadFile(file));
        ASSERT_TRUE(events.has_value());
        EXPECT_EQ(events->back().kind, MatchLogKind::End);
        EXPECT_EQ(events->front().seed, kOtherSeed);
    }

    TEST_F(MahjongGameUploadTest, APlayedMatchReachesTheSinkAsPiecesThatAssembleAndReplay) {
        Upload();
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        ASSERT_NO_FATAL_FAILURE(PlayMatch());

        ASSERT_EQ(sink.pieces.size(), static_cast<size_t>(kShortMatchHands) + 1);
        EXPECT_EQ(sink.pieces.back().end, UploadPieceEnd::Ended);
        for (const UploadPiece& piece : sink.pieces) {
            EXPECT_FALSE(piece.consentRemembered);
        }
        const auto assembled = AssemblePieces(sink.pieces);
        ASSERT_TRUE(assembled.has_value()) << assembled.error().c_str();
        ExpectReplays(*assembled);
        EXPECT_EQ(std::string(assembled->c_str(), assembled->size()), ReadFile(game->GetMatchLog().Path())) << "the writer still gets every event";
    }

    TEST_F(MahjongGameUploadTest, StopInTheMiddleOfAMatchSendsAnAbandonedPiece) {
        Upload();
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        ASSERT_NO_FATAL_FAILURE(PlayHand());
        game->GetSession().RunComputers();
        game->Stop();

        ASSERT_FALSE(sink.pieces.empty());
        EXPECT_EQ(sink.pieces.back().end, UploadPieceEnd::Abandoned);
        const auto assembled = AssemblePieces(sink.pieces);
        ASSERT_TRUE(assembled.has_value()) << assembled.error().c_str();
        ExpectReplays(*assembled);

        const size_t sent = sink.pieces.size();
        game->Stop();
        EXPECT_EQ(sink.pieces.size(), sent) << "a second Stop has nothing to give up";
    }

    TEST_F(MahjongGameUploadTest, TheMatchDealtBehindThePanelIsNotUploaded) {
        Upload();
        game->GetUpload().SetRecording(false);  // what BeginMatch(seed, false) does for the match Start() deals
        ASSERT_TRUE(game->GetSession().StartMatch(Short(), kSeed, kAllComputers).has_value());
        EXPECT_FALSE(game->GetUpload().IsRecording());
        ASSERT_NO_FATAL_FAILURE(PlayHand());
        game->Stop();
        EXPECT_TRUE(sink.pieces.empty());
    }

    TEST_F(MahjongGameUploadTest, TheMatchIdIs32HexDigitsAndTwoMatchesDiffer) {
        Upload();
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        ASSERT_NO_FATAL_FAILURE(PlayHand());
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());  // the same seed: the id has nothing of it
        ASSERT_NO_FATAL_FAILURE(PlayHand());
        game->Stop();

        std::vector<std::string> ids;
        for (const UploadPiece& piece : sink.pieces) {
            const std::string id(piece.matchId.c_str(), piece.matchId.size());
            if (ids.empty() || ids.back() != id) {
                ids.push_back(id);
            }
        }
        ASSERT_EQ(ids.size(), 2u);
        EXPECT_NE(ids[0], ids[1]);
        const std::regex hex32("[0-9a-f]{32}");
        for (const std::string& id : ids) {
            EXPECT_TRUE(std::regex_match(id, hex32)) << id;
        }
    }

}  // namespace BECore::Mahjong::Tests
