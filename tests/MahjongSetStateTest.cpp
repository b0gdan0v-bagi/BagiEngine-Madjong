#include <Common/EngineEnvironment.h>
#include <Mahjong/MahjongGame.h>
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
        constexpr uint32_t kLogSeed = 41;
        constexpr uint32_t kGivenSeed = 4000000000;
        constexpr int kSecondHandPlayed = 5;
        constexpr int kLastHandOfAShortMatch = kShortMatchHands - 1;
        constexpr const char* kInvalidParams = "invalid_params";

        /// The log of the first hand of a match of four computers (the `Start`, the hand, the actions, its settlement and the next hand
        /// dealt), as the text of a `.jsonl` file.
        String LogText(Vector<MatchLogEvent>* events = nullptr) {
            Match match;
            TableSession session(match);
            MatchLog log;
            session.SetLogSink(&log);
            const SeatSetup computers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
            EXPECT_TRUE(session.StartMatch(MahjongSettings{}, kLogSeed, computers, ComputerLevel::Easy).has_value());
            session.RunComputers();
            EXPECT_TRUE(session.NextHand().has_value());
            String text;
            for (const MatchLogEvent& event : log.Events()) {
                text += DumpJson(EventToJson(event));
                text += '\n';
            }
            if (events != nullptr) {
                *events = log.Events();
            }
            return text;
        }

        String TextOf(const Vector<MatchLogEvent>& events) {
            String text;
            for (const MatchLogEvent& event : events) {
                text += DumpJson(EventToJson(event));
                text += '\n';
            }
            return text;
        }

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

            static std::expected<SetStateRequest, ControlError> Parse(const Json& state) {
                return ParseSetState(state, Defaults());
            }

            /// The deal of a state that has one (a failure, and an empty deal, when it is the other form).
            static std::expected<SetStateDeal, ControlError> Read(const Json& state) {
                auto request = Parse(state);
                if (!request) {
                    return std::unexpected(request.error());
                }
                const SetStateDeal* deal = std::get_if<SetStateDeal>(&*request);
                EXPECT_NE(deal, nullptr);
                return deal ? *deal : SetStateDeal{};
            }

            /// `state` is refused as `invalid_params` and the message holds `fragment`.
            static void ExpectRefused(const Json& state, const char* fragment) {
                const auto read = Parse(state);
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
        state["scores"] = Json::array({1, 2, 3, kMaxScore + 1});
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

    // ----- the form with a log -----

    TEST_F(MahjongSetStateTest, ALogTakesTheSeatsAndLevelsOfTheCall) {
        Vector<MatchLogEvent> played;
        const String text = LogText(&played);
        Json state = Json::object();
        state["log"] = MakeJsonString(StringView(text.c_str(), text.size()));
        state["seats"] = "PPCC";
        state["levels"] = Json::array({3, 4, 5, 6});

        const auto request = Parse(state);
        ASSERT_TRUE(request.has_value());
        const SetStateLog* log = std::get_if<SetStateLog>(&*request);
        ASSERT_NE(log, nullptr);
        ASSERT_EQ(log->events.size(), played.size());
        EXPECT_EQ(SeatsToLetters(log->events.front().seats), "PPCC");
        EXPECT_EQ(LevelNumber(log->events.front().levels[2]), 5);
        EXPECT_EQ(log->events.front().seed, played.front().seed);
        EXPECT_EQ(log->events.back(), played.back());

        state.erase("seats");
        state.erase("levels");
        const auto defaulted = Parse(state);
        ASSERT_TRUE(defaulted.has_value());
        EXPECT_EQ(std::get<SetStateLog>(*defaulted).events.front().seats, Defaults().seats);
        EXPECT_EQ(std::get<SetStateLog>(*defaulted).events.front().levels, Defaults().levels);
    }

    TEST_F(MahjongSetStateTest, UntilKeepsTheFirstEvents) {
        Vector<MatchLogEvent> played;
        const String text = LogText(&played);
        Json state = Json::object();
        state["log"] = MakeJsonString(StringView(text.c_str(), text.size()));

        state["until"] = 1;
        EXPECT_EQ(std::get<SetStateLog>(*Parse(state)).events.size(), 1u);
        state["until"] = played.size() - 1;
        EXPECT_EQ(std::get<SetStateLog>(*Parse(state)).events.size(), played.size() - 1);
        state["until"] = played.size();
        EXPECT_EQ(std::get<SetStateLog>(*Parse(state)).events.size(), played.size());

        for (const Json& bad : {Json(0), Json(-1), Json(played.size() + 1), Json(1.5), Json("2"), Json(nullptr)}) {
            state["until"] = bad;
            ExpectRefused(state, "until");
        }
    }

    TEST_F(MahjongSetStateTest, ABadLogIsRefusedWithTheLineNamed) {
        Json state = Json::object();
        state["log"] = 5;
        ExpectRefused(state, "'log'");
        state["log"] = "";
        ExpectRefused(state, "no events");
        state["log"] = "\n  \n";
        ExpectRefused(state, "no events");
        state["log"] = "not json";
        ExpectRefused(state, "line 1");

        const String text = Format("{}{{\"event\": \"nothing\"}}\n", LogText());
        state["log"] = MakeJsonString(StringView(text.c_str(), text.size()));
        ExpectRefused(state, "line ");
    }

    TEST_F(MahjongSetStateTest, ALogHasNoKeyOfTheDealForm) {
        const String text = LogText();
        const Json log = MakeJsonString(StringView(text.c_str(), text.size()));
        for (const char* key : {"seed", "settings", "scores", "handsPlayed"}) {
            Json state = Json::object();
            state["log"] = log;
            state[key] = Json::object();
            ExpectRefused(state, key);
        }
        Json both = Base();
        both["log"] = log;
        ExpectRefused(both, "together");
        Json untilWithADeal = Base();
        untilWithADeal["until"] = 3;
        ExpectRefused(untilWithADeal, "until");
        Json levels = Json::object();
        levels["log"] = log;
        levels["level"] = 3;
        levels["levels"] = Json::array({1, 2, 3, 4});
        ExpectRefused(levels, "together");
    }

    // ----- the bounds of the rules reach every form -----

    TEST_F(MahjongSetStateTest, TheDealFormRefusesTheValuesBeyondTheBoundsAndTakesThoseAtThem) {
        Json state = Base();
        state["settings"] = Json::parse(R"({"limit": 1000000, "startingScore": -100000000})");
        EXPECT_TRUE(Parse(state).has_value());
        state["settings"] = Json::parse(R"({"limit": 1000001})");
        ExpectRefused(state, "settings.limit");
        state["settings"] = Json::parse(R"({"limit": 2147483647})");
        ExpectRefused(state, "settings.limit");
        state["settings"] = Json::parse(R"({"startingScore": 100000001})");
        ExpectRefused(state, "settings.startingScore");
        state["settings"] = Json::parse(R"({"startingScore": -2147483648})");
        ExpectRefused(state, "settings.startingScore");
        state["settings"] = Json::object();
        state["scores"] = Json::array({100000000, -100000000, 0, 1});
        EXPECT_TRUE(Parse(state).has_value());
        state["scores"] = Json::array({2147483647, 0, 0, 0});
        ExpectRefused(state, "scores");
    }

    TEST_F(MahjongSetStateTest, ALogWithAValueBeyondTheBoundsIsRefusedAndTheGameKeepsItsMatch) {
        IntrusivePtr<MahjongGame> game = New<MahjongGame>();
        const auto control = game->AsControllable();
        ASSERT_TRUE(control->Reset(Json::parse(R"({"seed": 3, "seats": "PCCC"})")).has_value());
        const Json before = *control->GetState();
        const uint64_t version = control->GetStateVersion();
        const uint64_t starts = game->GetLocalSession().StartCount();
        const MahjongSettings settings = game->GetSettings();

        Vector<MatchLogEvent> played;
        LogText(&played);
        constexpr int kIntMax = std::numeric_limits<int>::max();
        const auto changed = [&played](auto change) {
            Vector<MatchLogEvent> events = played;
            change(events.front());
            Json request = Json::object();
            const String text = TextOf(events);
            request["log"] = MakeJsonString(StringView(text.c_str(), text.size()));
            return request;
        };
        const Vector<Json> requests = {
            changed([](MatchLogEvent& start) { start.settings.startingScore = kIntMax; }),
            changed([](MatchLogEvent& start) { start.settings.startingScore = std::numeric_limits<int>::min(); }),
            changed([](MatchLogEvent& start) { start.settings.limit = kIntMax; }),
            changed([](MatchLogEvent& start) { start.settings.limit = kMaxLimit + 1; }),
            changed([](MatchLogEvent& start) { start.settings.limit = -1; }),
            changed([](MatchLogEvent& start) {
                start.state = MakeSamplePosition();
                start.state->scores[2] = kIntMax;
            }),
        };
        for (const Json& request : requests) {
            const auto set = control->SetState(request);
            ASSERT_FALSE(set.has_value()) << DumpJson(request).c_str();
            EXPECT_EQ(String(set.error().code.c_str(), set.error().code.size()), kInvalidParams);
            EXPECT_EQ(*control->GetState(), before);
            EXPECT_EQ(control->GetStateVersion(), version);
            EXPECT_EQ(game->GetLocalSession().StartCount(), starts);
            EXPECT_EQ(game->GetSettings().limit, settings.limit);
            EXPECT_EQ(game->GetSettings().startingScore, settings.startingScore);
        }
    }

}  // namespace BECore::Mahjong::Tests
