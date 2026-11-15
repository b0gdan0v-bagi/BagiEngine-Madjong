#include <Common/EngineEnvironment.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MahjongSetState.h>
#include <Mahjong/MatchLogJson.h>
#include <gtest/gtest.h>
#include <tests/FixedWall.h>

/** The reader of the `set_state` params (`MahjongSetState.h`): a pure function of the JSON and the game's defaults. */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr uint32_t kDefaultSeed = 5;
        constexpr uint32_t kGivenSeed = 4000000000;
        constexpr int kSecondHandPlayed = 5;
        constexpr int kLastHandOfAShortMatch = kShortMatchHands - 1;
        constexpr const char* kInvalidParams = "invalid_params";

        class MahjongSetStateTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            static SetStateDefaults Defaults() {
                SetStateDefaults defaults;
                defaults.seats = {SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
                defaults.levels = UniformLevels(ComputerLevel::Easy);
                defaults.settings.limit = 300;
                defaults.seed = kDefaultSeed;
                return defaults;
            }

            /// A state with the sample position's deal only.
            static Json Base() {
                Json state = Json::object();
                state["deal"] = FixedDealToJson(MakeSamplePosition().deal);
                return state;
            }

            static std::expected<SetStateDeal, ControlError> Read(const Json& state) {
                return ParseSetState(state, Defaults());
            }

            /// `state` is refused as `invalid_params` and the message holds `fragment`.
            static void ExpectRefused(const Json& state, const char* fragment) {
                const auto read = Read(state);
                ASSERT_FALSE(read.has_value()) << DumpJson(state).c_str();
                EXPECT_EQ(String(read.error().code.c_str(), read.error().code.size()), kInvalidParams) << DumpJson(state).c_str();
                EXPECT_NE(String(read.error().message.c_str(), read.error().message.size()).find(fragment), String::npos)
                    << "\"" << read.error().message.c_str() << "\" lacks \"" << fragment << "\" for " << DumpJson(state).c_str();
            }
        };

    }  // namespace

    TEST_F(MahjongSetStateTest, ADealAloneTakesTheGamesChoices) {
        const auto read = Read(Base());
        ASSERT_TRUE(read.has_value());
        const SetStateDefaults defaults = Defaults();
        EXPECT_EQ(read->start.deal, MakeSamplePosition().deal);
        EXPECT_EQ(read->start.handsPlayed, 0);
        EXPECT_EQ(read->seed, kDefaultSeed);
        EXPECT_EQ(read->seats, defaults.seats);
        EXPECT_EQ(read->levels, defaults.levels);
        EXPECT_EQ(read->settings.limit, defaults.settings.limit);
        for (const int score : read->start.scores) {
            EXPECT_EQ(score, defaults.settings.startingScore);
        }
    }

    TEST_F(MahjongSetStateTest, EveryKeyIsRead) {
        Json state = Base();
        state["seed"] = kGivenSeed;
        state["seats"] = "PPCC";
        state["levels"] = Json::array({1, 4, 9, 10});
        state["settings"] = Json::parse(R"({"handsPerMatch": 8, "flowers": true, "replayDrawnHand": false, "limit": 1000, "startingScore": 1500})");
        state["scores"] = Json::array({10, -20, 30, 40});
        state["handsPlayed"] = kSecondHandPlayed;

        const auto read = Read(state);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->seed, kGivenSeed);
        EXPECT_EQ(SeatsToLetters(read->seats), "PPCC");
        EXPECT_EQ(LevelNumber(read->levels[1]), 4);
        EXPECT_EQ(LevelNumber(read->levels[3]), 10);
        EXPECT_EQ(read->settings.handsPerMatch, kShortMatchHands);
        EXPECT_TRUE(read->settings.flowers);
        EXPECT_FALSE(read->settings.replayDrawnHand);
        EXPECT_EQ(read->settings.limit, 1000);
        EXPECT_EQ(read->settings.startingScore, 1500);
        EXPECT_EQ(read->start.scores, (SeatScores{10, -20, 30, 40}));
        EXPECT_EQ(read->start.handsPlayed, kSecondHandPlayed);
    }

    TEST_F(MahjongSetStateTest, SettingsLeftOutKeepTheGames) {
        Json state = Base();
        state["settings"] = Json::parse(R"({"flowers": true})");
        const auto read = Read(state);
        ASSERT_TRUE(read.has_value());
        EXPECT_TRUE(read->settings.flowers);
        EXPECT_EQ(read->settings.limit, Defaults().settings.limit);
        EXPECT_EQ(read->settings.handsPerMatch, Defaults().settings.handsPerMatch);
    }

    TEST_F(MahjongSetStateTest, OneLevelIsForEverySeat) {
        Json state = Base();
        state["level"] = 7;
        const auto read = Read(state);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->levels, UniformLevels(*LevelOf(7)));
    }

    TEST_F(MahjongSetStateTest, ATileListIsAnArrayOrOneText) {
        Json state = Base();
        state["deal"]["live"] = Json::array({"Gd", "Gd", "9m"});
        state["deal"]["loose"] = "4s 5s";
        const auto read = Read(state);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->start.deal, MakeSamplePosition().deal);
    }

    TEST_F(MahjongSetStateTest, AStateOfAMatchLogIsAValidSetState) {
        const FixedStart position = MakeSamplePosition(2, kSecondHandPlayed);
        Json state = Json::object();
        state["deal"] = FixedDealToJson(position.deal);
        state["handsPlayed"] = position.handsPlayed;
        state["scores"] = SeatScoresToJson(position.scores);
        const auto read = Read(state);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->start, position);
    }

    TEST_F(MahjongSetStateTest, TheLastHandOfAShortMatchIsPlayable) {
        Json state = Base();
        state["settings"] = Json::parse(R"({"handsPerMatch": 8})");
        state["handsPlayed"] = kLastHandOfAShortMatch;
        const auto read = Read(state);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->start.handsPlayed, kLastHandOfAShortMatch);
    }

    TEST_F(MahjongSetStateTest, WhatIsNotAnObjectIsRefused) {
        ExpectRefused(Json::array(), "object");
        ExpectRefused(Json(3), "object");
        ExpectRefused(Json(nullptr), "object");
    }

    TEST_F(MahjongSetStateTest, TheFormIsOneOfTwo) {
        ExpectRefused(Json::object(), "'deal'");
        Json both = Base();
        both["log"] = "";
        ExpectRefused(both, "together");
    }

    TEST_F(MahjongSetStateTest, ABadDealIsRefusedWithTheMemberNamed) {
        Json state = Base();
        state["deal"] = 5;
        ExpectRefused(state, "deal");

        state = Base();
        state["deal"]["hands"][2] = "1p 2p 3p 4p 5p 6p 7p 8p 9p Rd Wd Gd";
        ExpectRefused(state, "hands[2]");

        state = Base();
        state["deal"]["hands"][1] = "1m 2m 3m 4m 5m 6m 7m 8m 9m Ew Sw Ww 9z";
        ExpectRefused(state, "hands[1]");

        state = Base();
        state["deal"]["hands"].erase(3);
        ExpectRefused(state, "hands");

        state = Base();
        state["deal"]["east"] = 4;
        ExpectRefused(state, "east");

        state = Base();
        state["deal"]["live"] = Json::array({"1p", 5});
        ExpectRefused(state, "live");
    }

    TEST_F(MahjongSetStateTest, AFifthCopyAndABonusTileWithoutFlowersAreRefusedByTheWall) {
        Json state = Base();
        state["deal"]["live"] = "1p 1p 1p";  // hands 0 and 2 hold two copies already
        ExpectRefused(state, "1p");

        state = Base();
        state["deal"]["live"] = "F1";
        ExpectRefused(state, "F1");

        state["settings"] = Json::parse(R"({"flowers": true})");
        EXPECT_TRUE(Read(state).has_value());
    }

    TEST_F(MahjongSetStateTest, ALiveWallThatIsTooShortIsRefused) {
        Json state = Base();
        state["deal"]["liveAfterDeal"] = -3;
        ExpectRefused(state, "liveAfterDeal");
    }

    TEST_F(MahjongSetStateTest, HandsPlayedIsInsideTheMatch) {
        Json state = Base();
        state["handsPlayed"] = -1;
        ExpectRefused(state, "handsPlayed");
        state["handsPlayed"] = kFullMatchHands;
        ExpectRefused(state, "handsPlayed");
        state["handsPlayed"] = 1.5;
        ExpectRefused(state, "handsPlayed");
        state["handsPlayed"] = "3";
        ExpectRefused(state, "handsPlayed");
        state["settings"] = Json::parse(R"({"handsPerMatch": 8})");
        state["handsPlayed"] = kShortMatchHands;
        ExpectRefused(state, "handsPlayed");
    }

    TEST_F(MahjongSetStateTest, ScoresAreFourIntegers) {
        Json state = Base();
        state["scores"] = Json::array({1, 2, 3});
        ExpectRefused(state, "scores");
        state["scores"] = Json::array({1, 2, 3, 4, 5});
        ExpectRefused(state, "scores");
        state["scores"] = Json::array({1, 2, 3, 1.5});
        ExpectRefused(state, "scores");
        state["scores"] = Json::array({1, 2, 3, "4"});
        ExpectRefused(state, "scores");
        state["scores"] = Json::array({1, 2, 3, kMaxSetStateScore + 1});
        ExpectRefused(state, "scores");
        state["scores"] = 100;
        ExpectRefused(state, "scores");
    }

    TEST_F(MahjongSetStateTest, TheSeatsAndLevelsAreChecked) {
        Json state = Base();
        state["seats"] = "PCC";
        ExpectRefused(state, "seats");
        state["seats"] = "PCCX";
        ExpectRefused(state, "seats");
        state["seats"] = 4;
        ExpectRefused(state, "seats");

        state = Base();
        state["level"] = 0;
        ExpectRefused(state, "'level'");
        state["level"] = 11;
        ExpectRefused(state, "'level'");

        state = Base();
        state["levels"] = Json::array({1, 2, 3});
        ExpectRefused(state, "'levels'");
        state["levels"] = Json::array({1, 2, 3, 11});
        ExpectRefused(state, "'levels'");

        state = Base();
        state["level"] = 3;
        state["levels"] = Json::array({1, 2, 3, 4});
        ExpectRefused(state, "together");
    }

    TEST_F(MahjongSetStateTest, TheSeedIsAnUnsigned32BitInteger) {
        Json state = Base();
        state["seed"] = -1;
        ExpectRefused(state, "'seed'");
        state["seed"] = 4294967296LL;
        ExpectRefused(state, "'seed'");
        state["seed"] = "7";
        ExpectRefused(state, "'seed'");
    }

    TEST_F(MahjongSetStateTest, TheSettingsAreChecked) {
        Json state = Base();
        state["settings"] = 8;
        ExpectRefused(state, "'settings'");
        state["settings"] = Json::parse(R"({"handsPerMatch": 10})");
        ExpectRefused(state, "handsPerMatch");
        state["settings"] = Json::parse(R"({"handsPerMatch": "8"})");
        ExpectRefused(state, "handsPerMatch");
        state["settings"] = Json::parse(R"({"flowers": 1})");
        ExpectRefused(state, "flowers");
        state["settings"] = Json::parse(R"({"replayDrawnHand": "yes"})");
        ExpectRefused(state, "replayDrawnHand");
        state["settings"] = Json::parse(R"({"limit": -1})");
        ExpectRefused(state, "limit");
        state["settings"] = Json::parse(R"({"limit": 100000000000})");
        ExpectRefused(state, "limit");
        state["settings"] = Json::parse(R"({"startingScore": 0.5})");
        ExpectRefused(state, "startingScore");
    }

}  // namespace BECore::Mahjong::Tests
