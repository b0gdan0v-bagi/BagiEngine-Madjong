#include "HandTestSupport.h"

#include <Mahjong/Analyzer/AnalyzerPosition.h>
#include <Mahjong/Game/ComputerWorld.h>
#include <Mahjong/Game/MatchLog.h>
#include <gtest/gtest.h>

/**
 * The positions of the analyzer (Analyzer/AnalyzerPosition.h): a decision found in a match log of computers (replayed to the point,
 * reduced to the view, numbered by hand and action), and a hand-written position in the tile notation (parsed, judged by
 * `HandModel::Restore`, reduced to the view). Pure logic: no engine services, an in-memory `MatchLog`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using Analyzer::LogPosition;

        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr int kHandLimit = 200;
        constexpr uint32_t kSeedA = 41;
        constexpr uint32_t kSeedB = 77;
        constexpr uint32_t kSeedC = 123;
        constexpr int kFarHand = 99;
        constexpr int kFarAction = 9999;
        constexpr Seat kFarSeat = 7;
        constexpr uint32_t kRngSeed = 3;

        // LogPosition holds a view, a seat, an action and two numbers: never a model.
        void CheckMembers(const LogPosition& position) {
            const auto& [view, seat, logged, hand, actionIndex] = position;
            static_assert(std::is_same_v<std::remove_cvref_t<decltype(view)>, TableView>);
            static_assert(std::is_same_v<std::remove_cvref_t<decltype(seat)>, Seat>);
            static_assert(std::is_same_v<std::remove_cvref_t<decltype(logged)>, HandAction>);
            static_assert(std::is_same_v<std::remove_cvref_t<decltype(hand)>, int>);
            static_assert(std::is_same_v<std::remove_cvref_t<decltype(actionIndex)>, int>);
        }

        MahjongSettings ShortMatch() {
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            return settings;
        }

        /// The log of a match of four computers on `seed`.
        eastl::vector<MatchLogEvent> PlayedLog(uint32_t seed) {
            Match match;
            TableSession session(match);
            MatchLog log;
            session.SetLogSink(&log);
            EXPECT_TRUE(session.StartMatch(ShortMatch(), seed, kAllComputers, kDefaultLevel).has_value());
            for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kHandLimit; ++hands) {
                session.RunComputers();
                if (session.GetPhase() == SessionPhase::HandOver) {
                    EXPECT_TRUE(session.NextHand().has_value());
                }
            }
            return log.Events();
        }

        int HandCount(const eastl::vector<MatchLogEvent>& events) {
            int settlements = 0;
            for (const MatchLogEvent& event : events) {
                settlements += event.kind == MatchLogKind::Settlement ? 1 : 0;
            }
            return settlements;
        }

        int ActionCount(const eastl::vector<MatchLogEvent>& events, int hand) {
            int count = 0;
            int current = 1;
            for (const MatchLogEvent& event : events) {
                if (event.kind == MatchLogKind::Settlement) {
                    ++current;
                } else if (event.kind == MatchLogKind::Action && current == hand) {
                    ++count;
                }
            }
            return count;
        }

        /// Everything a view holds, as text, to compare two views.
        std::string ViewText(const TableView& view) {
            std::string text = "seat " + std::to_string(view.seat) + " phase " + Name(view.phase) + " current " + std::to_string(view.currentSeat) + " pending " +
                               std::string(ToString(view.pendingTile)) + " winds " + Name(view.seatWind) + "/" + Name(view.roundWind) + " own";
            for (const Tile tile : view.own.concealed) {
                text += " " + std::string(ToString(tile));
            }
            for (const Meld& meld : view.own.melds) {
                text += " | meld " + Name(meld.kind) + " " + std::string(ToString(meld.first)) + " " + std::to_string(meld.concealed);
            }
            text += " calling " + std::to_string(view.own.callingDeclared) + "/" + std::to_string(view.own.callingAtDiscardCount) + "/" + std::to_string(view.own.discardCount);
            for (const PublicSeat& seat : view.seats) {
                text += " || open";
                for (const Meld& meld : seat.openMelds) {
                    text += " " + Name(meld.kind) + std::string(ToString(meld.first));
                }
                for (const Tile tile : seat.bonus) {
                    text += " bonus:" + std::string(ToString(tile));
                }
                text += " ck " + std::to_string(seat.concealedKongs) + " disc " + std::to_string(seat.discardCount) + " calling " + std::to_string(seat.callingDeclared);
            }
            text += " visible";
            for (const uint8_t copies : view.visible) {
                text += " " + std::to_string(copies);
            }
            text += " live " + std::to_string(view.liveCount) + " legal";
            for (const HandAction& action : view.legal) {
                text += " " + Name(action.kind) + ":" + std::string(ToString(action.tile)) + std::to_string(action.declareCalling) + Name(action.claim) + std::string(ToString(action.chowFirst));
            }
            return text;
        }

        /// The view before action number `index` of hand number `hand`, by playing the match by hand: no replay function involved.
        std::string ViewPlayedByHand(const eastl::vector<MatchLogEvent>& events, int hand, int index) {
            Match match;
            int current = 1;
            int count = 0;
            for (const MatchLogEvent& event : events) {
                switch (event.kind) {
                    case MatchLogKind::Start:
                        EXPECT_TRUE(match.Start(event.settings, event.seed).has_value());
                        break;
                    case MatchLogKind::Action:
                        if (current == hand && count == index) {
                            return ViewText(MakeTableView(match.Hand(), event.seat));
                        }
                        count += current == hand ? 1 : 0;
                        EXPECT_TRUE(match.Hand().Apply(event.seat, event.action).has_value());
                        break;
                    case MatchLogKind::Settlement:
                        EXPECT_TRUE(match.EndHand().has_value());
                        ++current;
                        break;
                    case MatchLogKind::Hand:
                    case MatchLogKind::End:
                        break;
                }
            }
            ADD_FAILURE() << "no action " << index << " in hand " << hand;
            return {};
        }

        std::string ViewOfLog(const eastl::vector<MatchLogEvent>& events, int hand, int index) {
            const std::expected<LogPosition, eastl::string> position = Analyzer::PositionFromLog(events, hand, index);
            EXPECT_TRUE(position.has_value()) << (position.has_value() ? "" : position.error().c_str());
            return position.has_value() ? ViewText(position->view) : std::string();
        }

        size_t IndexOfKind(const eastl::vector<MatchLogEvent>& events, MatchLogKind kind, size_t from = 0) {
            for (size_t index = from; index < events.size(); ++index) {
                if (events[index].kind == kind) {
                    return index;
                }
            }
            return events.size();
        }

        size_t IndexOfFirstAction(const eastl::vector<MatchLogEvent>& events, size_t from = 0) {
            return IndexOfKind(events, MatchLogKind::Action, from);
        }

        bool Mentions(const eastl::string& text, const char* part) {
            return std::string(text.c_str()).find(part) != std::string::npos;
        }

        /// The view of a hand-written position, or a failure with the error text.
        TableView Parse(const std::string& text) {
            const std::expected<TableView, eastl::string> view = Analyzer::ParsePosition(text);
            EXPECT_TRUE(view.has_value()) << (view.has_value() ? "" : view.error().c_str());
            return view.has_value() ? *view : TableView{};
        }

        eastl::string ParseError(const std::string& text) {
            const std::expected<TableView, eastl::string> view = Analyzer::ParsePosition(text);
            EXPECT_FALSE(view.has_value()) << "the position should have been refused";
            return view.has_value() ? eastl::string() : view.error();
        }

        // East has 14 tiles and can declare the calling hand with a 5p or a 6p.
        constexpr const char* kDiscardPosition = "hand 1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew Ew\n";

        // Seat 0 answers a 5p of seat 3 with a pung or a chow.
        constexpr const char* kClaimPosition = "seat 0\n"
                                               "phase claim\n"
                                               "hand 1p 2p 3p 4p 6p 4s 5s 6s 7m 8m 9m 5p 5p\n"
                                               "discards 3 Ww\n"
                                               "pending 5p from 3\n";

        // Seat 1 (South) has a pung of Rd taken from seat 0; seat 3 has declared.
        constexpr const char* kTablePosition = "# a table with some history\n"
                                               "seat 1\n"
                                               "east 0\n"
                                               "round South\n"
                                               "hand 2p 3p 4p 5s 6s 7s 3m 4m 5m 8m 8m\n"
                                               "meld 1 pung Rd from 0\n"
                                               "discards 0 1p 9s\n"
                                               "discards 2 Ww\n"
                                               "calling 3\n";

    }  // namespace

    TEST(AnalyzerPositionTest, TheViewOfALogPositionIsTheViewOfAMatchPlayedByHandToTheSamePoint) {
        for (const uint32_t seed : {kSeedA, kSeedB, kSeedC}) {
            const eastl::vector<MatchLogEvent> events = PlayedLog(seed);
            ASSERT_GE(HandCount(events), kShortMatchHands);
            for (const int hand : {1, 2, 3}) {
                const int actions = ActionCount(events, hand);
                ASSERT_GT(actions, 2) << "seed " << seed << " hand " << hand;
                for (const int index : {0, 1, actions / 2, actions - 1}) {
                    EXPECT_EQ(ViewOfLog(events, hand, index), ViewPlayedByHand(events, hand, index)) << "seed " << seed << " hand " << hand << " action " << index;
                }
            }
        }
    }

    TEST(AnalyzerPositionTest, ALogPositionNamesTheSeatTheHandTheIndexAndTheLoggedAction) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeedA);
        const std::expected<LogPosition, eastl::string> first = Analyzer::PositionFromLog(events, 1, 0);
        ASSERT_TRUE(first.has_value());
        CheckMembers(*first);
        EXPECT_EQ(first->hand, 1);
        EXPECT_EQ(first->actionIndex, 0);
        EXPECT_EQ(first->seat, events[IndexOfFirstAction(events)].seat);
        EXPECT_EQ(first->view.seat, first->seat);
        EXPECT_TRUE(Has(first->view.legal, first->logged));

        // Hand 2 is what follows the first settlement: its first action is an East draw-and-discard of its own.
        const std::expected<LogPosition, eastl::string> second = Analyzer::PositionFromLog(events, 2, 0);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->hand, 2);
        EXPECT_EQ(second->view.phase, Phase::Discard);
        const size_t afterSettlement = IndexOfFirstAction(events, IndexOfKind(events, MatchLogKind::Settlement));
        EXPECT_EQ(second->logged, events[afterSettlement].action);
        EXPECT_EQ(second->seat, events[afterSettlement].seat);
    }

    TEST(AnalyzerPositionTest, AClaimWindowActionOfASeatThatIsNotFirstToDecideIsFound) {
        bool found = false;
        for (const uint32_t seed : {kSeedA, kSeedB, kSeedC}) {
            const eastl::vector<MatchLogEvent> events = PlayedLog(seed);
            for (int hand = 1; hand <= HandCount(events) && !found; ++hand) {
                const int actions = ActionCount(events, hand);
                for (int index = 1; index < actions && !found; ++index) {
                    const std::expected<LogPosition, eastl::string> position = Analyzer::PositionFromLog(events, hand, index);
                    const std::expected<LogPosition, eastl::string> before = Analyzer::PositionFromLog(events, hand, index - 1);
                    ASSERT_TRUE(position.has_value() && before.has_value());
                    // The window of the previous action is the same one: the earlier seat has decided, this one is next.
                    if (position->view.phase == Phase::Claim && before->view.phase == Phase::Claim && position->seat != before->seat && before->view.pendingTile == position->view.pendingTile) {
                        found = true;
                        EXPECT_TRUE(Has(position->view.legal, position->logged));
                        EXPECT_TRUE(Has(position->view.legal, HandAction::MakePass()));
                        EXPECT_EQ(ViewOfLog(events, hand, index), ViewPlayedByHand(events, hand, index));
                    }
                }
            }
        }
        EXPECT_TRUE(found) << "no claim window with two deciders in the three matches";
    }

    TEST(AnalyzerPositionTest, TheDecisionsOfASeatAreListedInLogOrderAndEqualTheSinglePositions) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeedB);
        for (const int hand : {1, 2}) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const std::expected<eastl::vector<LogPosition>, eastl::string> positions = Analyzer::PositionsOfSeat(events, hand, seat);
                ASSERT_TRUE(positions.has_value()) << positions.error().c_str();
                // The indexes are those of the seat's Action events among the hand's, in order.
                eastl::vector<int> expected;
                int current = 1;
                int index = 0;
                for (const MatchLogEvent& event : events) {
                    if (event.kind == MatchLogKind::Settlement) {
                        ++current;
                    } else if (event.kind == MatchLogKind::Action && current == hand) {
                        if (event.seat == seat) {
                            expected.push_back(index);
                        }
                        ++index;
                    }
                }
                ASSERT_EQ(positions->size(), expected.size());
                for (size_t i = 0; i < expected.size(); ++i) {
                    const LogPosition& position = (*positions)[i];
                    EXPECT_EQ(position.actionIndex, expected[i]);
                    EXPECT_EQ(position.hand, hand);
                    EXPECT_EQ(position.seat, seat);
                    EXPECT_TRUE(Has(position.view.legal, position.logged));
                    EXPECT_EQ(ViewText(position.view), ViewOfLog(events, hand, expected[i])) << "hand " << hand << " seat " << seat << " action " << expected[i];
                }
            }
        }
    }

    TEST(AnalyzerPositionTest, ALogWithoutItsHandAndEndEventsGivesTheSameViewsAndTheSameNumbers) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeedA);
        eastl::vector<MatchLogEvent> bare;
        for (const MatchLogEvent& event : events) {
            if (event.kind != MatchLogKind::Hand && event.kind != MatchLogKind::End) {
                bare.push_back(event);
            }
        }
        ASSERT_LT(bare.size(), events.size());
        for (const int hand : {1, 2, 3}) {
            const int actions = ActionCount(events, hand);
            EXPECT_EQ(ActionCount(bare, hand), actions);
            for (const int index : {0, actions / 2, actions - 1}) {
                EXPECT_EQ(ViewOfLog(bare, hand, index), ViewOfLog(events, hand, index)) << "hand " << hand << " action " << index;
            }
        }
    }

    TEST(AnalyzerPositionTest, APositionOutOfRangeOrOfALogThatDoesNotReplayIsAnErrorThatSaysSo) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeedA);
        EXPECT_TRUE(Mentions(Analyzer::PositionFromLog(events, 0, 0).error(), "hand 0"));
        EXPECT_TRUE(Mentions(Analyzer::PositionFromLog(events, kFarHand, 0).error(), "hand 99"));
        EXPECT_TRUE(Mentions(Analyzer::PositionFromLog(events, 1, -1).error(), "action -1"));
        EXPECT_TRUE(Mentions(Analyzer::PositionFromLog(events, 1, kFarAction).error(), "action 9999"));
        EXPECT_FALSE(Analyzer::PositionFromLog({}, 1, 0).has_value());
        EXPECT_FALSE(Analyzer::PositionsOfSeat(events, 1, kFarSeat).has_value());
        EXPECT_FALSE(Analyzer::PositionsOfSeat(events, kFarHand, kSeat0).has_value());

        // The first action is changed into one the rules refuse: the log does not replay, the error names the event.
        eastl::vector<MatchLogEvent> broken = events;
        broken[IndexOfFirstAction(broken)].action = HandAction::MakeDiscard(T("F4"));
        const std::expected<LogPosition, eastl::string> refused = Analyzer::PositionFromLog(broken, 1, 1);
        ASSERT_FALSE(refused.has_value());
        EXPECT_TRUE(Mentions(refused.error(), "does not replay"));
        EXPECT_TRUE(Mentions(refused.error(), "ActionRefused"));
        EXPECT_FALSE(Analyzer::PositionsOfSeat(broken, 1, kSeat0).has_value());
    }

    TEST(AnalyzerPositionTest, ASeatWithoutADecisionInTheHandIsAnErrorThatNamesIt) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeedA);
        const size_t firstAction = IndexOfFirstAction(events);
        // The log ends after the first action: only one seat has decided.
        const eastl::vector<MatchLogEvent> cut(events.begin(), events.begin() + static_cast<ptrdiff_t>(firstAction + 1));
        const Seat decided = events[firstAction].seat;
        const Seat idle = decided == kSeat0 ? kSeat1 : kSeat0;
        const std::expected<eastl::vector<LogPosition>, eastl::string> none = Analyzer::PositionsOfSeat(cut, 1, idle);
        ASSERT_FALSE(none.has_value());
        EXPECT_TRUE(Mentions(none.error(), "no decision"));
        const std::expected<eastl::vector<LogPosition>, eastl::string> one = Analyzer::PositionsOfSeat(cut, 1, decided);
        ASSERT_TRUE(one.has_value());
        EXPECT_EQ(one->size(), 1U);
    }

    TEST(AnalyzerPositionTest, ADiscardPositionListsTheDiscardsAndTheDeclarations) {
        const TableView view = Parse(kDiscardPosition);
        EXPECT_EQ(view.seat, kSeat0);
        EXPECT_EQ(view.phase, Phase::Discard);
        EXPECT_EQ(view.currentSeat, kSeat0);
        EXPECT_EQ(view.seatWind, Wind::East);
        EXPECT_EQ(view.roundWind, Wind::East);
        EXPECT_EQ(view.own.concealed.size(), 14U);
        EXPECT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("5p"))));
        EXPECT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("5p"), true)));
        EXPECT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("6p"), true)));
        EXPECT_FALSE(Has(view.legal, HandAction::MakeDiscard(T("Ew"), true)));
        EXPECT_FALSE(Has(view.legal, HandAction::MakeDeclareWin()));

        // The unseen tiles are dealt in a fixed order, so the same text is the same view; and the world sampler accepts the view.
        EXPECT_EQ(ViewText(Parse(kDiscardPosition)), ViewText(view));
        MahjongRng rng(kRngSeed);
        EXPECT_TRUE(SampleWorld(view, rng).has_value());
    }

    TEST(AnalyzerPositionTest, AClaimPositionListsThePungAndTheChowsAndAPass) {
        const TableView view = Parse(kClaimPosition);
        EXPECT_EQ(view.phase, Phase::Claim);
        EXPECT_EQ(view.currentSeat, kSeat3);
        EXPECT_EQ(view.pendingTile, T("5p"));
        EXPECT_EQ(view.own.concealed.size(), 13U);
        EXPECT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Pung)));
        EXPECT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Chow, T("3p"))));
        EXPECT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Chow, T("4p"))));
        EXPECT_TRUE(Has(view.legal, HandAction::MakePass()));
        EXPECT_FALSE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Kong)));
        EXPECT_EQ(view.legal.size(), 4U);
        EXPECT_EQ(view.seats[kSeat3].discardCount, 2);
        MahjongRng rng(kRngSeed);
        EXPECT_TRUE(SampleWorld(view, rng).has_value());
    }

    TEST(AnalyzerPositionTest, TheVisibleTilesAndTheWallComeFromTheTextAlone) {
        const TableView view = Parse(kTablePosition);
        EXPECT_EQ(view.seat, kSeat1);
        EXPECT_EQ(view.seatWind, Wind::South);
        EXPECT_EQ(view.roundWind, Wind::South);
        EXPECT_EQ(view.own.melds.size(), 1U);
        EXPECT_TRUE(view.seats[kSeat3].callingDeclared);
        EXPECT_FALSE(view.seats[kSeat0].callingDeclared);
        // The pung was taken from seat 0: its two pile tiles and the claimed one are its three discards.
        EXPECT_EQ(view.seats[kSeat0].discardCount, 3);
        EXPECT_EQ(view.seats[kSeat2].discardCount, 1);

        // Own 11 tiles, the pung of Rd (3), the pile 1p 9s Ww: nothing else is visible.
        int total = 0;
        for (const uint8_t copies : view.visible) {
            total += copies;
        }
        EXPECT_EQ(total, 11 + 3 + 3);
        EXPECT_EQ(view.visible[T("1p").Id()], 1);
        EXPECT_EQ(view.visible[T("9s").Id()], 1);
        EXPECT_EQ(view.visible[T("Ww").Id()], 1);
        EXPECT_EQ(view.visible[T("Rd").Id()], 3);
        EXPECT_EQ(view.visible[T("8m").Id()], 2);
        EXPECT_EQ(view.visible[T("Ew").Id()], 0);

        // 136 tiles less the 17 named and the three hands of 13 leave 80 for the wall: 14 dead, 2 loose, 64 live.
        EXPECT_EQ(view.liveCount, 64);
        EXPECT_EQ(Parse(std::string(kTablePosition) + "wall 66\n").liveCount, 66);
    }

    TEST(AnalyzerPositionTest, AWallThatTheTilesNamedCannotLeaveIsRefusedWithItsLine) {
        const eastl::string error = ParseError(std::string(kTablePosition) + "wall 60\n");
        EXPECT_TRUE(Mentions(error, "line 10")) << error.c_str();
        EXPECT_TRUE(Mentions(error, "64..66")) << error.c_str();
    }

    TEST(AnalyzerPositionTest, ASelfDrawWinNeedsTheDeclarationAndTheDrawnTile) {
        const std::string winning = "hand 1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd\ncalling 0\n";
        EXPECT_TRUE(Has(Parse(winning).legal, HandAction::MakeDeclareWin()));
        EXPECT_TRUE(Has(Parse(winning + "drew Rd\n").legal, HandAction::MakeDeclareWin()));
        EXPECT_FALSE(Has(Parse(winning + "drew none\n").legal, HandAction::MakeDeclareWin())) << "a hand taken by a claim did not draw";
        EXPECT_FALSE(Has(Parse("hand 1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd\n").legal, HandAction::MakeDeclareWin())) << "not declared";
    }

    TEST(AnalyzerPositionTest, BonusTilesMeldsFromAClaimAndADeclaredFirstDiscardAreRead) {
        const TableView view = Parse("seat 2\n"
                                     "east 1\n"
                                     "hand 3p 4p 5p 6s 7s 8s 2m 3m 4m 9m 9m\n"
                                     "meld 2 chow 3s from 1\n"
                                     "discards 2 Nw\n"
                                     "bonus 0 F1 T2\n"
                                     "calling 2 first\n");
        EXPECT_EQ(view.seatWind, Wind::South) << "seat 1 is East, so seat 2 is South";
        EXPECT_EQ(view.seats[kSeat0].bonusCount, 2);
        EXPECT_EQ(view.own.melds.size(), 1U);
        EXPECT_EQ(view.own.callingAtDiscardCount, 0);
        EXPECT_TRUE(view.own.callingDeclared);
        EXPECT_EQ(view.seats[kSeat1].discardCount, 1) << "the chow was taken from its discard";
    }

    TEST(AnalyzerPositionTest, APositionThatCannotExistIsRefusedWithAnErrorThatNamesTheLine) {
        // A comment and a blank line first: the line numbers count them.
        const std::string head = "# a position\n\nseat 0\n";
        {
            const eastl::string error = ParseError(head + "hand 1p 1p 1p 1p 1p 2p 3p 4s 5s 6s 7m 8m 9m 9m\n");
            EXPECT_TRUE(Mentions(error, "line 4")) << error.c_str();
            EXPECT_TRUE(Mentions(error, "1p")) << error.c_str();
        }
        {
            const eastl::string error = ParseError(head + "hand 1p 2p 3p 4s 5s 6s 7m 8m 9m 9m\n");
            EXPECT_TRUE(Mentions(error, "line 4")) << error.c_str();
            EXPECT_TRUE(Mentions(error, "10")) << error.c_str();
        }
        {
            const eastl::string error = ParseError(head + "banana 3\n");
            EXPECT_TRUE(Mentions(error, "line 4")) << error.c_str();
            EXPECT_TRUE(Mentions(error, "banana")) << error.c_str();
        }
        {
            const eastl::string error = ParseError(head + "hand 1p 2x 3p 4s 5s 6s 7m 8m 9m 9m 9s 9s 1m 2m\n");
            EXPECT_TRUE(Mentions(error, "line 4")) << error.c_str();
            EXPECT_TRUE(Mentions(error, "2x")) << error.c_str();
        }
        {
            const eastl::string error = ParseError(head + "seat 1\n");
            EXPECT_TRUE(Mentions(error, "line 4")) << error.c_str();
        }
        EXPECT_TRUE(Mentions(ParseError("seat 0\n"), "hand")) << "no hand line";
        EXPECT_TRUE(Mentions(ParseError(std::string(kDiscardPosition) + "pending 5p from 3\n"), "line 2"));
        EXPECT_TRUE(Mentions(ParseError("phase claim\nhand 1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Rd Rd\n"), "pending"));
        EXPECT_TRUE(Mentions(ParseError("phase robkong\nhand 1p\n"), "line 1"));
        EXPECT_TRUE(Mentions(ParseError(std::string(kDiscardPosition) + "meld 1 chow 3s from 3\n"), "line 2")) << "a chow comes from the seat before";
        EXPECT_TRUE(Mentions(ParseError(std::string(kDiscardPosition) + "drew 9p\n"), "line 2")) << "the drawn tile is not in the hand";
    }

    TEST(AnalyzerPositionTest, AClaimWindowTheSeatCannotAnswerIsRefused) {
        // Seat 0 holds nothing that claims a Ww of seat 3: the window would not have opened for it.
        const eastl::string error = ParseError("phase claim\n"
                                               "hand 1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd\n"
                                               "pending Ww from 3\n");
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(Mentions(error, "claim")) << error.c_str();
    }

}  // namespace BECore::Mahjong::Tests
