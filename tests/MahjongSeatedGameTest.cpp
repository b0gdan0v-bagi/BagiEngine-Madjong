#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <BECore/Control/Json.h>
#include <Mahjong/MahjongStateJson.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <gtest/gtest.h>

/**
 * `MahjongSeatedGame` (`Online/MahjongSeatedGame.h`): the four-seat game a room server runs. Plain C++: no scene, no `Application`.
 * The cases drive it only through `ISeatedGame`, except for the fixed walls, which a test lays into the hand in progress
 * (`GetMatch().Hand()`) to know what a seat holds.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint64_t kSeed = 7;
        constexpr int kStepLimit = 20000;
        constexpr int kSeatsOutOfRange[] = {-1, 4, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
        constexpr int kProbedSeeds = 3;
        constexpr int kProbeEvery = 25;
        constexpr size_t kFullCompareEvery = 16;
        constexpr int kLowLevel = 1;
        constexpr const char* kPriorityTile = "4p";

        Json Parse(const char* text) {
            return ParseJson(text).value();
        }

        Json Options(const char* text) {
            return Parse(text);
        }

        std::string CodeOf(const std::expected<Json, ControlError>& result) {
            return result ? std::string() : std::string(result.error().code.c_str(), result.error().code.size());
        }

        std::string Dump(const Json& json) {
            const eastl::string text = DumpJson(json);
            return std::string(text.c_str(), text.size());
        }

        MahjongSeatedGame Started(uint64_t seed = kSeed, const char* options = "{\"hands\": 8, \"level\": 1}") {
            MahjongSeatedGame game;
            const auto started = game.Start(seed, Options(options));
            EXPECT_TRUE(started.has_value());
            return game;
        }

        Json ViewOf(const MahjongSeatedGame& game, int seat) {
            return game.GetSeatView(seat).value();
        }

        Json ActionsOf(const MahjongSeatedGame& game, int seat) {
            return game.GetSeatActions(seat).value();
        }

        /// Every seat's view and actions as one text: the state a refused call must leave as it was.
        std::string Everything(const MahjongSeatedGame& game) {
            std::string text = std::to_string(game.GetStateVersion());
            for (int seat = 0; seat < kSeatCount; ++seat) {
                text += Dump(ViewOf(game, seat)) + Dump(ActionsOf(game, seat));
            }
            return text;
        }

        bool PhaseIs(const MahjongSeatedGame& game, SessionPhase phase) {
            return PhaseOfMatch(game.GetMatch()) == phase;
        }

        /// The first seat that has a computer's action now, -1 when none.
        int FirstSeatWithAction(const MahjongSeatedGame& game) {
            for (int seat = 0; seat < kSeatCount; ++seat) {
                if (game.ChooseComputerAction(seat).has_value()) {
                    return seat;
                }
            }
            return -1;
        }

        /// Plays the action of the first seat that has one, as a room does for computers. False when no seat has one or it was refused.
        bool StepOnce(MahjongSeatedGame& game) {
            const int seat = FirstSeatWithAction(game);
            if (seat < 0) {
                return false;
            }
            const uint64_t before = game.GetStateVersion();
            const auto done = game.PerformSeatAction(seat, *game.ChooseComputerAction(seat));
            EXPECT_TRUE(done.has_value()) << "seat " << seat << ": " << (done ? "" : done.error().message.c_str());
            EXPECT_GT(game.GetStateVersion(), before);
            return done.has_value();
        }

        /// Plays until the hand in progress is over (or the match); false when it does not get there.
        bool PlayToHandOver(MahjongSeatedGame& game) {
            for (int step = 0; step < kStepLimit; ++step) {
                if (!PhaseIs(game, SessionPhase::Playing)) {
                    return PhaseIs(game, SessionPhase::HandOver);
                }
                if (!StepOnce(game)) {
                    return false;
                }
            }
            return false;
        }

        /// Seat 3 discarded 4p: seat 0 can chow it, seat 1 pung it, seat 2 has declared a calling hand that waits on it.
        HandModel PriorityWindow() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "3p 5p 1s 4s 7s 1m 4m 7m 2s 8s Sw Ww Gd";
            spec.hands[kSeat1] = "4p 4p 7p 1s 4s 7s 2m 5m 8m Sw Ww Rd 9p";
            spec.hands[kSeat2] = "3p 5p 4s 5s 6s 7m 8m 9m Ew Ew Ew 1m 1m";
            spec.hands[kSeat3] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Rd Wd Nw 9s";
            spec.live = std::string("Wd Gd Nw ") + kPriorityTile;
            HandModel model = MakeModel(spec);
            Lap(model, "Wd Gd Nw", {kSeat2});
            EXPECT_EQ(model.CurrentSeat(), kSeat3);
            EXPECT_TRUE(model.Discard(kSeat3, T(kPriorityTile)).has_value());
            return model;
        }

        Json ActionObject(const char* text) {
            return Parse(text);
        }

        bool Lists(const Json& actions, const Json& wanted) {
            for (const Json& action : actions) {
                if (action == wanted) {
                    return true;
                }
            }
            return false;
        }

        /// East holds three east winds, draws the fourth and may declare a concealed kong of them; the others hold quiet hands.
        HandModel ConcealedKongModel() {
            return MakeModel(SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew"));
        }

        Json ConcealedKongAction() {
            return Parse("{\"type\":\"concealed_kong\",\"tile\":\"Ew\"}");
        }

    }  // namespace

    // ----- start and options -----

    TEST(MahjongSeatedGameStartTest, TheViewBeforeStartIsAnEmptyObjectAndNobodyActs) {
        const MahjongSeatedGame game;
        EXPECT_EQ(game.GetSeatCount(), kSeatCount);
        EXPECT_FALSE(game.IsFinished());
        EXPECT_EQ(game.GetStateVersion(), 0u);
        for (int seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(ViewOf(game, seat), Json::object());
            EXPECT_EQ(ActionsOf(game, seat), Json::array());
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value());
        }
    }

    TEST(MahjongSeatedGameStartTest, TheRegistryInfoNamesTheGameAndItsFourSeats) {
        const SeatedGameInfo info = MahjongSeatedGame::Info();
        EXPECT_EQ(info.name, "mahjong");
        EXPECT_EQ(info.seatCount, kSeatCount);
    }

    TEST(MahjongSeatedGameStartTest, DefaultsAreTheFullMatchAndTheDefaultLevel) {
        MahjongSeatedGame game;
        ASSERT_TRUE(game.Start(kSeed, Json::object()).has_value());
        EXPECT_EQ(ViewOf(game, 0)["handsTotal"], kFullMatchHands);
        EXPECT_EQ(ViewOf(game, 0)["phase"], "playing");
        EXPECT_EQ(game.GetStateVersion(), 1u);
        // a request that is no object at all asks for nothing
        MahjongSeatedGame other;
        ASSERT_TRUE(other.Start(kSeed, Json(nullptr)).has_value());
        EXPECT_EQ(ViewOf(other, 0)["handsTotal"], kFullMatchHands);
    }

    TEST(MahjongSeatedGameStartTest, TheShortMatchIsAskedWithHands) {
        MahjongSeatedGame game;
        ASSERT_TRUE(game.Start(kSeed, Options("{\"hands\": 8}")).has_value());
        EXPECT_EQ(ViewOf(game, 2)["handsTotal"], kShortMatchHands);
    }

    TEST(MahjongSeatedGameStartTest, AMalformedOptionIsInvalidParamsNamingTheKey) {
        for (const char* text : {"{\"hands\": 12}", "{\"hands\": \"8\"}", "{\"hands\": 8.5}", "{\"hands\": null}", "{\"hands\": 9223372036854775808}", "{\"level\": 0}", "{\"level\": 11}",
                                 "{\"level\": \"3\"}", "{\"level\": true}", "{\"hands\": 8, \"level\": 99}", "{\"hintsAllowed\": 1}", "{\"hintsAllowed\": \"true\"}", "{\"hintsAllowed\": null}"}) {
            MahjongSeatedGame game;
            const auto started = game.Start(kSeed, Options(text));
            ASSERT_FALSE(started.has_value()) << text;
            EXPECT_EQ(started.error().code, "invalid_params") << text;
            const bool namesAKey = started.error().message.find("'hands'") != eastl::string::npos || started.error().message.find("'level'") != eastl::string::npos ||
                                   started.error().message.find("'hintsAllowed'") != eastl::string::npos;
            EXPECT_TRUE(namesAKey) << text << ": " << started.error().message.c_str();
            EXPECT_EQ(game.GetStateVersion(), 0u) << text;
        }
    }

    TEST(MahjongSeatedGameStartTest, AnUnknownMemberIsIgnored) {
        MahjongSeatedGame plain;
        ASSERT_TRUE(plain.Start(kSeed, Options("{\"hands\": 8}")).has_value());
        MahjongSeatedGame extra;
        ASSERT_TRUE(extra.Start(kSeed, Options("{\"hands\": 8, \"flowers\": true, \"mood\": [1, 2]}")).has_value());
        EXPECT_EQ(Everything(plain), Everything(extra)) << "flowers are not offered, so the member does nothing";
    }

    TEST(MahjongSeatedGameStartTest, AFailedStartLeavesThePreviousMatchAlone) {
        MahjongSeatedGame game = Started();
        ASSERT_TRUE(StepOnce(game));
        const std::string before = Everything(game);
        EXPECT_FALSE(game.Start(kSeed + 1, Options("{\"hands\": 5}")).has_value());
        EXPECT_FALSE(game.Start(kSeed + 1, Options("{\"level\": 12}")).has_value());
        EXPECT_EQ(Everything(game), before);
    }

    TEST(MahjongSeatedGameStartTest, ASecondStartBeginsANewMatchAndTheVersionKeepsGrowing) {
        MahjongSeatedGame game = Started();
        ASSERT_TRUE(StepOnce(game));
        const uint64_t before = game.GetStateVersion();
        ASSERT_TRUE(game.Start(kSeed + 5, Options("{\"hands\": 16}")).has_value());
        EXPECT_GT(game.GetStateVersion(), before);
        EXPECT_EQ(ViewOf(game, 0)["handsTotal"], kFullMatchHands);
        EXPECT_EQ(ViewOf(game, 0)["handsPlayed"], 0);
        EXPECT_EQ(ViewOf(game, 0)["discards"], Json::array());
    }

    TEST(MahjongSeatedGameStartTest, TheRoomSeedIsFoldedToTheMatchSeedByXorOfItsHalves) {
        constexpr uint64_t kWide = 0xAAAAAAAA55555555ULL;  // low ^ high is 0xFFFFFFFF
        constexpr uint64_t kNarrow = 0xFFFFFFFFULL;
        MahjongSeatedGame wide;
        ASSERT_TRUE(wide.Start(kWide, Json::object()).has_value());
        MahjongSeatedGame narrow;
        ASSERT_TRUE(narrow.Start(kNarrow, Json::object()).has_value());
        EXPECT_EQ(Dump(ViewOf(wide, 0)), Dump(ViewOf(narrow, 0)));
    }

    // ----- seats out of range -----

    TEST(MahjongSeatedGameSeatTest, ASeatOutOfRangeIsInvalidSeatOnEveryMethod) {
        MahjongSeatedGame game = Started();
        const std::string before = Everything(game);
        for (const int seat : kSeatsOutOfRange) {
            EXPECT_EQ(CodeOf(game.GetSeatView(seat)), "invalid_seat") << seat;
            EXPECT_EQ(CodeOf(game.GetSeatActions(seat)), "invalid_seat") << seat;
            EXPECT_EQ(CodeOf(game.PerformSeatAction(seat, ActionObject("{\"type\":\"pass\"}"))), "invalid_seat") << seat;
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value()) << seat;
        }
        EXPECT_EQ(Everything(game), before);
        const MahjongSeatedGame fresh;
        for (const int seat : kSeatsOutOfRange) {
            EXPECT_EQ(CodeOf(fresh.GetSeatView(seat)), "invalid_seat") << "before Start, seat " << seat;
        }
    }

    // ----- who may act -----

    TEST(MahjongSeatedGameActionsTest, OnlyTheSeatToDiscardHasActionsAtTheStart) {
        const MahjongSeatedGame game = Started();
        const Seat east = game.GetMatch().Hand().CurrentSeat();
        for (int seat = 0; seat < kSeatCount; ++seat) {
            const Json actions = ActionsOf(game, seat);
            if (seat == east) {
                EXPECT_FALSE(actions.empty());
                EXPECT_TRUE(game.ChooseComputerAction(seat).has_value());
                EXPECT_TRUE(ViewOf(game, seat)["turn"]["yourTurn"].get<bool>());
            } else {
                EXPECT_TRUE(actions.empty()) << "seat " << seat;
                EXPECT_FALSE(game.ChooseComputerAction(seat).has_value()) << "seat " << seat;
                EXPECT_FALSE(ViewOf(game, seat)["turn"]["yourTurn"].get<bool>());
            }
        }
    }

    TEST(MahjongSeatedGameActionsTest, AClaimWindowGivesActionsToTheSeatsThatCanClaimAndToNoOthers) {
        MahjongSeatedGame game = Started();
        game.GetMatch().Hand() = PriorityWindow();
        ASSERT_EQ(game.GetMatch().Hand().GetPhase(), Phase::Claim);

        EXPECT_TRUE(Lists(ActionsOf(game, 0), ActionObject("{\"type\":\"claim\",\"claim\":\"chow\",\"first\":\"3p\"}")));
        EXPECT_TRUE(Lists(ActionsOf(game, 1), ActionObject("{\"type\":\"claim\",\"claim\":\"pung\"}")));
        EXPECT_TRUE(Lists(ActionsOf(game, 2), ActionObject("{\"type\":\"claim\",\"claim\":\"mahjong\"}")));
        for (int seat = 0; seat < 3; ++seat) {
            EXPECT_TRUE(Lists(ActionsOf(game, seat), ActionObject("{\"type\":\"pass\"}"))) << "seat " << seat;
        }
        EXPECT_TRUE(ActionsOf(game, 3).empty()) << "the discarder cannot claim its own tile";
        EXPECT_FALSE(game.ChooseComputerAction(3).has_value());
    }

    TEST(MahjongSeatedGameActionsTest, ASeatThatPassedHasNoActionsAndTheOthersStillDecide) {
        MahjongSeatedGame game = Started();
        game.GetMatch().Hand() = PriorityWindow();
        const uint64_t before = game.GetStateVersion();
        const auto passed = game.PerformSeatAction(1, ActionObject("{\"type\":\"pass\"}"));
        ASSERT_TRUE(passed.has_value());
        EXPECT_FALSE(passed->contains("applied")) << "a pass would tell every seat that this one could claim";
        EXPECT_EQ((*passed)["phase"], "playing");
        EXPECT_GT(game.GetStateVersion(), before);
        EXPECT_TRUE(ActionsOf(game, 1).empty());
        EXPECT_FALSE(ActionsOf(game, 0).empty());
        EXPECT_FALSE(ActionsOf(game, 2).empty());
        EXPECT_EQ(CodeOf(game.PerformSeatAction(1, ActionObject("{\"type\":\"pass\"}"))), "not_your_turn");
    }

    // ----- the order of the checks and the refusals -----

    TEST(MahjongSeatedGameActionsTest, TheErrorsComeInTheDocumentedOrderAndChangeNothing) {
        MahjongSeatedGame game = Started();
        const Seat actor = game.GetMatch().Hand().CurrentSeat();
        const Seat other = (actor + 1) % kSeatCount;
        const std::string before = Everything(game);

        // invalid_seat before everything, even before a malformed action
        EXPECT_EQ(CodeOf(game.PerformSeatAction(9, Json(nullptr))), "invalid_seat");
        // invalid_params before not_your_turn
        EXPECT_EQ(CodeOf(game.PerformSeatAction(other, Json::object())), "invalid_params");
        EXPECT_EQ(CodeOf(game.PerformSeatAction(other, Json(7))), "invalid_params");
        EXPECT_EQ(CodeOf(game.PerformSeatAction(actor, ActionObject("{\"type\":\"discard\",\"tile\":\"zz\"}"))), "invalid_params");
        EXPECT_EQ(CodeOf(game.PerformSeatAction(actor, ActionObject("{\"type\":\"dance\"}"))), "invalid_params");
        // not_your_turn before illegal_action
        EXPECT_EQ(CodeOf(game.PerformSeatAction(other, ActionObject("{\"type\":\"win\"}"))), "not_your_turn");
        EXPECT_EQ(CodeOf(game.PerformSeatAction(other, ActionObject("{\"type\":\"next_hand\"}"))), "not_your_turn");
        // illegal_action: well-formed, the seat acts, the action is not among its own
        EXPECT_EQ(CodeOf(game.PerformSeatAction(actor, ActionObject("{\"type\":\"next_hand\"}"))), "illegal_action");
        EXPECT_EQ(CodeOf(game.PerformSeatAction(actor, ActionObject("{\"type\":\"pass\"}"))), "illegal_action");
        EXPECT_EQ(CodeOf(game.PerformSeatAction(actor, ActionObject("{\"type\":\"claim\",\"claim\":\"pung\"}"))), "illegal_action");
        const PlayerHand& own = game.GetMatch().Hand().GetHand(actor);
        // a tile the seat does not hold: some kind that is not in its hand
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile{id};
            if (tile.IsBonus() || eastl::find(own.concealed.begin(), own.concealed.end(), tile) != own.concealed.end()) {
                continue;
            }
            Json discard = Json::object();
            discard["type"] = "discard";
            discard["tile"] = TileToJson(tile);
            EXPECT_EQ(CodeOf(game.PerformSeatAction(actor, discard)), "illegal_action") << discard["tile"];
            break;
        }
        EXPECT_EQ(Everything(game), before) << "a refusal changes neither a view nor the version";
    }

    TEST(MahjongSeatedGameActionsTest, TheAnswerToAnActionIsPublic) {
        MahjongSeatedGame game = Started();
        const int seat = FirstSeatWithAction(game);
        const Json action = *game.ChooseComputerAction(seat);
        const auto answer = game.PerformSeatAction(seat, action);
        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ((*answer)["applied"], action);
        EXPECT_EQ((*answer)["phase"], "playing");
        EXPECT_EQ((*answer)["version"], game.GetStateVersion());
        EXPECT_EQ(answer->size(), 3u) << Dump(*answer);
    }

    TEST(MahjongSeatedGameActionsTest, TheAnswerToAConcealedKongNamesNoTile) {
        MahjongSeatedGame game = Started();
        game.GetMatch().Hand() = ConcealedKongModel();
        const auto answer = game.PerformSeatAction(kSeat0, ConcealedKongAction());
        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ((*answer)["applied"], ActionObject("{\"type\":\"concealed_kong\"}"));
        EXPECT_EQ(Dump(*answer).find("Ew"), std::string::npos) << Dump(*answer);
    }

    namespace {

        /// Every action a seat could try (the canonical ones of the hand tests, as JSON) and the ready check.
        eastl::vector<Json> EveryAttempt() {
            eastl::vector<Json> attempts;
            for (const HandAction& action : EveryCanonicalAction()) {
                attempts.push_back(ActionToJson(action));
            }
            attempts.push_back(NextHandActionToJson());
            return attempts;
        }

        /// Accepted iff listed, for every seat in the position of `game`.
        void ExpectAcceptedExactlyWhenListed(const MahjongSeatedGame& game) {
            static const eastl::vector<Json> attempts = EveryAttempt();
            for (int seat = 0; seat < kSeatCount; ++seat) {
                const Json listed = ActionsOf(game, seat);
                for (const Json& action : listed) {
                    MahjongSeatedGame copy = game;
                    EXPECT_TRUE(copy.PerformSeatAction(seat, action).has_value()) << "seat " << seat << " " << Dump(action);
                }
                for (size_t index = 0; index < attempts.size(); ++index) {
                    const Json& attempt = attempts[index];
                    if (Lists(listed, attempt)) {
                        continue;
                    }
                    MahjongSeatedGame copy = game;
                    const auto done = copy.PerformSeatAction(seat, attempt);
                    ASSERT_FALSE(done.has_value()) << "seat " << seat << " " << Dump(attempt) << " is not listed but was accepted";
                    // a chow without its first tile is malformed, not just illegal; the rest is well formed
                    const std::string expected = !ActionFromJson(attempt) ? "invalid_params" : (listed.empty() ? "not_your_turn" : "illegal_action");
                    EXPECT_EQ(CodeOf(done), expected) << "seat " << seat << " " << Dump(attempt);
                    EXPECT_EQ(copy.GetStateVersion(), game.GetStateVersion());
                    if (index % kFullCompareEvery == 0) {
                        EXPECT_EQ(Everything(copy), Everything(game));
                    }
                }
            }
        }

    }  // namespace

    TEST(MahjongSeatedGameActionsTest, ActionsAreAcceptedExactlyWhenListedThroughHandsAndTheClaimWindow) {
        for (int round = 0; round < kProbedSeeds; ++round) {
            MahjongSeatedGame game = Started(kSeed + static_cast<uint64_t>(round));
            int step = 0;
            while (PhaseIs(game, SessionPhase::Playing) && step < kStepLimit) {
                if (step % kProbeEvery == 0) {
                    ASSERT_NO_FATAL_FAILURE(ExpectAcceptedExactlyWhenListed(game));
                }
                ASSERT_TRUE(StepOnce(game));
                ++step;
            }
            ASSERT_TRUE(PhaseIs(game, SessionPhase::HandOver));
            ASSERT_NO_FATAL_FAILURE(ExpectAcceptedExactlyWhenListed(game));
        }
        MahjongSeatedGame window = Started();
        window.GetMatch().Hand() = PriorityWindow();
        ASSERT_NO_FATAL_FAILURE(ExpectAcceptedExactlyWhenListed(window));
    }

    // ----- the ready check -----

    TEST(MahjongSeatedGameReadyTest, AFinishedHandWaitsForAllFourSeatsAndThenDealsTheNextOne) {
        MahjongSeatedGame game = Started();
        ASSERT_TRUE(PlayToHandOver(game));
        const size_t settled = game.GetMatch().History().size();
        const Json nextHand = ActionObject("{\"type\":\"next_hand\"}");

        for (int seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(ActionsOf(game, seat), Json::array({nextHand})) << "seat " << seat;
            const Json view = ViewOf(game, seat);
            EXPECT_EQ(view["phase"], "hand_over");
            EXPECT_TRUE(view.contains("result"));
            EXPECT_EQ(view["ready"], Json::array({false, false, false, false}));
        }

        uint64_t version = game.GetStateVersion();
        Json ready = Json::array({false, false, false, false});
        for (int seat = 0; seat < kSeatCount - 1; ++seat) {
            const auto done = game.PerformSeatAction(seat, nextHand);
            ASSERT_TRUE(done.has_value()) << seat;
            EXPECT_FALSE(done->contains("settlement")) << "one seat's next_hand does not settle";
            EXPECT_EQ((*done)["phase"], "hand_over");
            EXPECT_GT(game.GetStateVersion(), version) << "every flag set bumps the version";
            version = game.GetStateVersion();
            ready[static_cast<size_t>(seat)] = true;
            EXPECT_EQ(ViewOf(game, (seat + 1) % kSeatCount)["ready"], ready);
            EXPECT_TRUE(ActionsOf(game, seat).empty());
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value());
            EXPECT_EQ(CodeOf(game.PerformSeatAction(seat, nextHand)), "not_your_turn") << "a second next_hand from the same seat";
            EXPECT_EQ(game.GetStateVersion(), version);
            EXPECT_EQ(game.GetMatch().History().size(), settled);
        }

        const auto last = game.PerformSeatAction(kSeatCount - 1, nextHand);
        ASSERT_TRUE(last.has_value());
        ASSERT_TRUE(last->contains("settlement"));
        const Json& settlement = (*last)["settlement"];
        for (const char* key : {"kind", "winner", "scores", "deltas"}) {
            EXPECT_TRUE(settlement.contains(key)) << key;
        }
        EXPECT_EQ(settlement.size(), 4u);
        EXPECT_GT(game.GetStateVersion(), version);
        EXPECT_EQ(game.GetMatch().History().size(), settled + 1);
        EXPECT_EQ((*last)["phase"], "playing");
        for (int seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(ViewOf(game, seat)["ready"], Json::array({false, false, false, false}));
            EXPECT_FALSE(ViewOf(game, seat).contains("result")) << "the next hand is dealt";
        }
        EXPECT_NE(FirstSeatWithAction(game), -1);
    }

    TEST(MahjongSeatedGameReadyTest, ANextHandWhilePlayingIsRefusedAndTheMatchEndsWithNoActions) {
        MahjongSeatedGame game = Started();
        const Seat actor = game.GetMatch().Hand().CurrentSeat();
        EXPECT_EQ(CodeOf(game.PerformSeatAction(actor, ActionObject("{\"type\":\"next_hand\"}"))), "illegal_action");

        for (int step = 0; step < kStepLimit && !game.IsFinished(); ++step) {
            ASSERT_TRUE(StepOnce(game));
        }
        ASSERT_TRUE(game.IsFinished());
        for (int seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_TRUE(ActionsOf(game, seat).empty());
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value());
            EXPECT_EQ(CodeOf(game.PerformSeatAction(seat, ActionObject("{\"type\":\"next_hand\"}"))), "not_your_turn");
            EXPECT_EQ(ViewOf(game, seat)["phase"], "match_over");
            EXPECT_TRUE(ViewOf(game, seat).contains("result"));
        }
    }

    // ----- the computer seats -----

    TEST(MahjongSeatedGameComputerTest, ComputersPlayAWholeShortMatchWithEveryActionAccepted) {
        MahjongSeatedGame game = Started();
        int steps = 0;
        while (!game.IsFinished()) {
            ASSERT_LT(steps++, kStepLimit);
            const uint64_t before = game.GetStateVersion();
            bool someoneActs = false;
            for (int seat = 0; seat < kSeatCount; ++seat) {
                const std::optional<Json> action = game.ChooseComputerAction(seat);
                EXPECT_EQ(action.has_value(), !ActionsOf(game, seat).empty()) << "seat " << seat;
                if (action) {
                    EXPECT_TRUE(Lists(ActionsOf(game, seat), *action)) << "seat " << seat << " " << Dump(*action);
                    someoneActs = true;
                }
            }
            ASSERT_TRUE(someoneActs) << "no seat can act while the match is on";
            ASSERT_TRUE(StepOnce(game));
            ASSERT_GT(game.GetStateVersion(), before);
        }
        EXPECT_EQ(game.GetMatch().GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(game.GetMatch().HandsPlayed(), kShortMatchHands);
        EXPECT_FALSE(game.ChooseComputerAction(0).has_value());
    }

    TEST(MahjongSeatedGameComputerTest, AComputerAnswerIsAFunctionOfTheState) {
        MahjongSeatedGame game = Started();
        for (int step = 0; step < 40; ++step) {
            const int seat = FirstSeatWithAction(game);
            ASSERT_GE(seat, 0);
            const Json first = *game.ChooseComputerAction(seat);
            EXPECT_EQ(*game.ChooseComputerAction(seat), first) << "asking again changes nothing";
            ASSERT_TRUE(game.PerformSeatAction(seat, first).has_value());
        }
    }

    TEST(MahjongSeatedGameComputerTest, TheMixedSeedDependsOnTheVersionAndTheMatchSeed) {
        EXPECT_EQ(MixSeed(5, 3), MixSeed(5, 3));
        EXPECT_NE(MixSeed(5, 3), MixSeed(5, 4));
        EXPECT_NE(MixSeed(5, 3), MixSeed(6, 3));
    }

    namespace {

        /// Every view of every seat after every step of a match, as one text.
        std::string Transcript(uint64_t seed) {
            MahjongSeatedGame game = Started(seed);
            std::string text;
            for (int step = 0; step < kStepLimit && !game.IsFinished(); ++step) {
                text += Everything(game);
                if (!StepOnce(game)) {
                    break;
                }
            }
            return text + Everything(game);
        }

    }  // namespace

    TEST(MahjongSeatedGameComputerTest, OneSeedAndOptionsGiveOneSequenceOfViewsAndAnotherSeedDoesNot) {
        const std::string first = Transcript(kSeed);
        EXPECT_EQ(first, Transcript(kSeed));
        EXPECT_NE(first, Transcript(kSeed + 1));
    }

    // ----- what a view holds -----

    namespace {

        std::set<std::string> KeysOf(const Json& object) {
            std::set<std::string> keys;
            for (const auto& item : object.items()) {
                keys.insert(item.key());
            }
            return keys;
        }

    }  // namespace

    TEST(MahjongSeatedGameViewTest, AViewHoldsOnlyTheOwnTilesWhileTheHandIsPlayedAndNoKindOrLevel) {
        const std::set<std::string> topKeys = {"phase",    "seat", "seatWind",    "roundWind",  "east",    "hand",  "turn",   "seats",
                                               "discards", "wall", "handsPlayed", "handsTotal", "version", "ready", "format", "hintsAllowed"};
        const std::set<std::string> seatKeys = {"wind", "score", "concealedCount", "melds", "concealedKongs", "bonus", "discardCount", "calling"};
        MahjongSeatedGame game = Started();
        for (int step = 0; step < kStepLimit && PhaseIs(game, SessionPhase::Playing); ++step) {
            for (int seat = 0; seat < kSeatCount; ++seat) {
                const Json view = ViewOf(game, seat);
                EXPECT_EQ(KeysOf(view), topKeys) << "seat " << seat << " step " << step;
                EXPECT_EQ(view["seat"], seat);
                EXPECT_EQ(view["hand"].size(), view["seats"][static_cast<size_t>(seat)]["concealedCount"].get<size_t>());
                ASSERT_EQ(view["seats"].size(), static_cast<size_t>(kSeatCount));
                for (const Json& entry : view["seats"]) {
                    EXPECT_EQ(KeysOf(entry), seatKeys);
                }
                for (const Json& meld : view["seats"][static_cast<size_t>(seat)]["melds"]) {
                    EXPECT_TRUE(meld.contains("tiles"));
                }
            }
            ASSERT_TRUE(StepOnce(game));
        }
    }

    TEST(MahjongSeatedGameViewTest, EveryViewOfAStartedGameCarriesTheFormatNumberAndTheEmptyOneBeforeStartDoesNot) {
        const MahjongSeatedGame fresh;
        EXPECT_FALSE(ViewOf(fresh, 0).contains("format"));
        MahjongSeatedGame game = Started();
        for (int step = 0; step < kStepLimit && PhaseIs(game, SessionPhase::Playing); ++step) {
            ASSERT_TRUE(StepOnce(game));
        }
        for (int seat = 0; seat < kSeatCount; ++seat) {
            const Json view = ViewOf(game, seat);
            ASSERT_TRUE(view.contains("format")) << seat;
            EXPECT_EQ(view["format"], kOnlineViewFormat) << seat;
        }
        EXPECT_EQ(ViewOf(Started(), 2)["format"], kOnlineViewFormat);
    }

    TEST(MahjongSeatedGameViewTest, TheOtherSeatsConcealedKongsAreOnlyCountedInAView) {
        MahjongSeatedGame game = Started();
        game.GetMatch().Hand() = ConcealedKongModel();
        ASSERT_TRUE(Lists(ActionsOf(game, kSeat0), ConcealedKongAction()));
        ASSERT_TRUE(game.PerformSeatAction(kSeat0, ConcealedKongAction()).has_value());
        ASSERT_EQ(game.GetMatch().Hand().GetHand(kSeat0).melds.size(), 1u);

        // a concealed kong lies face down for the others: only `concealedKongs` of a seat says one exists
        for (int seat = 0; seat < kSeatCount; ++seat) {
            const Json view = ViewOf(game, seat);
            EXPECT_EQ(view["seats"][0]["concealedKongs"], 1) << "seat " << seat;
            if (seat == kSeat0) {
                EXPECT_EQ(view["seats"][0]["melds"].size(), 1u) << "the owner sees its own kong";
                continue;
            }
            EXPECT_TRUE(view["seats"][0]["melds"].empty()) << "seat " << seat << " sees the kong of seat 0";
            EXPECT_EQ(Dump(view).find("\"Ew\""), std::string::npos) << "no tile of the kong in the view of seat " << seat;
        }
    }

    TEST(MahjongSeatedGameViewTest, AFinishedHandShowsAllFourHandsInResultAndTheReadyFlags) {
        MahjongSeatedGame game = Started();
        for (int step = 0; step < kStepLimit && PhaseIs(game, SessionPhase::Playing); ++step) {
            for (int seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_FALSE(ViewOf(game, seat).contains("result")) << "no result while the hand is played";
            }
            ASSERT_TRUE(StepOnce(game));
        }
        ASSERT_TRUE(PhaseIs(game, SessionPhase::HandOver));
        for (int seat = 0; seat < kSeatCount; ++seat) {
            const Json view = ViewOf(game, seat);
            ASSERT_TRUE(view.contains("result"));
            ASSERT_TRUE(view.contains("ready"));
            ASSERT_EQ(view["result"]["seats"].size(), static_cast<size_t>(kSeatCount));
            for (int owner = 0; owner < kSeatCount; ++owner) {
                const PlayerHand& truth = game.GetMatch().Hand().GetHand(owner);
                EXPECT_EQ(view["result"]["seats"][static_cast<size_t>(owner)]["concealed"], TilesToJson(truth.concealed));
            }
        }
    }

    TEST(MahjongSeatedGameViewTest, TheSharedBuilderWritesNoKindNorLevelWithoutThemAndTheControlKeepsThem) {
        MahjongSeatedGame game = Started();
        const Json view = ViewOf(game, 0);
        EXPECT_FALSE(view.contains("level"));
        EXPECT_FALSE(view.contains("levels"));
        for (const Json& entry : view["seats"]) {
            EXPECT_FALSE(entry.contains("kind"));
        }

        const Match& match = game.GetMatch();
        const SeatSetup kinds = kDefaultSeatSetup;
        const SeatLevels levels = UniformLevels(ComputerLevel{kLowLevel});
        StateJsonContext context{match};
        context.phase = PhaseOfMatch(match);
        context.version = 3;
        context.viewSeat = 0;
        context.kinds = &kinds;
        context.levels = &levels;
        const Json full = TableStateToJson(context, MakeTableView(match.Hand(), 0));
        EXPECT_EQ(full["levels"], Json::array({1, 1, 1, 1}));
        EXPECT_EQ(full["level"], 1);
        EXPECT_EQ(full["seats"][0]["kind"], "person");
        EXPECT_EQ(full["seats"][1]["kind"], "computer");
        EXPECT_EQ(full["version"], 3);
    }

    TEST(MahjongSeatedGameViewTest, ThePhaseOfAMatchFollowsTheRuleOfTheSession) {
        Match match;
        EXPECT_EQ(PhaseOfMatch(match), SessionPhase::MatchOver) << "a match that is not in a hand is over, as TableSession reports it";
        ASSERT_TRUE(match.Start(MahjongSettings{}, 1).has_value());
        EXPECT_EQ(PhaseOfMatch(match), SessionPhase::Playing);
    }

}  // namespace BECore::Mahjong::Tests
