#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <BECore/Control/Json.h>
#include <Mahjong/MahjongStateJson.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <Mahjong/Online/OnlineTable.h>
#include <cctype>
#include <gtest/gtest.h>

/**
 * `OnlineTable` (`Online/OnlineTable.h`): the view and the actions of a room, read into plain data. The views come from the real
 * `MahjongSeatedGame`, so the reader is held against the writer; the hostile texts are mutations of those views. Plain C++: no scene,
 * no `Application`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint64_t kSeed = 11;
        constexpr int kStepLimit = 20000;
        constexpr int kCheckEvery = 9;
        constexpr int kProbedSeeds = 3;
        constexpr int kWrongNumber = 7;
        constexpr const char* kWrongText = "x";

        Json Parse(const char* text) {
            return ParseJson(text).value();
        }

        std::string Dump(const Json& json) {
            const eastl::string text = DumpJson(json);
            return std::string(text.c_str(), text.size());
        }

        MahjongSeatedGame Started(uint64_t seed = kSeed) {
            MahjongSeatedGame game;
            EXPECT_TRUE(game.Start(seed, Parse("{\"hands\": 8, \"level\": 1}")).has_value());
            return game;
        }

        bool PhaseIs(const MahjongSeatedGame& game, SessionPhase phase) {
            return PhaseOfMatch(game.GetMatch()) == phase;
        }

        bool StepOnce(MahjongSeatedGame& game) {
            for (int seat = 0; seat < kSeatCount; ++seat) {
                if (const std::optional<Json> action = game.ChooseComputerAction(seat)) {
                    const auto done = game.PerformSeatAction(seat, *action);
                    EXPECT_TRUE(done.has_value());
                    return done.has_value();
                }
            }
            return false;
        }

        /// Plays until the hand is over; the callback sees the game every `kCheckEvery` steps.
        template <typename TLook>
        void PlayToHandOver(MahjongSeatedGame& game, TLook&& look) {
            for (int step = 0; step < kStepLimit && PhaseIs(game, SessionPhase::Playing); ++step) {
                if (step % kCheckEvery == 0) {
                    look(game);
                }
                ASSERT_TRUE(StepOnce(game));
            }
            ASSERT_TRUE(PhaseIs(game, SessionPhase::HandOver));
        }

        Json ViewJson(const MahjongSeatedGame& game, int seat) {
            return game.GetSeatView(seat).value();
        }

        Json ActionsJson(const MahjongSeatedGame& game, int seat) {
            return game.GetSeatActions(seat).value();
        }

        std::expected<void, OnlineViewError> ReadSeat(const MahjongSeatedGame& game, int seat, OnlineTable& table) {
            return ReadOnlineView(ViewJson(game, seat), ActionsJson(game, seat), table);
        }

        std::string TextOf(const OnlineViewError& error) {
            return std::string(error.text.c_str(), error.text.size());
        }

        // ----- the comparison with the model -----

        void ExpectMeld(const Meld& read, const Meld& want, const std::string& where) {
            EXPECT_EQ(read.kind, want.kind) << where;
            EXPECT_EQ(read.concealed, want.concealed) << where;
            EXPECT_EQ(read.first, want.first) << where;
            if (want.concealed) {
                EXPECT_EQ(read.from, kNoSeat) << where;
                EXPECT_FALSE(read.claimed.IsValid()) << where;
            } else {
                EXPECT_EQ(read.from, want.from) << where;
                EXPECT_EQ(read.fromDiscard, want.fromDiscard) << where;
                EXPECT_EQ(read.claimed, want.claimed) << where;
            }
        }

        void ExpectMelds(const eastl::vector<Meld>& read, const eastl::vector<Meld>& want, const std::string& where) {
            ASSERT_EQ(read.size(), want.size()) << where;
            for (size_t index = 0; index < want.size(); ++index) {
                ExpectMeld(read[index], want[index], where + ".melds[" + std::to_string(index) + "]");
            }
        }

        void ExpectTiles(const eastl::vector<Tile>& read, const eastl::vector<Tile>& want, const std::string& where) {
            ASSERT_EQ(read.size(), want.size()) << where;
            for (size_t index = 0; index < want.size(); ++index) {
                EXPECT_EQ(read[index], want[index]) << where << "[" << index << "]";
            }
        }

        void ExpectResult(const OnlineResult& read, const HandModel& hand) {
            ASSERT_TRUE(hand.Result().has_value());
            const HandResult& want = *hand.Result();
            const HandSettlement settlement = SettleHand(hand);
            EXPECT_EQ(read.kind, want.kind);
            EXPECT_EQ(read.winner, settlement.winner);
            EXPECT_EQ(read.winningTile, want.kind == ResultKind::Win ? want.winningTile : Tile{});
            if (want.kind == ResultKind::Win) {
                EXPECT_EQ(read.source, want.source);
            }
            EXPECT_EQ(read.from, want.from);
            EXPECT_EQ(read.loose, want.loose);
            EXPECT_EQ(read.lastLiveTile, want.lastLiveTile);
            EXPECT_EQ(read.scores, settlement.scores);
            EXPECT_EQ(read.deltas, settlement.deltas);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& tiles = hand.GetHand(seat);
                const OnlineResultSeat& shown = read.seats[static_cast<size_t>(seat)];
                const std::string where = "result.seats[" + std::to_string(seat) + "]";
                ExpectTiles(shown.concealed, tiles.concealed, where + ".concealed");
                ExpectMelds(shown.melds, tiles.melds, where);
                ExpectTiles(shown.bonus, tiles.bonus, where + ".bonus");
            }
        }

        /// Every fact of @p table equals what the adapter was given for @p seat: the view of `MakeTableView` and the public table.
        void ExpectMatchesModel(const OnlineTable& table, const MahjongSeatedGame& game, int seat) {
            SCOPED_TRACE("seat " + std::to_string(seat) + ", version " + std::to_string(game.GetStateVersion()));
            const Match& match = game.GetMatch();
            const HandModel& hand = match.Hand();
            const TableView view = MakeTableView(hand, seat);

            EXPECT_EQ(table.format, kOnlineViewFormat);
            EXPECT_FALSE(table.hintsAllowed);
            EXPECT_EQ(table.phase, PhaseOfMatch(match));
            EXPECT_EQ(table.seat, seat);
            EXPECT_EQ(table.seatWind, view.seatWind);
            EXPECT_EQ(table.roundWind, view.roundWind);
            EXPECT_EQ(table.east, hand.East());
            ExpectTiles(table.hand, view.own.concealed, "hand");
            EXPECT_EQ(table.turn.phase, view.phase);
            EXPECT_EQ(table.turn.current, view.phase == Phase::Finished ? kNoSeat : view.currentSeat);
            EXPECT_EQ(table.turn.pendingTile, view.pendingTile);
            EXPECT_EQ(table.turn.yourTurn, !view.legal.empty());

            for (Seat other = 0; other < kSeatCount; ++other) {
                const OnlineSeat& shown = table.seats[static_cast<size_t>(other)];
                const PublicSeat& open = view.seats[static_cast<size_t>(other)];
                const std::string where = "seats[" + std::to_string(other) + "]";
                EXPECT_EQ(shown.wind, hand.SeatWind(other)) << where;
                EXPECT_EQ(shown.score, match.Scores()[static_cast<size_t>(other)]) << where;
                EXPECT_EQ(shown.concealedCount, static_cast<int>(hand.GetHand(other).concealed.size())) << where;
                ExpectMelds(shown.melds, other == seat ? view.own.melds : open.openMelds, where);
                EXPECT_EQ(shown.concealedKongs, open.concealedKongs) << where;
                ExpectTiles(shown.bonus, open.bonus, where + ".bonus");
                EXPECT_EQ(shown.discardCount, open.discardCount) << where;
                EXPECT_EQ(shown.calling, open.callingDeclared) << where;
            }

            ASSERT_EQ(table.discards.size(), hand.Discards().size());
            for (size_t index = 0; index < hand.Discards().size(); ++index) {
                EXPECT_EQ(table.discards[index].tile, hand.Discards()[index].tile) << "discards[" << index << "]";
                EXPECT_EQ(table.discards[index].seat, hand.Discards()[index].seat) << "discards[" << index << "]";
            }
            EXPECT_EQ(table.wall, view.liveCount);
            EXPECT_EQ(table.handsPlayed, match.HandsPlayed());
            EXPECT_EQ(table.handsTotal, match.HandsTotal());
            EXPECT_EQ(table.version, game.GetStateVersion());

            const Json listed = ActionsJson(game, seat);
            ASSERT_EQ(table.actions.size(), listed.size());
            for (size_t index = 0; index < listed.size(); ++index) {
                const ControlAction& action = table.actions[index];
                EXPECT_EQ(action.nextHand ? NextHandActionToJson() : ActionToJson(action.hand), listed[index]) << "actions[" << index << "]";
            }

            EXPECT_EQ(table.result.has_value(), PhaseOfMatch(match) != SessionPhase::Playing);
            if (table.result) {
                ExpectResult(*table.result, hand);
            }
        }

        void ExpectEverySeatMatches(const MahjongSeatedGame& game) {
            for (int seat = 0; seat < kSeatCount; ++seat) {
                OnlineTable table;
                const auto read = ReadSeat(game, seat, table);
                ASSERT_TRUE(read.has_value()) << TextOf(read.error());
                ExpectMatchesModel(table, game, seat);
            }
        }

        // ----- positions with a pung, a concealed kong and a claim window -----

        /// Seat 3 discarded 4p: seat 0 can chow it, seat 1 pung it, seat 2 has declared a calling hand that waits on it.
        HandModel PriorityWindow() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "3p 5p 1s 4s 7s 1m 4m 7m 2s 8s Sw Ww Gd";
            spec.hands[kSeat1] = "4p 4p 7p 1s 4s 7s 2m 5m 8m Sw Ww Rd 9p";
            spec.hands[kSeat2] = "3p 5p 4s 5s 6s 7m 8m 9m Ew Ew Ew 1m 1m";
            spec.hands[kSeat3] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Rd Wd Nw 9s";
            spec.live = "Wd Gd Nw 4p";
            HandModel model = MakeModel(spec);
            Lap(model, "Wd Gd Nw", {kSeat2});
            EXPECT_EQ(model.CurrentSeat(), kSeat3);
            EXPECT_TRUE(model.Discard(kSeat3, T("4p")).has_value());
            return model;
        }

        HandModel ConcealedKongModel() {
            return MakeModel(SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew"));
        }

        void Perform(MahjongSeatedGame& game, int seat, const char* action) {
            const auto done = game.PerformSeatAction(seat, Parse(action));
            ASSERT_TRUE(done.has_value()) << action << ": " << (done ? "" : done.error().message.c_str());
        }

        /// A finished hand with a meld in two places, a key at every level that a reader reads, and `result`.
        Json RichHandOverView() {
            MahjongSeatedGame game = Started();
            EXPECT_TRUE(PhaseIs(game, SessionPhase::Playing));
            for (int step = 0; step < kStepLimit && PhaseIs(game, SessionPhase::Playing); ++step) {
                if (!StepOnce(game)) {
                    break;
                }
            }
            Json view = ViewJson(game, 0);
            const Json meld = Parse("{\"kind\":\"pung\",\"tiles\":[\"1p\",\"1p\",\"1p\"],\"concealed\":false,\"from\":2,\"fromDiscard\":true,\"claimed\":\"1p\"}");
            view["seats"][1]["melds"].push_back(meld);
            view["result"]["seats"][0]["melds"].push_back(meld);
            view["result"]["seats"][3]["bonus"].push_back("F1");
            view["seats"][2]["bonus"].push_back("T2");
            if (view["discards"].empty()) {
                view["discards"].push_back(Parse("{\"tile\":\"9s\",\"seat\":3}"));
            }
            return view;
        }

        Json ActionsWithEverything() {
            return Parse("[{\"type\":\"discard\",\"tile\":\"1p\",\"declareCalling\":true},{\"type\":\"win\"},{\"type\":\"claim\",\"claim\":\"chow\",\"first\":\"3p\"},"
                         "{\"type\":\"pass\"},{\"type\":\"next_hand\"}]");
        }

        /// The path "seats/2/melds/0/kind" in the text a failure names: "seats[2].melds[0].kind".
        std::string NamedPath(const std::string& path) {
            std::string named;
            size_t start = 0;
            while (start <= path.size()) {
                const size_t end = path.find('/', start);
                const std::string step = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!step.empty() && std::isdigit(static_cast<unsigned char>(step[0]))) {
                    named += "[" + step + "]";
                } else {
                    named += (named.empty() ? "" : ".") + step;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return named;
        }

        Json* At(Json& root, const std::string& path) {
            Json* node = &root;
            size_t start = 0;
            while (start <= path.size() && node != nullptr) {
                const size_t end = path.find('/', start);
                const std::string step = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (node->is_array()) {
                    const size_t index = static_cast<size_t>(std::stoul(step));
                    node = index < node->size() ? &(*node)[index] : nullptr;
                } else if (node->is_object()) {
                    const auto found = node->find(step);
                    node = found == node->end() ? nullptr : &*found;
                } else {
                    return nullptr;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return node;
        }

        /// What the reader makes of a table, as text: the fields a failed read must not have changed.
        std::string Describe(const OnlineTable& table) {
            std::string text = std::to_string(table.format) + "/" + std::to_string(table.version) + "/" + std::to_string(table.seat) + "/" + std::to_string(table.wall) + "/";
            for (const Tile tile : table.hand) {
                text += std::string(ToString(tile)) + ",";
            }
            text += "/" + std::to_string(table.discards.size()) + "/" + std::to_string(table.actions.size()) + "/" + (table.result ? "result" : "none");
            for (const OnlineSeat& seat : table.seats) {
                text += "/" + std::to_string(seat.score) + ":" + std::to_string(seat.melds.size());
            }
            return text;
        }

        /// A bad text is refused with an error that names @p key, of the kind @p kind, and leaves a table that had a good read as it was.
        void ExpectRefused(const Json& view, const Json& actions, const std::string& key, OnlineViewErrorKind kind, const std::string& what) {
            SCOPED_TRACE(what);
            OnlineTable table;
            const Json good = RichHandOverView();
            ASSERT_TRUE(ReadOnlineView(good, ActionsWithEverything(), table).has_value());
            const std::string before = Describe(table);
            const auto read = ReadOnlineView(view, actions, table);
            ASSERT_FALSE(read.has_value());
            EXPECT_EQ(read.error().kind, kind);
            EXPECT_NE(TextOf(read.error()).find(key), std::string::npos) << TextOf(read.error()) << " should name " << key;
            EXPECT_EQ(Describe(table), before) << "a refused view must leave the table as it was";
        }

    }  // namespace

    // ----- the reader against the writer -----

    TEST(MahjongOnlineTableTest, AHandPlayedToTheEndReadsAsTheModelForEverySeatAtEveryProbe) {
        for (int index = 0; index < kProbedSeeds; ++index) {
            MahjongSeatedGame game = Started(kSeed + static_cast<uint64_t>(index));
            PlayToHandOver(game, [](const MahjongSeatedGame& at) { ExpectEverySeatMatches(at); });
            ExpectEverySeatMatches(game);
            ASSERT_TRUE(game.PerformSeatAction(2, Parse("{\"type\":\"next_hand\"}")).has_value());

            OnlineTable table;
            ASSERT_TRUE(ReadSeat(game, 0, table).has_value());
            EXPECT_EQ(table.ready, (eastl::array<bool, kSeatCount>{false, false, true, false}));
            ExpectEverySeatMatches(game);
        }
    }

    TEST(MahjongOnlineTableTest, AFinishedHandHasAResultAndAPlayedOneHasNone) {
        MahjongSeatedGame game = Started();
        OnlineTable table;
        ASSERT_TRUE(ReadSeat(game, 1, table).has_value());
        EXPECT_FALSE(table.result.has_value());
        EXPECT_EQ(table.phase, SessionPhase::Playing);

        PlayToHandOver(game, [](const MahjongSeatedGame&) {});
        ASSERT_TRUE(ReadSeat(game, 1, table).has_value());
        ASSERT_TRUE(table.result.has_value());
        EXPECT_EQ(table.phase, SessionPhase::HandOver);
        ASSERT_EQ(table.actions.size(), 1u);
        EXPECT_TRUE(table.actions[0].nextHand) << "a client sends next_hand like any listed action";
    }

    TEST(MahjongOnlineTableTest, AConcealedKongIsListedForItsOwnerAndOnlyCountedForTheOthers) {
        MahjongSeatedGame game = Started();
        game.GetMatch().Hand() = ConcealedKongModel();
        ExpectEverySeatMatches(game);
        Perform(game, kSeat0, "{\"type\":\"concealed_kong\",\"tile\":\"Ew\"}");

        OnlineTable own;
        ASSERT_TRUE(ReadSeat(game, kSeat0, own).has_value());
        ASSERT_EQ(own.seats[0].melds.size(), 1u);
        EXPECT_TRUE(own.seats[0].melds[0].concealed);
        EXPECT_EQ(own.seats[0].melds[0].kind, MeldKind::Kong);
        EXPECT_EQ(own.seats[0].melds[0].first, T("Ew"));
        EXPECT_EQ(own.seats[0].concealedKongs, 1);

        OnlineTable other;
        ASSERT_TRUE(ReadSeat(game, kSeat1, other).has_value());
        EXPECT_TRUE(other.seats[0].melds.empty());
        EXPECT_EQ(other.seats[0].concealedKongs, 1);
        ExpectEverySeatMatches(game);
    }

    TEST(MahjongOnlineTableTest, AClaimWindowAndTheOpenPungAfterItReadAsTheModel) {
        MahjongSeatedGame game = Started();
        game.GetMatch().Hand() = PriorityWindow();
        ASSERT_EQ(game.GetMatch().Hand().GetPhase(), Phase::Claim);
        ExpectEverySeatMatches(game);

        OnlineTable window;
        ASSERT_TRUE(ReadSeat(game, kSeat1, window).has_value());
        EXPECT_EQ(window.turn.phase, Phase::Claim);
        EXPECT_EQ(window.turn.pendingTile, T("4p"));
        EXPECT_EQ(window.turn.current, kSeat3);
        EXPECT_FALSE(window.actions.empty());

        Perform(game, kSeat0, "{\"type\":\"pass\"}");
        Perform(game, kSeat2, "{\"type\":\"pass\"}");
        Perform(game, kSeat1, "{\"type\":\"claim\",\"claim\":\"pung\"}");
        ExpectEverySeatMatches(game);

        OnlineTable after;
        ASSERT_TRUE(ReadSeat(game, kSeat0, after).has_value());
        ASSERT_EQ(after.seats[1].melds.size(), 1u);
        EXPECT_EQ(after.seats[1].melds[0].kind, MeldKind::Pung);
        EXPECT_FALSE(after.seats[1].melds[0].concealed);
        EXPECT_EQ(after.seats[1].melds[0].from, kSeat3);
        EXPECT_TRUE(after.seats[1].melds[0].fromDiscard);
        EXPECT_EQ(after.seats[1].melds[0].claimed, T("4p"));
    }

    TEST(MahjongOnlineTableTest, ActionsKeepTheOrderOfTheServer) {
        OnlineTable table;
        ASSERT_TRUE(ReadOnlineView(RichHandOverView(), ActionsWithEverything(), table).has_value());
        ASSERT_EQ(table.actions.size(), 5u);
        EXPECT_EQ(table.actions[0].hand, HandAction::MakeDiscard(T("1p"), true));
        EXPECT_EQ(table.actions[1].hand, HandAction::MakeDeclareWin());
        EXPECT_EQ(table.actions[2].hand, HandAction::MakeClaim(ClaimKind::Chow, T("3p")));
        EXPECT_EQ(table.actions[3].hand, HandAction::MakePass());
        EXPECT_TRUE(table.actions[4].nextHand);
    }

    TEST(MahjongOnlineTableTest, UnknownKeysAreIgnoredSoThatANewerServerStillReads) {
        Json view = RichHandOverView();
        view["extra"] = 1;
        view["turn"]["extra"] = Json::array();
        view["seats"][0]["extra"] = "x";
        view["seats"][1]["melds"][0]["extra"] = true;
        view["result"]["extra"] = 1;
        view["result"]["seats"][0]["breakdown"] = Json::object();
        OnlineTable table;
        const auto read = ReadOnlineView(view, ActionsWithEverything(), table);
        EXPECT_TRUE(read.has_value()) << (read ? "" : TextOf(read.error()));
    }

    TEST(MahjongOnlineTableTest, TheRoomsHintsSettingIsReadAsTheHostChoseIt) {
        for (const bool allowed : {true, false}) {
            MahjongSeatedGame game;
            ASSERT_TRUE(game.Start(kSeed, Parse(allowed ? "{\"hands\": 8, \"level\": 1, \"hintsAllowed\": true}" : "{\"hands\": 8, \"level\": 1, \"hintsAllowed\": false}")).has_value());
            for (int seat = 0; seat < kSeatCount; ++seat) {
                OnlineTable table;
                ASSERT_TRUE(ReadSeat(game, seat, table).has_value());
                EXPECT_EQ(table.hintsAllowed, allowed) << "seat " << seat;
            }
        }
    }

    TEST(MahjongOnlineTableTest, ASettingThatIsMissingOrMalformedReadsAsNotAllowedAndTheTableIsRead) {
        MahjongSeatedGame game;
        ASSERT_TRUE(game.Start(kSeed, Parse("{\"hands\": 8, \"level\": 1, \"hintsAllowed\": true}")).has_value());
        const Json actions = ActionsJson(game, 0);
        OnlineTable table;
        OnlineTable scratch;
        ASSERT_TRUE(ReadOnlineView(ViewJson(game, 0), actions, table, scratch).has_value());
        ASSERT_TRUE(table.hintsAllowed);

        // each of these is read into the pair that held `true`: nothing of the earlier read survives
        Json missing = ViewJson(game, 0);
        missing.erase("hintsAllowed");
        std::vector<Json> views{missing};
        for (const Json& wrong : {Json("true"), Json(1), Json(0), Json(nullptr), Json::array(), Json::object()}) {
            Json view = ViewJson(game, 0);
            view["hintsAllowed"] = wrong;
            views.push_back(view);
        }
        for (const Json& view : views) {
            ASSERT_TRUE(ReadOnlineView(ViewJson(game, 0), actions, table, scratch).has_value());
            ASSERT_TRUE(table.hintsAllowed);
            const auto read = ReadOnlineView(view, actions, table, scratch);
            ASSERT_TRUE(read.has_value()) << (read ? "" : TextOf(read.error())) << " " << Dump(view["hintsAllowed"]);
            EXPECT_FALSE(table.hintsAllowed) << Dump(view);
            EXPECT_EQ(table.seat, 0);
            EXPECT_EQ(table.hand.size(), ViewJson(game, 0)["hand"].size());
        }
    }

    TEST(MahjongOnlineTableTest, TheHandActionsOfATableAreTheServersListWithoutTheReadyCheckAndKeepTheCapacity) {
        OnlineTable table;
        ASSERT_TRUE(ReadOnlineView(RichHandOverView(), ActionsWithEverything(), table).has_value());
        eastl::vector<HandAction> list;
        ListHandActions(table, list);
        ASSERT_EQ(list.size(), 4u);
        EXPECT_EQ(list[0], HandAction::MakeDiscard(T("1p"), true));
        EXPECT_EQ(list[1], HandAction::MakeDeclareWin());
        EXPECT_EQ(list[2], HandAction::MakeClaim(ClaimKind::Chow, T("3p")));
        EXPECT_EQ(list[3], HandAction::MakePass());

        const size_t capacity = list.capacity();
        table.actions.clear();
        ListHandActions(table, list);
        EXPECT_TRUE(list.empty()) << "an earlier list is not kept";
        EXPECT_EQ(list.capacity(), capacity);
        ASSERT_TRUE(ReadOnlineView(RichHandOverView(), ActionsWithEverything(), table).has_value());
        ListHandActions(table, list);
        EXPECT_EQ(list.size(), 4u);
        EXPECT_EQ(list.capacity(), capacity);
    }

    TEST(MahjongOnlineTableTest, ReadingAgainKeepsTheCapacityOfTheLists) {
        MahjongSeatedGame game = Started();
        OnlineTable table;
        OnlineTable scratch;
        const auto readOnce = [&] { ASSERT_TRUE(ReadOnlineView(ViewJson(game, 0), ActionsJson(game, 0), table, scratch).has_value()); };
        PlayToHandOver(game, [](const MahjongSeatedGame&) {});
        readOnce();
        readOnce();
        const size_t handCapacity = table.hand.capacity();
        const size_t discardCapacity = table.discards.capacity();
        const size_t actionCapacity = table.actions.capacity();
        ASSERT_GT(discardCapacity, 0u);
        for (int index = 0; index < 3; ++index) {
            readOnce();
            EXPECT_EQ(table.hand.capacity(), handCapacity);
            EXPECT_EQ(table.discards.capacity(), discardCapacity);
            EXPECT_EQ(table.actions.capacity(), actionCapacity);
        }

        // a smaller view read into the same pair does not give the capacity back
        MahjongSeatedGame fresh = Started();
        ASSERT_TRUE(ReadOnlineView(ViewJson(fresh, 0), ActionsJson(fresh, 0), table, scratch).has_value());
        EXPECT_GE(table.discards.capacity(), discardCapacity);
        EXPECT_GE(table.hand.capacity(), handCapacity);
        EXPECT_TRUE(table.discards.empty());
    }

    // ----- hostile and broken input -----

    TEST(MahjongOnlineTableTest, AFormatThatIsMissingOrNotOursIsAFormatErrorNamingTheKey) {
        const Json actions = ActionsWithEverything();
        Json missing = RichHandOverView();
        missing.erase("format");
        ExpectRefused(missing, actions, "format", OnlineViewErrorKind::Format, "missing");
        for (const Json& wrong : {Json(0), Json(kOnlineViewFormat + 1), Json("1"), Json(1.5), Json(nullptr), Json::array()}) {
            Json view = RichHandOverView();
            view["format"] = wrong;
            ExpectRefused(view, actions, "format", OnlineViewErrorKind::Format, "format " + Dump(wrong));
        }
        ExpectRefused(Json::object(), actions, "format", OnlineViewErrorKind::Format, "the view before the start");
    }

    TEST(MahjongOnlineTableTest, AViewThatIsNotAnObjectAndActionsThatAreNotAListAreRefused) {
        for (const Json& wrong : {Json(5), Json("x"), Json::array(), Json(nullptr)}) {
            ExpectRefused(wrong, ActionsWithEverything(), "view", OnlineViewErrorKind::Malformed, "view " + Dump(wrong));
        }
        for (const Json& wrong : {Json::object(), Json("x"), Json(nullptr), Json(3)}) {
            ExpectRefused(RichHandOverView(), wrong, "actions", OnlineViewErrorKind::Malformed, "actions " + Dump(wrong));
        }
    }

    TEST(MahjongOnlineTableTest, AnActionThatDoesNotParseNamesItsIndex) {
        Json actions = ActionsWithEverything();
        actions[2] = Parse("{\"type\":\"claim\",\"claim\":\"chow\"}");
        ExpectRefused(RichHandOverView(), actions, "actions[2]", OnlineViewErrorKind::Malformed, "a chow with no first tile");
        actions = ActionsWithEverything();
        actions[0] = Parse("{\"type\":\"dance\"}");
        ExpectRefused(RichHandOverView(), actions, "actions[0]", OnlineViewErrorKind::Malformed, "an unknown type");
        actions = ActionsWithEverything();
        actions[1] = 5;
        ExpectRefused(RichHandOverView(), actions, "actions[1]", OnlineViewErrorKind::Malformed, "not an object");
    }

    TEST(MahjongOnlineTableTest, EveryKeyOfTheSchemaIsRequiredAndOfItsTypeAtEveryLevel) {
        const char* const kPaths[] = {"phase",
                                      "seat",
                                      "seatWind",
                                      "roundWind",
                                      "east",
                                      "hand",
                                      "turn",
                                      "turn/phase",
                                      "turn/current",
                                      "turn/pendingTile",
                                      "turn/yourTurn",
                                      "seats",
                                      "seats/0/wind",
                                      "seats/1/score",
                                      "seats/2/concealedCount",
                                      "seats/3/melds",
                                      "seats/1/melds/0",
                                      "seats/1/melds/0/kind",
                                      "seats/1/melds/0/tiles",
                                      "seats/1/melds/0/tiles/1",
                                      "seats/1/melds/0/concealed",
                                      "seats/1/melds/0/from",
                                      "seats/1/melds/0/fromDiscard",
                                      "seats/1/melds/0/claimed",
                                      "seats/0/concealedKongs",
                                      "seats/2/bonus",
                                      "seats/2/bonus/0",
                                      "seats/3/discardCount",
                                      "seats/0/calling",
                                      "discards",
                                      "discards/0",
                                      "discards/0/tile",
                                      "discards/0/seat",
                                      "wall",
                                      "handsPlayed",
                                      "handsTotal",
                                      "version",
                                      "ready",
                                      "ready/2",
                                      "result",
                                      "result/kind",
                                      "result/winner",
                                      "result/winningTile",
                                      "result/source",
                                      "result/from",
                                      "result/loose",
                                      "result/lastLiveTile",
                                      "result/seats",
                                      "result/seats/0",
                                      "result/seats/0/concealed",
                                      "result/seats/1/melds",
                                      "result/seats/0/melds/0",
                                      "result/seats/0/melds/0/kind",
                                      "result/seats/0/melds/0/tiles",
                                      "result/seats/0/melds/0/concealed",
                                      "result/seats/0/melds/0/from",
                                      "result/seats/0/melds/0/fromDiscard",
                                      "result/seats/0/melds/0/claimed",
                                      "result/seats/3/bonus",
                                      "result/seats/3/bonus/0",
                                      "result/scores",
                                      "result/scores/1",
                                      "result/deltas",
                                      "result/deltas/3"};
        const Json good = RichHandOverView();
        for (const char* path : kPaths) {
            const std::string where = path;
            const std::string named = NamedPath(where);
            Json copy = good;
            Json* node = At(copy, where);
            ASSERT_NE(node, nullptr) << where;
            const bool isObject = node->is_object();

            Json wrong = copy;
            *At(wrong, where) = isObject ? Json(kWrongNumber) : Json::object();
            ExpectRefused(wrong, ActionsWithEverything(), named, OnlineViewErrorKind::Malformed, "wrong type at " + where);

            const size_t cut = where.find_last_of('/');
            const std::string parentPath = cut == std::string::npos ? "" : where.substr(0, cut);
            const std::string last = cut == std::string::npos ? where : where.substr(cut + 1);
            Json* parent = parentPath.empty() ? &copy : At(copy, parentPath);
            ASSERT_NE(parent, nullptr) << where;
            if (parent->is_object()) {
                parent->erase(last);
                ExpectRefused(copy, ActionsWithEverything(), named, OnlineViewErrorKind::Malformed, "missing " + where);
            }
        }
    }

    TEST(MahjongOnlineTableTest, ABadTileASeatOutOfRangeAndANegativeCountAreRefusedWithTheKey) {
        const Json actions = ActionsWithEverything();
        const auto refuse = [&](const std::string& path, const Json& value) {
            Json view = RichHandOverView();
            Json* node = At(view, path);
            ASSERT_NE(node, nullptr) << path;
            *node = value;
            ExpectRefused(view, actions, NamedPath(path), OnlineViewErrorKind::Malformed, path + " = " + Dump(value));
        };
        for (const char* tile : {"9z", "", "0p", "10p", "Zz", "1P", "5p "}) {
            refuse("hand/0", tile);
            refuse("seats/2/bonus/0", tile);
            refuse("discards/0/tile", tile);
            refuse("seats/1/melds/0/tiles/2", tile);
            refuse("seats/1/melds/0/claimed", tile);
            refuse("result/seats/1/concealed/0", tile);
            refuse("turn/pendingTile", tile);
        }
        for (const Json& seat : {Json(4), Json(-1), Json(kSeatCount * 2), Json(1.5)}) {
            refuse("seat", seat);
            refuse("east", seat);
            refuse("discards/0/seat", seat);
            refuse("seats/1/melds/0/from", seat);
            refuse("turn/current", seat);
            refuse("result/from", seat);
        }
        refuse("result/winner", -1);
        for (const char* count : {"wall", "handsPlayed", "handsTotal", "version", "seats/0/concealedCount", "seats/1/concealedKongs", "seats/2/discardCount"}) {
            refuse(count, -1);
        }
        refuse("phase", "playing_on");
        refuse("turn/phase", "wait");
        refuse("seats/1/melds/0/kind", "quad");
        refuse("result/kind", "tie");
        refuse("result/source", "wish");
        refuse("seatWind", "Up");
    }

    TEST(MahjongOnlineTableTest, ListsOfTheWrongLengthAreRefusedWithTheKey) {
        const Json actions = ActionsWithEverything();
        const auto refuse = [&](const std::function<void(Json&)>& change, const std::string& key) {
            Json view = RichHandOverView();
            change(view);
            ExpectRefused(view, actions, key, OnlineViewErrorKind::Malformed, key);
        };
        refuse([](Json& view) { view["seats"].erase(view["seats"].begin()); }, "seats");
        refuse([](Json& view) { view["ready"].push_back(true); }, "ready");
        refuse([](Json& view) { view["result"]["seats"].erase(view["result"]["seats"].begin()); }, "result.seats");
        refuse([](Json& view) { view["result"]["scores"].push_back(0); }, "result.scores");
        refuse([](Json& view) { view["seats"][1]["melds"][0]["tiles"].push_back("1p"); }, "seats[1].melds[0].tiles");
        refuse([](Json& view) { view["seats"][1]["melds"][0]["kind"] = "kong"; }, "seats[1].melds[0].tiles");
    }

    TEST(MahjongOnlineTableTest, AFinishedHandWithoutAResultIsRefusedAndAPlayedOneIgnoresIt) {
        Json view = RichHandOverView();
        view.erase("result");
        ExpectRefused(view, ActionsWithEverything(), "result", OnlineViewErrorKind::Malformed, "hand_over without result");

        MahjongSeatedGame game = Started();
        Json playing = ViewJson(game, 0);
        playing["result"] = RichHandOverView()["result"];
        OnlineTable table;
        ASSERT_TRUE(ReadOnlineView(playing, ActionsJson(game, 0), table).has_value());
        EXPECT_FALSE(table.result.has_value()) << "a playing hand shows no result, whatever a server adds";
    }

    TEST(MahjongOnlineTableTest, ARefusedReadLeavesTheScratchUsableForTheNextGoodOne) {
        MahjongSeatedGame game = Started();
        OnlineTable table;
        OnlineTable scratch;
        Json broken = ViewJson(game, 0);
        broken["seats"][2]["melds"] = 5;
        ASSERT_FALSE(ReadOnlineView(broken, ActionsJson(game, 0), table, scratch).has_value());
        EXPECT_EQ(table.version, 0u);
        ASSERT_TRUE(ReadOnlineView(ViewJson(game, 0), ActionsJson(game, 0), table, scratch).has_value());
        ExpectMatchesModel(table, game, 0);
    }

}  // namespace BECore::Mahjong::Tests
