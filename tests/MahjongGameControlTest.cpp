#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"
#include "StringsTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/MahjongGameControl.h>
#include <gtest/gtest.h>

/**
 * `MahjongGameControl` (`MahjongGameControl.h`): the state a client sees, the actions it can take, `perform_action` with the computers
 * answering, `reset` and its options, and the errors. The game is built directly (no scene, no `GameManager`) and driven only through
 * the adapter, apart from the fixed walls, which a test lays into the hand in progress (`Match::Hand()`) to know what a seat holds.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr uint32_t kSeed = 7;
        constexpr int kSeedsToSearch = 200;
        constexpr int kActionLimitPerMatch = 20000;
        constexpr int kHandLimitPerMatch = 200;
        constexpr int kStepsToAClaimWindow = 300;
        constexpr int kMovesChecked = 12;
        constexpr int64_t kMaxSeed = 4294967295;
        constexpr const char* kFlowerTile = "F1";

        Json Parse(const char* text) {
            return ParseJson(text).value();
        }

        std::string ErrorCodeOf(const ControlError& error) {
            return std::string(error.code.c_str(), error.code.size());
        }

        template <typename T>
        std::string CodeOf(const std::expected<T, ControlError>& result) {
            return result ? "" : ErrorCodeOf(result.error());
        }

        /// A member of a JSON object that the test expects to be there (a failure, and null, when it is not).
        const Json& Member(const Json& object, const char* key) {
            static const Json kNull;
            const Json* found = FindJsonMember(object, key);
            EXPECT_NE(found, nullptr) << "missing member '" << key << "'";
            return found ? *found : kNull;
        }

        std::string Str(const Json& json) {
            return json.is_string() ? json.get<std::string>() : std::string("<not a string>");
        }

        std::vector<std::string> Strings(const Json& list) {
            std::vector<std::string> out;
            for (const Json& item : list) {
                out.push_back(Str(item));
            }
            return out;
        }

        /// The seats whose tiles the client must not see, kinds that seat holds and the viewer does not.
        std::set<uint8_t> KindsOnlyOthersHold(const HandModel& hand, Seat viewer) {
            std::set<uint8_t> mine;
            for (const Tile tile : hand.GetHand(viewer).concealed) {
                mine.insert(tile.Id());
            }
            for (const Meld& meld : hand.GetHand(viewer).melds) {  // the viewer sees its own concealed kongs
                for (int offset = 0; offset < (meld.kind == MeldKind::Chow ? 3 : 1); ++offset) {
                    mine.insert(static_cast<uint8_t>(meld.first.Id() + offset));
                }
            }
            std::set<uint8_t> hidden;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat == viewer) {
                    continue;
                }
                for (const Tile tile : hand.GetHand(seat).concealed) {
                    if (mine.count(tile.Id()) == 0) {
                        hidden.insert(tile.Id());
                    }
                }
            }
            return hidden;
        }

        /// The actions played to get to a state, to build it again.
        struct Trace {
            std::string options;
            std::vector<Json> actions;
        };

        /// Starts the match of `trace` again and plays its actions.
        void Rebuild(IControllableGame& control, const Trace& trace) {
            ASSERT_TRUE(control.Reset(Parse(trace.options.c_str())).has_value());
            for (const Json& action : trace.actions) {
                ASSERT_TRUE(control.PerformAction(action).has_value()) << DumpJson(action).c_str();
            }
        }

        bool IsListed(const Json& actions, const Json& action) {
            return std::find(actions.begin(), actions.end(), action) != actions.end();
        }

        /// Whether `perform_action` accepts exactly what `list_actions` lists, for every canonical action, at the state `trace` leads to.
        ::testing::AssertionResult ApiAcceptsExactlyWhatIsListed(MahjongGame& game, const Trace& trace) {
            IControllableGame& control = *game.AsControllable();
            Rebuild(control, trace);
            const Json listed = control.GetActions().value();
            for (const HandAction& candidate : EveryCanonicalAction()) {
                if (candidate.kind == ActionKind::Claim && candidate.claim == ClaimKind::Chow && !candidate.chowFirst.IsValid()) {
                    continue;  // a chow without its first tile has no JSON form: it is invalid_params, not an action
                }
                Rebuild(control, trace);
                const Json json = ActionToJson(candidate);
                const auto outcome = control.PerformAction(json);
                const bool accepted = outcome.has_value();
                if (!accepted && ErrorCodeOf(outcome.error()) != "illegal_action") {
                    return ::testing::AssertionFailure() << DumpJson(json).c_str() << " was refused with " << ErrorCodeOf(outcome.error());
                }
                if (accepted != IsListed(listed, json)) {
                    return ::testing::AssertionFailure() << DumpJson(json).c_str() << (accepted ? " is accepted but not listed" : " is listed but refused");
                }
            }
            return ::testing::AssertionSuccess();
        }

        class MahjongGameControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                game = New<MahjongGame>();
                control = game->AsControllable();
            }

            Json State() const {
                auto state = control->GetState();
                EXPECT_TRUE(state.has_value()) << CodeOf(state);
                return state ? *state : Json();
            }

            Json Actions() const {
                auto actions = control->GetActions();
                EXPECT_TRUE(actions.has_value()) << CodeOf(actions);
                return actions ? *actions : Json();
            }

            /// A started match from options such as {"seed": 7, "seats": "PCCC"}.
            void Reset(const char* options) {
                const auto reset = control->Reset(Parse(options));
                ASSERT_TRUE(reset.has_value()) << ErrorCodeOf(reset.error()) << ": " << reset.error().message.c_str();
            }

            /// Starts a match in which seat 0 holds East (the first seed that gives it), so that a person at seat 0 acts first.
            void ResetWithEastAtSeat0(const char* seats = "PCCC", const char* extra = "") {
                for (int seed = 1; seed <= kSeedsToSearch; ++seed) {
                    const std::string options = std::string("{\"seed\": ") + std::to_string(seed) + ", \"seats\": \"" + seats + "\"" + extra + "}";
                    ASSERT_NO_FATAL_FAILURE(Reset(options.c_str()));
                    if (game->GetMatch().East() == kSeat0) {
                        return;
                    }
                }
                FAIL() << "no seed gives East to seat 0";
            }

            /// Performs `action` and expects it to go through.
            Json Perform(const Json& action) {
                auto outcome = control->PerformAction(action);
                EXPECT_TRUE(outcome.has_value()) << DumpJson(action).c_str() << " -> " << CodeOf(outcome) << ": " << (outcome ? "" : outcome.error().message.c_str());
                return outcome ? *outcome : Json();
            }

            Json Perform(const char* action) {
                return Perform(Parse(action));
            }

            /// Every person seat that has to decide a claim window passes, until the hand asks for a discard again (or is over).
            void PassTheWindow() {
                for (int guard = 0; guard < kSeatCount; ++guard) {
                    const Json state = State();
                    if (Str(Member(state, "phase")) != "playing" || Str(Member(Member(state, "turn"), "phase")) == "discard") {
                        return;
                    }
                    Perform("{\"type\": \"pass\"}");
                }
                FAIL() << "the window does not close";
            }

            /// Lays the hand of `model` in place of the hand in progress; the match and the session go on from there.
            void Lay(const HandModel& model) {
                game->GetMatch().Hand() = model;
            }

            /// The action the "seek win" chooser takes from the listed ones, as JSON.
            Json Choose(MahjongRng& rng) const {
                const Json actions = Actions();
                eastl::vector<HandAction> hands;
                for (const Json& item : actions) {
                    const auto parsed = ActionFromJson(item);
                    EXPECT_TRUE(parsed.has_value());
                    if (parsed && !parsed->nextHand) {
                        hands.push_back(parsed->hand);
                    }
                }
                EXPECT_FALSE(hands.empty());
                return hands.empty() ? Json() : ActionToJson(Tests::Choose(hands, Mode::SeekWin, rng));
            }

            /// The state and the actions name no tile that only another seat holds (unless it lies open in the pile or in a set).
            void ExpectNoTileOfAnotherHand(uint32_t seed, int moves) const {
                const HandModel& hand = game->GetMatch().Hand();
                const Json state = State();
                const Seat viewer = Member(state, "seat").get<int>();
                const std::string text = std::string(DumpJson(state).c_str()) + std::string(DumpJson(Actions()).c_str());
                for (const uint8_t id : KindsOnlyOthersHold(hand, viewer)) {
                    const std::string quoted = "\"" + std::string(ToString(Tile{id})) + "\"";
                    // The piles and the open sets are public, so a kind that only others hold can be there only when it lies open.
                    bool lies_open = false;
                    for (const DiscardEntry& entry : hand.Discards()) {
                        lies_open = lies_open || entry.tile.Id() == id;
                    }
                    for (Seat seat = 0; seat < kSeatCount; ++seat) {
                        for (const Meld& meld : hand.GetHand(seat).melds) {
                            const bool inSet = meld.kind == MeldKind::Chow ? meld.first.Id() <= id && id <= meld.first.Id() + 2 : meld.first.Id() == id;
                            lies_open = lies_open || (!meld.concealed && inSet);
                        }
                    }
                    if (!lies_open) {
                        EXPECT_EQ(text.find(quoted), std::string::npos) << "seed " << seed << ", after " << moves << " moves: " << quoted << " is held only by another seat";
                    }
                }
            }

            /// The state without the version, to compare two states.
            Json Stable(Json state) const {
                state.erase("version");
                return state;
            }

            IntrusivePtr<MahjongGame> game;
            IControllableGame* control = nullptr;
        };

        /// The `levels` of a state: four numbers.
        std::array<int, kSeatCount> LevelsOf(const Json& state) {
            const Json& list = Member(state, "levels");
            std::array<int, kSeatCount> levels{};
            EXPECT_TRUE(list.is_array() && list.size() == kSeatCount);
            for (size_t seat = 0; seat < kSeatCount && seat < list.size(); ++seat) {
                levels[seat] = list[seat].get<int>();
            }
            return levels;
        }

        /// The points of the four seats of a state.
        int PointsOnTheTable(const Json& state) {
            int total = 0;
            for (const Json& seat : Member(state, "seats")) {
                total += Member(seat, "score").get<int>();
            }
            return total;
        }

        /// The sorted tile ids of a "hand" list.
        std::vector<int> HandIds(const Json& hand) {
            std::vector<int> ids;
            for (const Json& item : hand) {
                const auto tile = TryParse(Str(item));
                ids.push_back(tile ? tile->Id() : -1);
            }
            return ids;
        }

    }  // namespace

    // ----- the conversions -----

    TEST(MahjongGameControlConversionTest, EveryActionKindSurvivesTheRoundTrip) {
        eastl::vector<HandAction> samples = {HandAction::MakeDiscard(T("5p")),
                                             HandAction::MakeDiscard(T("Rd"), true),
                                             HandAction::MakeDeclareWin(),
                                             HandAction::MakeConcealedKong(T("3s")),
                                             HandAction::MakeUpgradeKong(T("Ew")),
                                             HandAction::MakePass(),
                                             HandAction::MakeClaim(ClaimKind::Pung),
                                             HandAction::MakeClaim(ClaimKind::Kong),
                                             HandAction::MakeClaim(ClaimKind::Mahjong),
                                             HandAction::MakeClaim(ClaimKind::Chow, T("2p")),
                                             HandAction::MakeClaim(ClaimKind::Chow, T("7m"))};
        for (const HandAction& sample : samples) {
            const auto read = ActionFromJson(ActionToJson(sample));
            ASSERT_TRUE(read.has_value()) << DumpJson(ActionToJson(sample)).c_str();
            EXPECT_FALSE(read->nextHand);
            EXPECT_EQ(read->hand, sample) << DumpJson(ActionToJson(sample)).c_str();
        }
        const auto next = ActionFromJson(NextHandActionToJson());
        ASSERT_TRUE(next.has_value());
        EXPECT_TRUE(next->nextHand);
    }

    TEST(MahjongGameControlConversionTest, TheJsonOfAnActionIsTheDocumentedShape) {
        EXPECT_EQ(DumpJson(ActionToJson(HandAction::MakeDiscard(T("5p"), true))), eastl::string("{\"declareCalling\":true,\"tile\":\"5p\",\"type\":\"discard\"}"));
        EXPECT_EQ(DumpJson(ActionToJson(HandAction::MakeClaim(ClaimKind::Chow, T("2p")))), eastl::string("{\"claim\":\"chow\",\"first\":\"2p\",\"type\":\"claim\"}"));
        EXPECT_EQ(DumpJson(ActionToJson(HandAction::MakeClaim(ClaimKind::Pung))), eastl::string("{\"claim\":\"pung\",\"type\":\"claim\"}"));
        EXPECT_EQ(DumpJson(ActionToJson(HandAction::MakeDeclareWin())), eastl::string("{\"type\":\"win\"}"));
        EXPECT_EQ(DumpJson(TileToJson(T("Ew"))), eastl::string("\"Ew\""));
        EXPECT_TRUE(TileToJson(Tile{}).is_null());
    }

    TEST(MahjongGameControlConversionTest, ADiscardWithoutTheDeclarationFieldDoesNotDeclare) {
        const auto read = ActionFromJson(Parse("{\"type\": \"discard\", \"tile\": \"5p\"}"));
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->hand, HandAction::MakeDiscard(T("5p"), false));
    }

    TEST(MahjongGameControlConversionTest, FieldsAKindIgnoresAreIgnored) {
        const auto pung = ActionFromJson(Parse("{\"type\": \"claim\", \"claim\": \"pung\", \"first\": \"2p\", \"tile\": \"9s\"}"));
        ASSERT_TRUE(pung.has_value());
        EXPECT_EQ(pung->hand, HandAction::MakeClaim(ClaimKind::Pung));
        const auto pass = ActionFromJson(Parse("{\"type\": \"pass\", \"tile\": \"9s\"}"));
        ASSERT_TRUE(pass.has_value());
        EXPECT_EQ(pass->hand, HandAction::MakePass());
    }

    TEST(MahjongGameControlConversionTest, MalformedActionsAreInvalidParams) {
        const char* const bad[] = {
            "{}",
            "[]",
            "5",
            "null",
            "\"discard\"",
            "{\"type\": 5}",
            "{\"type\": \"bogus\"}",
            "{\"type\": \"discard\"}",
            "{\"type\": \"discard\", \"tile\": \"zz\"}",
            "{\"type\": \"discard\", \"tile\": \"\"}",
            "{\"type\": \"discard\", \"tile\": 5}",
            "{\"type\": \"discard\", \"tile\": null}",
            "{\"type\": \"discard\", \"tile\": \"5p\", \"declareCalling\": \"yes\"}",
            "{\"type\": \"discard\", \"tile\": \"5p\", \"declareCalling\": 1}",
            "{\"type\": \"concealed_kong\"}",
            "{\"type\": \"upgrade_kong\", \"tile\": 3}",
            "{\"type\": \"claim\"}",
            "{\"type\": \"claim\", \"claim\": 5}",
            "{\"type\": \"claim\", \"claim\": \"sit\"}",
            "{\"type\": \"claim\", \"claim\": \"chow\"}",
            "{\"type\": \"claim\", \"claim\": \"chow\", \"first\": \"?\"}",
            "{\"type\": \"claim\", \"claim\": \"chow\", \"first\": 2}",
        };
        for (const char* text : bad) {
            const auto read = ActionFromJson(Parse(text));
            ASSERT_FALSE(read.has_value()) << text;
            EXPECT_EQ(ErrorCodeOf(read.error()), "invalid_params") << text;
        }
    }

    // ----- the state -----

    TEST_F(MahjongGameControlTest, BeforeTheFirstMatchEveryMethodAnswersNoActiveModel) {
        EXPECT_EQ(CodeOf(control->GetState()), "no_active_model");
        EXPECT_EQ(CodeOf(control->GetActions()), "no_active_model");
        EXPECT_EQ(CodeOf(control->PerformAction(Parse("{\"type\": \"pass\"}"))), "no_active_model");
        EXPECT_EQ(control->GetStateVersion(), 0u);
    }

    TEST_F(MahjongGameControlTest, SetStateIsNotSupported) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 7}"));
        const auto loaded = control->SetState(Json::object());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_EQ(ErrorCodeOf(loaded.error()), "unsupported");
    }

    TEST_F(MahjongGameControlTest, TheStateOfAFreshMatchIsTheViewOfThePersonWhoActs) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0());
        const Json state = State();

        EXPECT_EQ(Str(Member(state, "phase")), "playing");
        EXPECT_EQ(Member(state, "seat").get<int>(), kSeat0);
        EXPECT_EQ(Str(Member(state, "seatWind")), "East");
        EXPECT_EQ(Str(Member(state, "roundWind")), "East");
        EXPECT_EQ(Member(state, "east").get<int>(), kSeat0);
        EXPECT_EQ(Member(state, "level").get<int>(), LevelNumber(ComputerLevel::Normal)) << "the level of the match, Normal until a reset says otherwise";
        EXPECT_EQ(LevelsOf(state), (std::array<int, kSeatCount>{5, 5, 5, 5})) << "a level per seat";
        EXPECT_EQ(Member(state, "handsPlayed").get<int>(), 0);
        EXPECT_EQ(Member(state, "handsTotal").get<int>(), kFullMatchHands);
        EXPECT_EQ(Member(state, "version").get<uint64_t>(), control->GetStateVersion());
        EXPECT_GT(Member(state, "wall").get<int>(), 0);
        EXPECT_EQ(FindJsonMember(state, "result"), nullptr) << "no result while the hand is played";
        EXPECT_TRUE(Member(state, "discards").empty());

        const Json& hand = Member(state, "hand");
        ASSERT_EQ(hand.size(), static_cast<size_t>(kDealtTiles + 1)) << "East holds one tile more than the others";
        const std::vector<int> ids = HandIds(hand);
        EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));
        EXPECT_EQ(std::count(ids.begin(), ids.end(), -1), 0);

        const Json& turn = Member(state, "turn");
        EXPECT_EQ(Str(Member(turn, "phase")), "discard");
        EXPECT_EQ(Member(turn, "current").get<int>(), kSeat0);
        EXPECT_TRUE(Member(turn, "pendingTile").is_null());
        EXPECT_TRUE(Member(turn, "yourTurn").get<bool>());

        const Json& seats = Member(state, "seats");
        ASSERT_EQ(seats.size(), 4u);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const Json& entry = seats[static_cast<size_t>(seat)];
            EXPECT_EQ(Str(Member(entry, "kind")), seat == kSeat0 ? "person" : "computer");
            EXPECT_EQ(Member(entry, "score").get<int>(), kExpectedStartingScore);
            EXPECT_EQ(Member(entry, "concealedCount").get<int>(), seat == kSeat0 ? kDealtTiles + 1 : kDealtTiles);
            EXPECT_TRUE(Member(entry, "melds").empty());
            EXPECT_EQ(Member(entry, "concealedKongs").get<int>(), 0);
            EXPECT_TRUE(Member(entry, "bonus").empty());
            EXPECT_EQ(Member(entry, "discardCount").get<int>(), 0);
            EXPECT_FALSE(Member(entry, "calling").get<bool>());
            EXPECT_EQ(Str(Member(entry, "wind")), std::string(EnumUtils<Wind>::ToString(MakeSeatWinds(kSeat0)[static_cast<size_t>(seat)]).data()));
        }
    }

    TEST_F(MahjongGameControlTest, ThePersonSeatIsOtherThanZeroWhenItIsTheOneToAct) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 7, \"seats\": \"CCPC\"}"));
        const Json state = State();
        // Seat 2 is the only person: the view is its view whoever acts, and the computers answered up to its turn.
        EXPECT_EQ(Member(state, "seat").get<int>(), kSeat2);
        EXPECT_TRUE(Member(Member(state, "turn"), "yourTurn").get<bool>());
        EXPECT_EQ(HandIds(Member(state, "hand")).size(), game->GetMatch().Hand().GetHand(kSeat2).concealed.size());
    }

    TEST_F(MahjongGameControlTest, TheSameSeedGivesTheSameState) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 5, \"seats\": \"PCCC\"}"));
        const Json first = Stable(State());
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 6, \"seats\": \"PCCC\"}"));
        const Json other = Stable(State());
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 5, \"seats\": \"PCCC\"}"));
        EXPECT_EQ(Stable(State()), first);
        EXPECT_NE(other, first);
    }

    TEST_F(MahjongGameControlTest, TheOutputIsTheSameInEveryLanguage) {
        UIStrings& strings = CoreManager::GetUIStrings();
        ASSERT_TRUE(LoadShippedStrings(strings).empty());

        // The same seed, the same moves: the state, the listed actions and the outcome of what a client does
        const auto play = [&](const char* language) {
            EXPECT_TRUE(game->SetLanguage(PoolString::Intern(language))) << language;
            ResetWithEastAtSeat0();
            // The version counts the changes of the game since it was built, so the second run does not start at the same number
            const auto unversioned = [](Json json) {
                json.erase("version");
                return json;
            };
            std::vector<Json> seen;
            for (int move = 0; move < kMovesChecked && Str(Member(State(), "phase")) == "playing"; ++move) {
                seen.push_back(unversioned(State()));
                const Json actions = Actions();
                seen.push_back(actions);
                if (actions.empty()) {
                    break;
                }
                seen.push_back(unversioned(Perform(actions[0])));
            }
            seen.push_back(unversioned(State()));
            return seen;
        };
        const std::vector<Json> english = play("en");
        const std::vector<Json> russian = play("ru");

        ASSERT_GT(english.size(), 6u) << "a few moves were played";
        EXPECT_EQ(russian, english) << "ids, tile notation and the keys of the JSON are not translated";
        EXPECT_EQ(CoreManager::GetUIStrings().GetLanguage(), "ru"_intern);
        strings.Clear();
    }

    TEST_F(MahjongGameControlTest, TheStateAndTheActionsAreTheSameInEveryHintsMode) {
        const auto play = [&](HintsMode mode) {
            game->SetHintsMode(mode);
            ResetWithEastAtSeat0();
            const auto unversioned = [](Json json) {
                json.erase("version");
                return json;
            };
            std::vector<Json> seen;
            for (int move = 0; move < kMovesChecked && Str(Member(State(), "phase")) == "playing"; ++move) {
                seen.push_back(unversioned(State()));
                const Json actions = Actions();
                seen.push_back(actions);
                if (actions.empty()) {
                    break;
                }
                seen.push_back(unversioned(Perform(actions[0])));
            }
            return seen;
        };
        const std::vector<Json> off = play(HintsMode::Off);
        ASSERT_GT(off.size(), 6u) << "a few moves were played";
        EXPECT_EQ(play(HintsMode::Hints), off);
        EXPECT_EQ(play(HintsMode::Coach), off) << "the coach judges the moves and tells the API nothing";
    }

    TEST_F(MahjongGameControlTest, NoTileOfAnotherHandIsInTheStateOrTheActions) {
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            ASSERT_NO_FATAL_FAILURE(Reset((std::string("{\"seed\": ") + std::to_string(seed) + ", \"seats\": \"PCCC\"}").c_str()));
            // The fresh deal and the states after some moves of the person (the computers answer each time).
            for (int moves = 0; moves <= kMovesChecked; ++moves) {
                if (Str(Member(State(), "phase")) != "playing") {
                    break;
                }
                ASSERT_NO_FATAL_FAILURE(ExpectNoTileOfAnotherHand(seed, moves));
                const Json actions = Actions();
                ASSERT_FALSE(actions.empty());
                Perform(actions[0]);
            }
        }
    }

    TEST_F(MahjongGameControlTest, ASpectatorSeesNoConcealedTileWhileTheHandIsPlayed) {
        // No person seat: a scene paces the computers, so a match started by the game waits at its first action.
        game->SetSeats(SeatSetup{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer});
        ASSERT_TRUE(game->StartMatch(4).has_value());
        ASSERT_EQ(game->GetSession().GetPhase(), SessionPhase::Playing);
        for (int step = 0; step < kMovesChecked; ++step) {
            const Json state = State();
            EXPECT_TRUE(Member(state, "seat").is_null());
            EXPECT_TRUE(Member(state, "seatWind").is_null());
            EXPECT_TRUE(Member(state, "hand").empty());
            EXPECT_FALSE(Member(Member(state, "turn"), "yourTurn").get<bool>());
            EXPECT_TRUE(Actions().empty());
            // Only the counts of the concealed tiles; the open sets and the pile are public.
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(Member(Member(state, "seats")[static_cast<size_t>(seat)], "concealedCount").get<size_t>(), game->GetMatch().Hand().GetHand(seat).concealed.size());
            }
            if (game->GetSession().GetPhase() != SessionPhase::Playing) {
                break;
            }
            ASSERT_TRUE(game->GetSession().StepComputer().has_value());
        }
    }

    TEST_F(MahjongGameControlTest, ABonusTileIsPublicAndAComputersTurnListsNoActions) {
        // Seat 0 (East) is a computer, seat 1 the only person; the computers have played up to seat 1, then a fixed hand is laid.
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0("CPCC", ", \"flowers\": true"));
        FixedWallSpec spec;
        spec.hands[kSeat0] = std::string(kFlowerTile) + " 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        spec.hands[kSeat1] = kQuiet1;
        spec.hands[kSeat2] = kQuiet2;
        spec.hands[kSeat3] = kQuiet3;
        spec.live = "9p Wd";
        spec.loose = "1p";
        ASSERT_NO_FATAL_FAILURE(Lay(MakeModel(spec, kSeat0, Wind::East, true)));
        ASSERT_EQ(game->GetSession().Actor(), kSeat0);

        const Json state = State();
        // The view is of the person, the actor is a computer: the flower of seat 0 lies open and nobody has actions.
        EXPECT_EQ(Member(state, "seat").get<int>(), kSeat1);
        const Json& seats = Member(state, "seats");
        EXPECT_EQ(Strings(Member(seats[0], "bonus")), std::vector<std::string>{kFlowerTile});
        EXPECT_TRUE(Member(seats[1], "bonus").empty());
        EXPECT_FALSE(Member(Member(state, "turn"), "yourTurn").get<bool>());
        EXPECT_EQ(Member(state, "hand").size(), static_cast<size_t>(kDealtTiles));
        EXPECT_TRUE(Actions().empty());
        const uint64_t version = control->GetStateVersion();
        EXPECT_EQ(CodeOf(control->PerformAction(Parse("{\"type\": \"pass\"}"))), "illegal_action");
        EXPECT_EQ(control->GetStateVersion(), version);
    }

    // ----- the actions -----

    TEST_F(MahjongGameControlTest, TheListedActionsAreTheLegalActionsOfTheActor) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0());
        const Json actions = Actions();
        const eastl::vector<HandAction> legal = game->GetMatch().Hand().LegalActions(kSeat0);

        ASSERT_EQ(actions.size(), legal.size());
        for (size_t index = 0; index < legal.size(); ++index) {
            const auto read = ActionFromJson(actions[index]);
            ASSERT_TRUE(read.has_value());
            EXPECT_EQ(read->hand, legal[index]);
            EXPECT_EQ(actions[index], ActionToJson(legal[index]));
        }
    }

    TEST_F(MahjongGameControlTest, ActionsAreAcceptedExactlyWhenListedAtTheStartAndInAClaimWindow) {
        Trace trace;
        trace.options = "{\"seed\": 11, \"seats\": \"PCCC\"}";
        EXPECT_TRUE(ApiAcceptsExactlyWhatIsListed(*game, trace));

        // Play on (discarding) until the person is asked about a discard: a window with claims and a pass.
        MahjongRng rng(1);
        bool reached = false;
        for (uint32_t seed = 1; seed <= 40 && !reached; ++seed) {
            trace.options = "{\"seed\": " + std::to_string(seed) + ", \"seats\": \"PCCC\"}";
            trace.actions.clear();
            ASSERT_NO_FATAL_FAILURE(Rebuild(*control, trace));
            for (int step = 0; step < kStepsToAClaimWindow && !reached; ++step) {
                const Json state = State();
                if (Str(Member(state, "phase")) != "playing") {
                    break;
                }
                if (Str(Member(Member(state, "turn"), "phase")) == "claim" && Member(Member(state, "turn"), "yourTurn").get<bool>()) {
                    reached = Actions().size() >= 2;
                    break;
                }
                const Json action = Choose(rng);
                ASSERT_TRUE(control->PerformAction(action).has_value());
                trace.actions.push_back(action);
            }
        }
        ASSERT_TRUE(reached) << "no claim window for the person in 40 matches";
        const Json listed = Actions();
        EXPECT_TRUE(IsListed(listed, Parse("{\"type\": \"pass\"}")));
        EXPECT_TRUE(ApiAcceptsExactlyWhatIsListed(*game, trace));
    }

    TEST_F(MahjongGameControlTest, ADiscardMovesTheTurnAndTheComputersAnswer) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0());
        const Json before = State();
        const uint64_t version = control->GetStateVersion();
        const std::string tile = Str(Member(before, "hand")[0]);

        const Json outcome = Perform(("{\"type\": \"discard\", \"tile\": \"" + tile + "\", \"declareCalling\": false}").c_str());

        EXPECT_EQ(Str(Member(Member(outcome, "applied"), "type")), "discard");
        EXPECT_EQ(Str(Member(outcome, "phase")), "playing");
        EXPECT_GT(Member(outcome, "version").get<uint64_t>(), version);
        EXPECT_EQ(Member(outcome, "version").get<uint64_t>(), control->GetStateVersion());

        const Json after = State();
        EXPECT_EQ(Member(after, "version").get<uint64_t>(), control->GetStateVersion());
        EXPECT_TRUE(Member(Member(after, "turn"), "yourTurn").get<bool>()) << "the computers played up to the person's next decision";
        EXPECT_EQ(Member(Member(after, "seats")[0], "discardCount").get<int>(), 1);
        int computerDiscards = 0;
        for (size_t seat = 1; seat < kSeatCount; ++seat) {
            computerDiscards += Member(Member(after, "seats")[seat], "discardCount").get<int>();
        }
        EXPECT_GE(computerDiscards, 1) << "the computers answered";
        EXPECT_FALSE(Member(after, "discards").empty());
    }

    TEST_F(MahjongGameControlTest, AClaimWindowIsSeenByAPersonSeat) {
        MahjongRng rng(2);
        bool seen = false;
        for (uint32_t seed = 1; seed <= 60 && !seen; ++seed) {
            ASSERT_NO_FATAL_FAILURE(Reset(("{\"seed\": " + std::to_string(seed) + ", \"seats\": \"PCCC\"}").c_str()));
            for (int step = 0; step < kStepsToAClaimWindow; ++step) {
                const Json state = State();
                if (Str(Member(state, "phase")) != "playing") {
                    break;
                }
                const Json& turn = Member(state, "turn");
                if (Str(Member(turn, "phase")) == "claim" && Member(turn, "yourTurn").get<bool>()) {
                    seen = true;
                    // The pending tile is the last of the pile, the discarder is not the viewer, and the actions are claims and a pass.
                    const Json& discards = Member(state, "discards");
                    ASSERT_FALSE(discards.empty());
                    EXPECT_EQ(Member(turn, "pendingTile"), Member(discards[discards.size() - 1], "tile"));
                    EXPECT_EQ(Member(turn, "current"), Member(discards[discards.size() - 1], "seat"));
                    EXPECT_NE(Member(turn, "current").get<int>(), Member(state, "seat").get<int>());
                    bool hasPass = false;
                    for (const Json& action : Actions()) {
                        const std::string type = Str(Member(action, "type"));
                        EXPECT_TRUE(type == "claim" || type == "pass") << type;
                        hasPass = hasPass || type == "pass";
                    }
                    EXPECT_TRUE(hasPass);
                    break;
                }
                Perform(Choose(rng));
            }
        }
        EXPECT_TRUE(seen) << "no claim window for the person in 60 matches";
    }

    TEST_F(MahjongGameControlTest, ActionsOfEveryKindAreAcceptedWhenPlayedThroughTheApi) {
        // Over many matches the person takes the claims, kongs, declarations and wins that the chooser prefers; between them every kind
        // that random play reaches is accepted. (The kongs and the robbery have their own tests on fixed walls.)
        std::set<std::string> played;
        MahjongRng rng(3);
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            ASSERT_NO_FATAL_FAILURE(Reset(("{\"seed\": " + std::to_string(seed) + ", \"seats\": \"PCCC\", \"hands\": 8}").c_str()));
            for (int step = 0; step < kActionLimitPerMatch; ++step) {
                const Json state = State();
                const std::string phase = Str(Member(state, "phase"));
                if (phase == "match_over") {
                    break;
                }
                if (phase == "hand_over") {
                    Perform("{\"type\": \"next_hand\"}");
                    continue;
                }
                const Json action = Choose(rng);
                std::string name = Str(Member(action, "type"));
                if (name == "claim") {
                    name += ":" + Str(Member(action, "claim"));
                } else if (name == "discard" && Member(action, "declareCalling").get<bool>()) {
                    name = "discard:declaring";
                }
                played.insert(name);
                Perform(action);
            }
        }
        for (const char* wanted : {"discard", "discard:declaring", "win", "claim:chow", "claim:pung", "claim:mahjong"}) {
            EXPECT_EQ(played.count(wanted), 1u) << wanted << " never came up";
        }
    }

    // ----- errors -----

    TEST_F(MahjongGameControlTest, ALegalLookingActionOfTheWrongTurnIsIllegalAndChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0());
        const Json state = State();
        const uint64_t version = control->GetStateVersion();
        const PlayerHand& own = game->GetMatch().Hand().GetHand(kSeat0);
        Tile missing;
        for (uint8_t id = 0; id < kKindCount && !missing.IsValid(); ++id) {
            if (own.CountConcealed(Tile{id}) == 0) {
                missing = Tile{id};
            }
        }
        ASSERT_TRUE(missing.IsValid());

        const std::string tile(ToString(missing));
        const std::string illegal[] = {
            "{\"type\": \"discard\", \"tile\": \"" + tile + "\"}",
            "{\"type\": \"discard\", \"tile\": \"F1\"}",
            "{\"type\": \"win\"}",
            "{\"type\": \"pass\"}",
            "{\"type\": \"next_hand\"}",
            "{\"type\": \"claim\", \"claim\": \"pung\"}",
            "{\"type\": \"claim\", \"claim\": \"mahjong\"}",
            "{\"type\": \"claim\", \"claim\": \"chow\", \"first\": \"2p\"}",
            "{\"type\": \"concealed_kong\", \"tile\": \"" + tile + "\"}",
            "{\"type\": \"upgrade_kong\", \"tile\": \"" + tile + "\"}",
        };
        for (const std::string& text : illegal) {
            const auto outcome = control->PerformAction(Parse(text.c_str()));
            ASSERT_FALSE(outcome.has_value()) << text;
            EXPECT_EQ(ErrorCodeOf(outcome.error()), "illegal_action") << text;
            EXPECT_EQ(control->GetStateVersion(), version) << text;
            EXPECT_EQ(State(), state) << text;
        }
    }

    TEST_F(MahjongGameControlTest, MalformedActionsAreInvalidParamsAndChangeNothing) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0());
        const Json state = State();
        const uint64_t version = control->GetStateVersion();
        const char* const bad[] = {"{}",
                                   "{\"type\": \"bogus\"}",
                                   "{\"type\": \"discard\"}",
                                   "{\"type\": \"discard\", \"tile\": 5}",
                                   "{\"type\": \"discard\", \"tile\": \"5p\", \"declareCalling\": \"yes\"}",
                                   "{\"type\": \"claim\", \"claim\": \"chow\"}",
                                   "[]",
                                   "5"};
        for (const char* text : bad) {
            EXPECT_EQ(CodeOf(control->PerformAction(Parse(text))), "invalid_params") << text;
            EXPECT_EQ(control->GetStateVersion(), version) << text;
            EXPECT_EQ(State(), state) << text;
        }
    }

    TEST_F(MahjongGameControlTest, ResetOptionsAreCheckedAndAnInvalidOneChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 9, \"seats\": \"PCCC\"}"));
        const Json state = State();
        const uint64_t version = control->GetStateVersion();
        const SeatSetup seats = game->GetSeats();
        const char* const bad[] = {
            "{\"seats\": \"PCC\"}",
            "{\"seats\": \"PCCX\"}",
            "{\"seats\": \"PCCCC\"}",
            "{\"seats\": \"pccc\"}",
            "{\"seats\": 4}",
            "{\"hands\": 12}",
            "{\"hands\": \"8\"}",
            "{\"hands\": 8.5}",
            "{\"hands\": 0}",
            "{\"seed\": -1}",
            "{\"seed\": 4294967296}",
            "{\"seed\": \"1\"}",
            "{\"seed\": 1.5}",
            "{\"seed\": null}",
            "{\"flowers\": \"yes\"}",
            "{\"flowers\": 1}",
            "{\"level\": \"hard\"}",
            "{\"level\": \"easy\"}",
            "{\"level\": \"normal\"}",
            "{\"level\": \"3\"}",
            "{\"level\": \"\"}",
            "{\"level\": 0}",
            "{\"level\": 11}",
            "{\"level\": -1}",
            "{\"level\": 2.5}",
            "{\"level\": true}",
            "{\"level\": null}",
            "{\"level\": [3]}",
            "{\"levels\": [1, 2, 3]}",
            "{\"levels\": [1, 2, 3, 4, 5]}",
            "{\"levels\": []}",
            "{\"levels\": [0, 5, 5, 5]}",
            "{\"levels\": [5, 5, 5, 11]}",
            "{\"levels\": [5, 5, -1, 5]}",
            "{\"levels\": [5, 5, 2.5, 5]}",
            "{\"levels\": [\"5\", 5, 5, 5]}",
            "{\"levels\": [true, 5, 5, 5]}",
            "{\"levels\": [null, 5, 5, 5]}",
            "{\"levels\": [[5], 5, 5, 5]}",
            "{\"levels\": 5}",
            "{\"levels\": \"5555\"}",
            "{\"levels\": {\"0\": 5}}",
            "{\"levels\": null}",
            "{\"levels\": [1, 2, 3, 4], \"level\": 3}",
            "{\"level\": 3, \"levels\": [1, 2, 3, 4]}",
            "{\"levels\": [1, 2, 3, 4], \"seats\": \"XXXX\"}",
            "{\"level\": 1, \"seats\": \"XXXX\"}",
            "{\"level\": 1, \"hands\": 12}",
            "{\"hands\": 8, \"seats\": \"XXXX\"}",
        };
        for (const char* text : bad) {
            const auto reset = control->Reset(Parse(text));
            ASSERT_FALSE(reset.has_value()) << text;
            EXPECT_EQ(ErrorCodeOf(reset.error()), "invalid_params") << text;
            EXPECT_EQ(control->GetStateVersion(), version) << text;
            EXPECT_EQ(State(), state) << text;
            EXPECT_EQ(game->GetSeats(), seats) << text;
            EXPECT_EQ(game->GetSettings().handsPerMatch, kFullMatchHands) << text;
            EXPECT_EQ(game->GetLevels(), UniformLevels(ComputerLevel::Normal)) << text;
            EXPECT_EQ(game->GetSession().Levels(), UniformLevels(ComputerLevel::Normal)) << text;
        }
        // The limits of a seed are accepted.
        EXPECT_TRUE(control->Reset(Parse("{\"seed\": 0}")).has_value());
        EXPECT_TRUE(control->Reset(Parse(("{\"seed\": " + std::to_string(kMaxSeed) + "}").c_str())).has_value());
    }

    TEST_F(MahjongGameControlTest, ResetKeepsWhatItLeavesOutAndStoresWhatItSets) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 4, \"seats\": \"CCCC\", \"hands\": 8, \"flowers\": true}"));
        EXPECT_EQ(game->GetSeats(), (SeatSetup{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer}));
        EXPECT_EQ(game->GetSettings().handsPerMatch, kShortMatchHands);
        EXPECT_TRUE(game->GetSettings().flowers);
        const Json first = Stable(State());

        ASSERT_NO_FATAL_FAILURE(Reset("{}"));  // the seed of the last reset, the seats and the settings as they were
        EXPECT_EQ(Stable(State()), first);
        const Json again = State();
        EXPECT_EQ(Member(again, "handsTotal").get<int>(), kShortMatchHands);
    }

    TEST_F(MahjongGameControlTest, ResetSetsTheLevelAndTheStateSaysIt) {
        for (const ComputerLevel level : kAllLevels) {
            const int number = LevelNumber(level);
            ASSERT_NO_FATAL_FAILURE(Reset(("{\"seed\": 8, \"seats\": \"PCCC\", \"level\": " + std::to_string(number) + "}").c_str()));
            EXPECT_EQ(game->GetLevels()[0], level) << number;
            EXPECT_EQ(game->GetLevels(), UniformLevels(level)) << number;
            EXPECT_EQ(game->GetSession().Levels(), UniformLevels(level)) << number;
            EXPECT_EQ(Member(State(), "level").get<int>(), number);
            EXPECT_EQ(LevelsOf(State()), (std::array<int, kSeatCount>{number, number, number, number}));

            // What is left out keeps the level of the game, also when the next reset changes other things
            ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 9}"));
            EXPECT_EQ(game->GetLevels()[0], level) << number;
            EXPECT_EQ(Member(State(), "level").get<int>(), number);
        }
        ASSERT_NO_FATAL_FAILURE(Reset("{}"));
        EXPECT_EQ(Member(State(), "level").get<int>(), kMaxLevel) << "the last level set";
    }

    TEST_F(MahjongGameControlTest, ResetSetsALevelPerSeatAndTheStateSaysThemAndTheLowestComputerSeatsOne) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 8, \"seats\": \"CCCC\", \"levels\": [2, 6, 10, 1]}"));
        const SeatLevels want{ComputerLevel{2}, ComputerLevel{6}, ComputerLevel{10}, ComputerLevel{1}};
        EXPECT_EQ(game->GetLevels(), want);
        EXPECT_EQ(game->GetSession().Levels(), want);
        EXPECT_EQ(LevelsOf(State()), (std::array<int, kSeatCount>{2, 6, 10, 1}));
        EXPECT_EQ(Member(State(), "level").get<int>(), 2) << "the lowest computer seat is seat 0";

        // A person's seat keeps its level (it plays again when the seat becomes a computer); `level` is the first computer seat's
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 8, \"seats\": \"PCPC\"}"));
        EXPECT_EQ(game->GetSession().Levels(), want) << "a reset without levels keeps those of the game";
        EXPECT_EQ(LevelsOf(State()), (std::array<int, kSeatCount>{2, 6, 10, 1}));
        EXPECT_EQ(Member(State(), "level").get<int>(), 6);
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 8, \"seats\": \"PPPP\"}"));
        EXPECT_EQ(Member(State(), "level").get<int>(), LevelNumber(kDefaultLevel)) << "no computer seat";

        // `level` sets all four, over what `levels` set before
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 8, \"seats\": \"PCCC\", \"level\": 3}"));
        EXPECT_EQ(LevelsOf(State()), (std::array<int, kSeatCount>{3, 3, 3, 3}));
        EXPECT_EQ(Member(State(), "level").get<int>(), 3);
        EXPECT_EQ(game->GetSession().Levels(), UniformLevels(ComputerLevel{3}));
    }

    TEST_F(MahjongGameControlTest, ABadLevelsNamesTheKey) {
        for (const char* text : {"{\"levels\": [1, 2, 3]}", "{\"levels\": [1, 2, 3, 11]}", "{\"levels\": \"1234\"}"}) {
            const auto reset = control->Reset(Parse(text));
            ASSERT_FALSE(reset.has_value()) << text;
            EXPECT_EQ(ErrorCodeOf(reset.error()), "invalid_params") << text;
            const std::string message = reset.error().message.c_str();
            EXPECT_NE(message.find("'levels'"), std::string::npos) << message;
            EXPECT_NE(message.find("1..10"), std::string::npos) << message;
        }
        const auto both = control->Reset(Parse("{\"level\": 3, \"levels\": [1, 2, 3, 4]}"));
        ASSERT_FALSE(both.has_value());
        const std::string message = both.error().message.c_str();
        EXPECT_NE(message.find("'level'"), std::string::npos) << message;
        EXPECT_NE(message.find("'levels'"), std::string::npos) << message;
    }

    TEST_F(MahjongGameControlTest, AWholeMatchOfSeatsAtDifferentLevelsEndsLegallyWithAllThePoints) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 5, \"seats\": \"CCCC\", \"hands\": 8, \"levels\": [1, 4, 5, 10]}"));
        int hands = 0;
        for (; hands < kHandLimitPerMatch && Str(Member(State(), "phase")) != "match_over"; ++hands) {
            ASSERT_EQ(Str(Member(State(), "phase")), "hand_over");
            EXPECT_EQ(LevelsOf(State()), (std::array<int, kSeatCount>{1, 4, 5, 10}));
            Perform(NextHandActionToJson());
            EXPECT_EQ(PointsOnTheTable(State()), kSeatCount * kExpectedStartingScore);
        }
        ASSERT_LT(hands, kHandLimitPerMatch);
        EXPECT_EQ(Member(State(), "handsPlayed").get<int>(), kShortMatchHands);
    }

    TEST_F(MahjongGameControlTest, ABadLevelNamesTheRange) {
        const auto reset = control->Reset(Parse("{\"level\": \"easy\"}"));
        ASSERT_FALSE(reset.has_value());
        EXPECT_EQ(ErrorCodeOf(reset.error()), "invalid_params");
        const std::string message = reset.error().message.c_str();
        EXPECT_NE(message.find("'level'"), std::string::npos) << message;
        EXPECT_NE(message.find("1..10"), std::string::npos) << message;
    }

    TEST_F(MahjongGameControlTest, AWholeMatchAtEveryLevelEndsLegallyWithAllThePoints) {
        for (const ComputerLevel level : kAllLevels) {
            const std::string name(LevelText(level));
            ASSERT_NO_FATAL_FAILURE(Reset(("{\"seed\": 5, \"seats\": \"CCCC\", \"hands\": 8, \"level\": " + name + "}").c_str()));
            int hands = 0;
            for (; hands < kHandLimitPerMatch && Str(Member(State(), "phase")) != "match_over"; ++hands) {
                ASSERT_EQ(Str(Member(State(), "phase")), "hand_over") << name;
                EXPECT_EQ(Member(State(), "level").get<int>(), LevelNumber(level));
                Perform(NextHandActionToJson());
                EXPECT_EQ(PointsOnTheTable(State()), kSeatCount * kExpectedStartingScore) << name;
            }
            ASSERT_LT(hands, kHandLimitPerMatch) << name;
            const Json end = State();
            EXPECT_EQ(Member(end, "handsPlayed").get<int>(), kShortMatchHands) << name;
            EXPECT_EQ(PointsOnTheTable(end), kSeatCount * kExpectedStartingScore) << name;
            EXPECT_EQ(Member(end, "level").get<int>(), LevelNumber(level));
        }
    }

    // ----- the end of a hand and of a match -----

    TEST_F(MahjongGameControlTest, WithNoPersonAResetPlaysTheHandAndNextHandGoesOnToTheEndOfTheMatch) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 3, \"seats\": \"CCCC\", \"hands\": 8}"));

        uint64_t version = control->GetStateVersion();
        int hands = 0;
        for (; hands < kHandLimitPerMatch; ++hands) {
            const Json state = State();
            const std::string phase = Str(Member(state, "phase"));
            if (phase == "match_over") {
                break;
            }
            ASSERT_EQ(phase, "hand_over");
            // The hand is over: all four hands and breakdowns, next_hand is the only action, and a play action is illegal.
            const Json& result = Member(state, "result");
            ASSERT_EQ(Member(result, "seats").size(), 4u);
            for (const Json& entry : Member(result, "seats")) {
                EXPECT_TRUE(Member(entry, "concealed").is_array());
                EXPECT_TRUE(Member(entry, "melds").is_array());
                EXPECT_TRUE(Member(entry, "bonus").is_array());
                EXPECT_TRUE(Member(Member(entry, "breakdown"), "lines").is_array());
                EXPECT_TRUE(Member(entry, "breakdown").is_object());
            }
            EXPECT_EQ(Member(result, "scores").size(), 4u);
            EXPECT_EQ(Member(result, "deltas").size(), 4u);
            EXPECT_EQ(Member(Member(state, "turn"), "phase"), Json("finished"));
            EXPECT_FALSE(Member(Member(state, "turn"), "yourTurn").get<bool>());
            const Json actions = Actions();
            ASSERT_EQ(actions.size(), 1u);
            EXPECT_EQ(actions[0], NextHandActionToJson());
            EXPECT_EQ(CodeOf(control->PerformAction(Parse("{\"type\": \"pass\"}"))), "illegal_action");

            const Json outcome = Perform(NextHandActionToJson());
            const Json& settlement = Member(outcome, "settlement");
            EXPECT_EQ(Member(settlement, "kind"), Member(result, "kind"));
            EXPECT_EQ(Member(settlement, "winner"), Member(result, "winner"));
            EXPECT_EQ(Member(settlement, "scores"), Member(result, "scores"));
            EXPECT_EQ(Member(settlement, "deltas"), Member(result, "deltas"));
            EXPECT_GT(Member(outcome, "version").get<uint64_t>(), version) << "the version grows with every hand";
            version = Member(outcome, "version").get<uint64_t>();
            EXPECT_EQ(PointsOnTheTable(State()), kSeatCount * kExpectedStartingScore);
        }
        ASSERT_LT(hands, kHandLimitPerMatch);

        const Json end = State();
        EXPECT_EQ(Str(Member(end, "phase")), "match_over");
        EXPECT_EQ(Member(end, "handsPlayed").get<int>(), kShortMatchHands);
        EXPECT_EQ(Member(end, "handsTotal").get<int>(), kShortMatchHands);
        EXPECT_TRUE(Member(end, "result").is_object());
        EXPECT_TRUE(Actions().empty());
        EXPECT_EQ(CodeOf(control->PerformAction(NextHandActionToJson())), "illegal_action");
        EXPECT_EQ(control->GetStateVersion(), version);
    }

    TEST_F(MahjongGameControlTest, APersonWhoSeeksTheWinPlaysAWholeMatch) {
        ASSERT_NO_FATAL_FAILURE(Reset("{\"seed\": 21, \"seats\": \"PCCC\", \"hands\": 8}"));
        MahjongRng rng(4);
        uint64_t version = control->GetStateVersion();
        int steps = 0;
        for (; steps < kActionLimitPerMatch; ++steps) {
            const Json state = State();
            const std::string phase = Str(Member(state, "phase"));
            if (phase == "match_over") {
                break;
            }
            Json outcome;
            if (phase == "hand_over") {
                outcome = Perform(NextHandActionToJson());
                EXPECT_EQ(PointsOnTheTable(State()), kSeatCount * kExpectedStartingScore);
            } else {
                ASSERT_TRUE(Member(Member(state, "turn"), "yourTurn").get<bool>()) << "the computers stop only where the person decides";
                ASSERT_FALSE(Actions().empty());
                outcome = Perform(Choose(rng));
            }
            const uint64_t now = Member(outcome, "version").get<uint64_t>();
            EXPECT_GT(now, version) << "the version grows with every change";
            EXPECT_EQ(now, control->GetStateVersion());
            version = now;
        }
        ASSERT_LT(steps, kActionLimitPerMatch);
        const Json end = State();
        EXPECT_EQ(Str(Member(end, "phase")), "match_over");
        EXPECT_EQ(PointsOnTheTable(end), kSeatCount * kExpectedStartingScore);
        EXPECT_EQ(Member(end, "handsPlayed").get<int>(), kShortMatchHands);
    }

    // ----- fixed walls: the kongs, the win and the robbery -----

    TEST_F(MahjongGameControlTest, AConcealedKongIsListedAndLaysAMeld) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0("PPPP"));
        FixedWallSpec spec = SpecOf("1p 1p 1p 1p 4s 5s 6s 7m 8m 9m Ew Ew Ew", "9p");
        spec.loose = "1m";
        ASSERT_NO_FATAL_FAILURE(Lay(MakeModel(spec, kSeat0)));

        const Json actions = Actions();
        EXPECT_TRUE(IsListed(actions, Parse("{\"type\": \"concealed_kong\", \"tile\": \"1p\"}")));
        const Json before = State();
        const size_t handSize = Member(before, "hand").size();

        const Json outcome = Perform("{\"type\": \"concealed_kong\", \"tile\": \"1p\"}");

        EXPECT_EQ(Str(Member(Member(outcome, "applied"), "type")), "concealed_kong");
        const Json after = State();
        const Json& mine = Member(after, "seats")[0];
        EXPECT_EQ(Member(mine, "concealedKongs").get<int>(), 1);
        ASSERT_EQ(Member(mine, "melds").size(), 1u);
        const Json& meld = Member(mine, "melds")[0];
        EXPECT_EQ(Str(Member(meld, "kind")), "kong");
        EXPECT_TRUE(Member(meld, "concealed").get<bool>());
        EXPECT_EQ(Strings(Member(meld, "tiles")), (std::vector<std::string>{"1p", "1p", "1p", "1p"}));
        EXPECT_EQ(Member(after, "hand").size(), handSize - 4 + 1) << "four tiles laid, one loose tile drawn";
        EXPECT_EQ(Str(Member(Member(after, "turn"), "phase")), "discard");
        EXPECT_TRUE(Member(Member(after, "turn"), "yourTurn").get<bool>());
    }

    TEST_F(MahjongGameControlTest, ADeclaredHandWinsBySelfDrawAndNextHandSettlesIt) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0("PPPP"));
        constexpr Seat east = kSeat0;
        // East waits on the red dragon and is dealt a lap of tiles: it draws 9p, then the others draw one each, then East draws the Rd.
        FixedWallSpec spec;
        spec.hands[kSeat0] = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        spec.hands[kSeat1] = "1m 3m 5m 7m 9m 2s 4s 6s 8s 1p 3p 5p 7p";
        spec.hands[kSeat2] = "2m 4m 6m 8m 1s 3s 5s 7s 9s 2p 4p 6p 8p";
        spec.hands[kSeat3] = "9p 1m 3m 5m 7m 9m 2s 4s 6s 8s Sw Ww Nw";
        spec.live = "9p 4m 6m 8p Rd";
        ASSERT_NO_FATAL_FAILURE(Lay(MakeModel(spec, east)));

        // East declares with its first discard (the listed discard that leaves a waiting hand), the others discard what they drew.
        ASSERT_TRUE(IsListed(Actions(), Parse("{\"type\": \"discard\", \"tile\": \"9p\", \"declareCalling\": true}")));
        Perform("{\"type\": \"discard\", \"tile\": \"9p\", \"declareCalling\": true}");
        for (const char* tile : {"4m", "6m", "8p"}) {
            ASSERT_NO_FATAL_FAILURE(PassTheWindow());
            Perform((std::string("{\"type\": \"discard\", \"tile\": \"") + tile + "\"}").c_str());
        }
        ASSERT_NO_FATAL_FAILURE(PassTheWindow());

        ASSERT_TRUE(IsListed(Actions(), Parse("{\"type\": \"win\"}")));
        const Json outcome = Perform("{\"type\": \"win\"}");
        EXPECT_EQ(Str(Member(outcome, "phase")), "hand_over");

        const Json state = State();
        EXPECT_EQ(Str(Member(state, "phase")), "hand_over");
        const Json& result = Member(state, "result");
        EXPECT_EQ(Str(Member(result, "kind")), "win");
        EXPECT_EQ(Member(result, "winner").get<int>(), east);
        EXPECT_EQ(Str(Member(result, "winningTile")), "Rd");
        EXPECT_EQ(Str(Member(result, "source")), "self_draw");
        EXPECT_EQ(Member(result, "from").get<int>(), east);
        const Json& winner = Member(result, "seats")[static_cast<size_t>(east)];
        const Json& breakdown = Member(winner, "breakdown");
        EXPECT_TRUE(Member(breakdown, "won").get<bool>());
        EXPECT_GT(Member(breakdown, "score").get<int>(), 0);
        ASSERT_FALSE(Member(breakdown, "lines").empty());
        EXPECT_EQ(Str(Member(Member(breakdown, "lines")[0], "kind")), "Win");
        int sum = 0;
        for (const Json& delta : Member(result, "deltas")) {
            sum += delta.get<int>();
        }
        EXPECT_EQ(sum, 0) << "the payments are zero-sum";
        EXPECT_GT(Member(result, "deltas")[static_cast<size_t>(east)].get<int>(), 0) << "the winner is paid";

        const Json settled = Perform("{\"type\": \"next_hand\"}");
        EXPECT_EQ(Member(Member(settled, "settlement"), "winner").get<int>(), east);
        EXPECT_EQ(Str(Member(settled, "phase")), "playing");
        const Json next = State();
        EXPECT_EQ(Member(next, "handsPlayed").get<int>(), 1);
    }

    TEST_F(MahjongGameControlTest, AnUpgradedKongIsRobbedThroughTheApi) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0("PPPP"));
        ASSERT_NO_FATAL_FAILURE(Lay(UpgradeModel({kSeat2})));
        ASSERT_EQ(game->GetSession().Actor(), kSeat1);

        const Json state = State();
        EXPECT_EQ(Member(state, "seat").get<int>(), kSeat1);
        EXPECT_TRUE(IsListed(Actions(), Parse("{\"type\": \"upgrade_kong\", \"tile\": \"5p\"}")));
        const Json& seat1 = Member(state, "seats")[1];
        ASSERT_EQ(Member(seat1, "melds").size(), 1u);
        const Json& pung = Member(seat1, "melds")[0];
        EXPECT_EQ(Str(Member(pung, "kind")), "pung");
        EXPECT_FALSE(Member(pung, "concealed").get<bool>());
        EXPECT_TRUE(Member(pung, "fromDiscard").get<bool>());
        EXPECT_EQ(Member(pung, "from").get<int>(), kSeat0);
        EXPECT_EQ(Str(Member(pung, "claimed")), "5p");

        Perform("{\"type\": \"upgrade_kong\", \"tile\": \"5p\"}");

        // Seat 2 has declared a hand that the 5p completes: the window is its, mahjong or pass only.
        const Json window = State();
        EXPECT_EQ(Member(window, "seat").get<int>(), kSeat2);
        EXPECT_EQ(Str(Member(Member(window, "turn"), "phase")), "rob_kong");
        EXPECT_EQ(Str(Member(Member(window, "turn"), "pendingTile")), "5p");
        EXPECT_EQ(Member(Member(window, "turn"), "current").get<int>(), kSeat1);
        EXPECT_TRUE(Member(Member(window, "turn"), "yourTurn").get<bool>());
        const Json actions = Actions();
        ASSERT_EQ(actions.size(), 2u);
        EXPECT_TRUE(IsListed(actions, Parse("{\"type\": \"claim\", \"claim\": \"mahjong\"}")));
        EXPECT_TRUE(IsListed(actions, Parse("{\"type\": \"pass\"}")));

        Perform("{\"type\": \"claim\", \"claim\": \"mahjong\"}");

        const Json over = State();
        const Json& result = Member(over, "result");
        EXPECT_EQ(Str(Member(result, "source")), "robbed_kong");
        EXPECT_EQ(Member(result, "winner").get<int>(), kSeat2);
        EXPECT_EQ(Member(result, "from").get<int>(), kSeat1);
        EXPECT_EQ(Str(Member(result, "winningTile")), "5p");
    }

    TEST_F(MahjongGameControlTest, AnUpgradedKongIsCompletedWhenNobodyRobsIt) {
        ASSERT_NO_FATAL_FAILURE(ResetWithEastAtSeat0("PPPP"));
        ASSERT_NO_FATAL_FAILURE(Lay(UpgradeModel({kSeat2})));

        Perform("{\"type\": \"upgrade_kong\", \"tile\": \"5p\"}");
        Perform("{\"type\": \"pass\"}");

        const Json state = State();
        EXPECT_EQ(Member(state, "seat").get<int>(), kSeat1);
        EXPECT_EQ(Str(Member(Member(state, "turn"), "phase")), "discard");
        const Json& meld = Member(Member(state, "seats")[1], "melds")[0];
        EXPECT_EQ(Str(Member(meld, "kind")), "kong");
        EXPECT_EQ(Strings(Member(meld, "tiles")), (std::vector<std::string>{"5p", "5p", "5p", "5p"}));
    }

}  // namespace BECore::Mahjong::Tests
