#include "HandTestSupport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/CenterPanel.h>
#include <Mahjong/Table/SeatPlates.h>
#include <Mahjong/Table/TableLayout.h>
#include <gtest/gtest.h>

/**
 * The centre panel inside the wall ring (`Table/CenterPanel.h`): the round wind and the number of the hand in it, the tiles left in the wall,
 * and per side of the table the wind letter, the points, the dealer and the turn. Pure logic over a `Match` and its `TableSession`; the
 * shipped words are loaded into a `UIStrings` of the test. The expectations are worked out here from the rules (a hand counts, a round is a
 * quarter of the match), not taken from the function under test.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeed = 31;
        constexpr int kActionLimit = 4000;
        /// The wall of a hand without flowers holds fewer tiles than the whole set.
        constexpr int kTileSetSize = 136;
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        const char* const kRoundWords[2][4] = {{"East", "South", "West", "North"}, {"Восток", "Юг", "Запад", "Север"}};
        const char* const kLetters[2][4] = {{"E", "S", "W", "N"}, {"В", "Ю", "З", "С"}};

        UIStrings& Words() {
            static UIStrings strings;
            [[maybe_unused]] static const bool loaded = [] {
                EXPECT_TRUE(LoadShippedStrings(strings).empty());
                EXPECT_TRUE(strings.SetLanguage("en"_intern));
                return true;
            }();
            return strings;
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        struct Table {
            Match match;
            TableSession session{match};

            void Start(const SeatSetup& seats, uint32_t seed = kSeed, int hands = kFullMatchHands, bool replay = true) {
                MahjongSettings settings;
                settings.handsPerMatch = hands;
                settings.replayDrawnHand = replay;
                ASSERT_TRUE(session.StartMatch(settings, seed, seats).has_value());
            }

            /// Ends the hand in progress by a settlement that nobody played.
            void Settle(ResultKind kind, Seat winner) {
                HandSettlement settlement;
                settlement.kind = kind;
                settlement.winner = winner;
                settlement.east = match.East();
                settlement.roundWind = match.RoundWind();
                ASSERT_TRUE(match.ApplySettlement(settlement).has_value());
            }
        };

        SeatSetup PersonAt(Seat seat) {
            SeatSetup setup = kAllComputers;
            setup[static_cast<size_t>(seat)] = SeatKind::Person;
            return setup;
        }

        int LanguageIndex() {
            return Words().GetLanguage() == "ru"_intern ? 1 : 0;
        }

        /// "East 1" in the current language: the word of the round and the number.
        std::string RoundText(int roundIndex, int number) {
            return std::string(kRoundWords[LanguageIndex()][roundIndex]) + " " + std::to_string(number);
        }

        /// Checks the panel against the match and the session, with `bottom` the seat the table shows at the bottom.
        void ExpectPanel(const Table& table, Seat bottom) {
            const CenterPanelText panel = MakeCenterPanel(Words(), table.session, table.match);
            const HandModel& hand = table.match.Hand();
            EXPECT_EQ(panel.wall, static_cast<int>(hand.GetWall().LiveCount()));
            EXPECT_NE(panel.wallText.find(std::to_string(panel.wall).c_str()), eastl::string::npos) << Std(panel.wallText);
            EXPECT_EQ(panel.wallText.find('{'), eastl::string::npos);
            EXPECT_EQ(panel.round.find('{'), eastl::string::npos);
            const Seat turn = table.session.GetPhase() == SessionPhase::Playing ? hand.CurrentSeat() : kNoSeat;
            int dealers = 0;
            int turns = 0;
            for (int position = 0; position < kSeatCount; ++position) {
                const CenterPanelSeat& side = panel.seats[static_cast<size_t>(position)];
                const Seat seat = (bottom + position) % kSeatCount;
                ASSERT_EQ(side.seat, seat) << "position " << position;
                EXPECT_EQ(Std(side.wind), kLetters[LanguageIndex()][static_cast<int>(hand.SeatWind(seat))]) << "position " << position;
                EXPECT_EQ(side.score, table.match.Scores()[static_cast<size_t>(seat)]) << "position " << position;
                EXPECT_EQ(side.dealer, hand.SeatWind(seat) == Wind::East) << "position " << position;
                EXPECT_EQ(side.turn, seat == turn) << "position " << position;
                dealers += side.dealer ? 1 : 0;
                turns += side.turn ? 1 : 0;
            }
            EXPECT_EQ(dealers, 1);
            EXPECT_EQ(turns, turn == kNoSeat ? 0 : 1);
        }

        /// `check(languageId)` once per language of the shipped table.
        template <class Check>
        void InEachLanguage(Check&& check) {
            ForEachLanguage(Words(), check);
        }

    }  // namespace

    TEST(CenterPanelTest, TheHandNumberCountsInsideTheRoundForAMatchOf16Hands) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        for (int played = 0; played < kFullMatchHands; ++played) {
            ASSERT_EQ(table.match.HandsPlayed(), played);
            EXPECT_EQ(HandNumberInRound(table.match), played % 4 + 1) << "hand " << played;
            InEachLanguage([&](PoolString) { EXPECT_EQ(Std(MakeCenterPanel(Words(), table.session, table.match).round), RoundText(played / 4, played % 4 + 1)) << "hand " << played; });
            ASSERT_NO_FATAL_FAILURE(table.Settle(ResultKind::Win, NextSeat(table.match.East())));
        }
        // Over: the last hand's number stays, and the round wind is the last one
        ASSERT_EQ(table.match.GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(HandNumberInRound(table.match), 4);
        InEachLanguage([&](PoolString) { EXPECT_EQ(Std(MakeCenterPanel(Words(), table.session, table.match).round), RoundText(3, 4)); });
    }

    TEST(CenterPanelTest, TheHandNumberCountsInsideTheRoundForAMatchOf8Hands) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, kSeed, kShortMatchHands));
        for (int played = 0; played < kShortMatchHands; ++played) {
            EXPECT_EQ(HandNumberInRound(table.match), played % 2 + 1) << "hand " << played;
            InEachLanguage([&](PoolString) { EXPECT_EQ(Std(MakeCenterPanel(Words(), table.session, table.match).round), RoundText(played / 2, played % 2 + 1)) << "hand " << played; });
            ASSERT_NO_FATAL_FAILURE(table.Settle(ResultKind::Win, NextSeat(table.match.East())));
        }
        EXPECT_EQ(HandNumberInRound(table.match), 2);
        InEachLanguage([&](PoolString) { EXPECT_EQ(Std(MakeCenterPanel(Words(), table.session, table.match).round), RoundText(3, 2)); });
    }

    TEST(CenterPanelTest, ADrawnHandThatIsReplayedKeepsTheNumberAndAnEastWinMovesIt) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(table.Settle(ResultKind::Drawn, kNoSeat));
        EXPECT_EQ(table.match.HandsPlayed(), 0);
        EXPECT_EQ(HandNumberInRound(table.match), 1);
        ASSERT_NO_FATAL_FAILURE(table.Settle(ResultKind::Win, table.match.East()));
        EXPECT_EQ(HandNumberInRound(table.match), 2) << "an East repeat counts as a hand";
        ASSERT_NO_FATAL_FAILURE(table.Settle(ResultKind::Drawn, kNoSeat));
        EXPECT_EQ(HandNumberInRound(table.match), 2);
        EXPECT_EQ(Std(MakeCenterPanel(Words(), table.session, table.match).round), RoundText(0, 2));

        Table counted;
        ASSERT_NO_FATAL_FAILURE(counted.Start(kAllComputers, kSeed, kFullMatchHands, false));
        ASSERT_NO_FATAL_FAILURE(counted.Settle(ResultKind::Drawn, kNoSeat));
        EXPECT_EQ(HandNumberInRound(counted.match), 2) << "a drawn hand that is not replayed counts";
    }

    TEST(CenterPanelTest, EachSideShowsTheSeatThatSitsThereForEveryBottomSeatAndEveryLanguage) {
        for (Seat person = 0; person < kSeatCount; ++person) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(person)));
            ASSERT_EQ(BottomSeat(table.session), person);
            // After a few hands the winds, the points and the wall differ from the start
            for (int hand = 0; hand < 3; ++hand) {
                InEachLanguage([&](PoolString) { ExpectPanel(table, person); });
                SeatScores deltas{};
                deltas[static_cast<size_t>(hand)] = 30;
                deltas[static_cast<size_t>((hand + 1) % kSeatCount)] = -30;
                HandSettlement settlement;
                settlement.kind = ResultKind::Win;
                settlement.winner = NextSeat(table.match.East());
                settlement.east = table.match.East();
                settlement.roundWind = table.match.RoundWind();
                settlement.deltas = deltas;
                ASSERT_TRUE(table.match.ApplySettlement(settlement).has_value());
            }
            InEachLanguage([&](PoolString) { ExpectPanel(table, person); });
        }
    }

    TEST(CenterPanelTest, TheTurnFollowsTheSeatAndStaysOnTheDiscarderInAClaimWindowAndIsNoneAtHandOver) {
        int claimWindows = 0;
        for (uint32_t seed = 1; seed <= 4; ++seed) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, seed, kShortMatchHands));
            for (int actions = 0; actions < kActionLimit && table.session.GetPhase() == SessionPhase::Playing; ++actions) {
                ASSERT_NO_FATAL_FAILURE(ExpectPanel(table, 0));
                claimWindows += table.match.Hand().GetPhase() == Phase::Claim ? 1 : 0;
                ASSERT_TRUE(table.session.StepComputer().has_value());
            }
            ASSERT_EQ(table.session.GetPhase(), SessionPhase::HandOver);
            ASSERT_NO_FATAL_FAILURE(ExpectPanel(table, 0));
            for (const CenterPanelSeat& side : MakeCenterPanel(Words(), table.session, table.match).seats) {
                EXPECT_FALSE(side.turn);
            }
        }
        EXPECT_GT(claimWindows, 0);
    }

    TEST(CenterPanelTest, TheWallCountIsTheLiveWallAndFallsWithTheDraws) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        int last = MakeCenterPanel(Words(), table.session, table.match).wall;
        EXPECT_EQ(last, static_cast<int>(table.match.Hand().GetWall().LiveCount()));
        for (int actions = 0; actions < kActionLimit && table.session.GetPhase() == SessionPhase::Playing; ++actions) {
            ASSERT_TRUE(table.session.StepComputer().has_value());
            const int now = MakeCenterPanel(Words(), table.session, table.match).wall;
            EXPECT_LE(now, last);
            last = now;
        }
        EXPECT_LT(last, kTileSetSize);
    }

    TEST(CenterPanelTest, HiddenTilesDoNotChangeThePanel) {
        // Two matches with the same East differ in every wall and hand, and not in anything the table shows
        Table first;
        ASSERT_NO_FATAL_FAILURE(first.Start(kAllComputers, kSeed));
        Table other;
        bool found = false;
        for (uint32_t seed = kSeed + 1; seed < kSeed + 200 && !found; ++seed) {
            ASSERT_NO_FATAL_FAILURE(other.Start(kAllComputers, seed));
            found = other.match.East() == first.match.East();
        }
        ASSERT_TRUE(found);
        ASSERT_NE(Snapshot(first.match.Hand()), Snapshot(other.match.Hand()));
        InEachLanguage([&](PoolString) {
            const CenterPanelText a = MakeCenterPanel(Words(), first.session, first.match);
            const CenterPanelText b = MakeCenterPanel(Words(), other.session, other.match);
            EXPECT_EQ(Std(a.round), Std(b.round));
            EXPECT_EQ(Std(a.wallText), Std(b.wallText));
            EXPECT_EQ(a.wall, b.wall);
            for (size_t position = 0; position < kSeatCount; ++position) {
                EXPECT_EQ(a.seats[position].seat, b.seats[position].seat);
                EXPECT_EQ(Std(a.seats[position].wind), Std(b.seats[position].wind));
                EXPECT_EQ(a.seats[position].score, b.seats[position].score);
                EXPECT_EQ(a.seats[position].dealer, b.seats[position].dealer);
                EXPECT_EQ(a.seats[position].turn, b.seats[position].turn);
            }
        });
    }

    TEST(CenterPanelTest, TheDealerIsReadFromTheWindsOfTheHandShownAfterTheLastHand) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, kSeed, kShortMatchHands));
        for (int played = 0; played < kShortMatchHands; ++played) {
            ASSERT_NO_FATAL_FAILURE(table.Settle(ResultKind::Win, NextSeat(table.match.East())));
        }
        ASSERT_EQ(table.match.GetPhase(), MatchPhase::Finished);
        ASSERT_NO_FATAL_FAILURE(ExpectPanel(table, 0));
    }

    TEST(CenterPanelTest, TheWordsOfThePanelAreInBothLanguages) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        const CenterPanelText english = MakeCenterPanel(Words(), table.session, table.match);
        ASSERT_TRUE(Words().SetLanguage("ru"_intern));
        const CenterPanelText russian = MakeCenterPanel(Words(), table.session, table.match);
        ASSERT_TRUE(Words().SetLanguage("en"_intern));
        EXPECT_NE(Std(english.round), Std(russian.round));
        EXPECT_NE(Std(english.wallText), Std(russian.wallText));
        EXPECT_NE(Std(english.seats[0].wind), Std(russian.seats[0].wind));
        for (const CenterPanelSeat& side : english.seats) {
            EXPECT_EQ(Std(side.wind).size(), 1u);
        }
    }

}  // namespace BECore::Mahjong::Tests
