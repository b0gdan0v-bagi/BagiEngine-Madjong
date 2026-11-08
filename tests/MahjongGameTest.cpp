#include "HandTestSupport.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Games/GameManager.h>
#include <Mahjong/MahjongGame.h>
#include <gtest/gtest.h>

/**
 * The game is registered and starts on its scene: the shipped SceneConfig.xml is read from the source tree (a game's content is not mounted in the
 * test environment) and the game is started through GameManager like the executable does. Needs the engine services.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr uint32_t kSeed = 11;
        /// Not a positive multiple of 4, so `StartMatch` refuses it.
        constexpr int kUnplayableHandsPerMatch = 6;

        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

    }  // namespace

    class MahjongGameTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(ShippedSceneConfig())) << ShippedSceneConfig().string();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);  // GameManager switches to the scene of the game through CoreManager
            GameManager::GetInstance().Initialize();
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
        }

        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(MahjongGameTest, IsTheActiveGame) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);
        EXPECT_EQ(game->GetName(), "Mahjong"_intern);
        EXPECT_EQ(game->GetSceneName(), "MahjongScene"_intern);
    }

    TEST_F(MahjongGameTest, ItsSceneIsShippedAndActive) {
        const PoolString sceneName = "MahjongScene"_intern;
        ASSERT_NE(_manager->GetSceneByName(sceneName), nullptr);
        Scene* active = _manager->GetActiveScene();
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(active->GetName(), sceneName);
    }

    TEST_F(MahjongGameTest, TheActiveGameOwnsAStartedMatch) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);
        const Match& match = game->GetMatch();

        EXPECT_EQ(match.GetPhase(), MatchPhase::InHand);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(match.Scores()[seat], kExpectedStartingScore) << "seat " << seat;
        }
        ASSERT_TRUE(IsSeat(match.East()));
        EXPECT_EQ(match.Hand().GetPhase(), Phase::Discard);
        EXPECT_EQ(match.Hand().East(), match.East());
        EXPECT_EQ(match.HandsPlayed(), 0);
    }

    TEST_F(MahjongGameTest, StartMatchWithOneSeedGivesTheSameMatch) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);

        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        const Seat east = game->GetMatch().East();
        const SeatWinds winds = game->GetMatch().Winds();
        const SeatScores scores = game->GetMatch().Scores();
        const std::string hand = Snapshot(game->GetMatch().Hand());
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());

        EXPECT_EQ(game->GetMatch().East(), east);
        EXPECT_EQ(game->GetMatch().Winds(), winds);
        EXPECT_EQ(game->GetMatch().Scores(), scores);
        EXPECT_EQ(Snapshot(game->GetMatch().Hand()), hand);
        EXPECT_EQ(game->GetMatch().GetPhase(), MatchPhase::InHand);
    }

    TEST_F(MahjongGameTest, ResetBeginsANewMatch) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        HandSettlement settlement;
        settlement.kind = ResultKind::Drawn;
        settlement.winner = kNoSeat;
        settlement.deltas = SeatScores{};
        ASSERT_TRUE(game->GetMatch().ApplySettlement(settlement).has_value());
        ASSERT_FALSE(game->GetMatch().History().empty());

        game->Reset();

        EXPECT_EQ(game->GetMatch().GetPhase(), MatchPhase::InHand);
        EXPECT_TRUE(game->GetMatch().History().empty());
        EXPECT_EQ(game->GetMatch().HandsPlayed(), 0);
        EXPECT_EQ(Sum(game->GetMatch().Scores()), kSeatCount * kExpectedStartingScore);
    }

    TEST_F(MahjongGameTest, TheGameOwnsASessionInPlayAfterTheStart) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);
        const TableSession& session = game->GetSession();

        EXPECT_EQ(session.GetPhase(), SessionPhase::Playing);
        EXPECT_GT(session.Version(), 0u);
        EXPECT_EQ(session.Seats(), kDefaultSeatSetup);
        EXPECT_EQ(session.Actor(), game->GetMatch().East());
    }

    TEST_F(MahjongGameTest, StartMatchGoesThroughTheSessionWithTheSettingsAndTheSeats) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        SeatSetup seats = kDefaultSeatSetup;
        seats[0] = SeatKind::Computer;
        game->SetSettings(settings);
        game->SetSeats(seats);

        const uint64_t version = game->GetSession().Version();
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());

        EXPECT_GT(game->GetSession().Version(), version);
        EXPECT_EQ(game->GetMatch().HandsTotal(), kShortMatchHands);
        EXPECT_EQ(game->GetSession().Seats(), seats);
        EXPECT_EQ(game->GetSession().PersonCount(), 0);

        // Settings that cannot be played are refused and the match in play is left as it is.
        settings.handsPerMatch = kUnplayableHandsPerMatch;
        game->SetSettings(settings);
        const std::string hand = Snapshot(game->GetMatch().Hand());
        const MatchStatus refused = game->StartMatch(kSeed);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), MatchError::BadSettings);
        EXPECT_EQ(Snapshot(game->GetMatch().Hand()), hand);
    }

    TEST_F(MahjongGameTest, ResetKeepsTheSeatSetup) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);
        SeatSetup seats = kDefaultSeatSetup;
        seats[2] = SeatKind::Person;
        game->SetSeats(seats);

        game->Reset();

        EXPECT_EQ(game->GetSession().Seats(), seats);
        EXPECT_EQ(game->GetSession().PersonCount(), 2);
        EXPECT_EQ(game->GetSession().GetPhase(), SessionPhase::Playing);
        EXPECT_EQ(game->GetSeats(), seats);
    }

}  // namespace BECore::Mahjong::Tests
