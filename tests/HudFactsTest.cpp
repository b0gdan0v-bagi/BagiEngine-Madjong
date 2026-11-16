#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/HudFacts.h>
#include <gtest/gtest.h>
#include <tests/HandTestSupport.h>

/// The facts are worked out here from what the HUD shows today (a last move text, the final standings), not taken from the function under test.
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeed = 31;
        constexpr int kActionLimit = 4000;
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        struct Table {
            Match match;
            TableSession session{match};

            void Start(const SeatSetup& seats, uint32_t seed = kSeed, int hands = kShortMatchHands) {
                MahjongSettings settings;
                settings.handsPerMatch = hands;
                ASSERT_TRUE(session.StartMatch(settings, seed, seats).has_value());
            }

            void Settle(Seat winner) {
                HandSettlement settlement;
                settlement.kind = ResultKind::Win;
                settlement.winner = winner;
                settlement.east = match.East();
                settlement.roundWind = match.RoundWind();
                ASSERT_TRUE(match.ApplySettlement(settlement).has_value());
            }
        };

        HudFacts FactsOf(const Table& table, bool roomLineUp = false) {
            return MakeHudFacts(table.match, table.session, roomLineUp);
        }

    }  // namespace

    TEST(HudFactsTest, NothingIsShownBeforeAMatchStarts) {
        Match match;
        TableSession session(match);
        EXPECT_EQ(MakeHudFacts(match, session, false), HudFacts{});
    }

    TEST(HudFactsTest, ARoomLineShowsTheMoveLineAtOnceButNeverBeforeAMatch) {
        Match match;
        TableSession session(match);
        EXPECT_FALSE(MakeHudFacts(match, session, true).hasLastMove);

        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        ASSERT_TRUE(table.match.Hand().Moves().empty());
        EXPECT_FALSE(FactsOf(table).hasLastMove);
        EXPECT_TRUE(FactsOf(table, true).hasLastMove);
    }

    TEST(HudFactsTest, TheMoveLineAppearsWithTheFirstMoveOfAHandAndGoesWhenTheHandIsOver) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        bool sawEmpty = false;
        bool sawMove = false;
        bool sawClaimWindow = false;
        for (int actions = 0; actions < kActionLimit && table.session.GetPhase() == SessionPhase::Playing; ++actions) {
            const HudFacts facts = FactsOf(table);
            EXPECT_EQ(facts.hasLastMove, !table.match.Hand().Moves().empty());
            EXPECT_FALSE(facts.matchOver);
            sawEmpty = sawEmpty || !facts.hasLastMove;
            sawMove = sawMove || facts.hasLastMove;
            sawClaimWindow = sawClaimWindow || (table.match.Hand().GetPhase() == Phase::Claim && facts.hasLastMove);
            ASSERT_TRUE(table.session.StepComputer().has_value());
        }
        ASSERT_EQ(table.session.GetPhase(), SessionPhase::HandOver);
        EXPECT_TRUE(sawEmpty);
        EXPECT_TRUE(sawMove);
        EXPECT_TRUE(sawClaimWindow);
        EXPECT_FALSE(table.match.Hand().Moves().empty());
        EXPECT_EQ(FactsOf(table), HudFacts{});
    }

    TEST(HudFactsTest, ANewHandStartsWithoutAMoveLine) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        while (table.session.GetPhase() == SessionPhase::Playing) {
            ASSERT_TRUE(table.session.StepComputer().has_value());
        }
        ASSERT_TRUE(table.session.NextHand().has_value());
        ASSERT_EQ(table.session.GetPhase(), SessionPhase::Playing);
        EXPECT_FALSE(FactsOf(table).hasLastMove);
    }

    TEST(HudFactsTest, TheMatchIsOverExactlyWhenTheMatchIsFinished) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        for (int played = 0; played < kShortMatchHands; ++played) {
            EXPECT_FALSE(FactsOf(table).matchOver);
            ASSERT_NO_FATAL_FAILURE(table.Settle(NextSeat(table.match.East())));
        }
        ASSERT_EQ(table.match.GetPhase(), MatchPhase::Finished);
        const HudFacts facts = FactsOf(table);
        EXPECT_TRUE(facts.matchOver);
        EXPECT_FALSE(facts.hasLastMove);
    }

    TEST(HudFactsTest, HiddenTilesAndTheWallDoNotChangeTheFacts) {
        // Two matches that differ in every wall and hand, and in nothing the table shows, give the same facts at the same point of play
        Table first;
        ASSERT_NO_FATAL_FAILURE(first.Start(kAllComputers, kSeed));
        Table other;
        ASSERT_NO_FATAL_FAILURE(other.Start(kAllComputers, kSeed + 1));
        ASSERT_NE(Snapshot(first.match.Hand()), Snapshot(other.match.Hand()));
        for (int step = 0; step < 6; ++step) {
            EXPECT_EQ(FactsOf(first), FactsOf(other)) << "after " << step << " moves";
            ASSERT_TRUE(first.session.StepComputer().has_value());
            ASSERT_TRUE(other.session.StepComputer().has_value());
        }
    }

    TEST(HudFactsTest, TheFactsDoNotDependOnWhereThePersonSits) {
        for (Seat person = 0; person < kSeatCount; ++person) {
            SeatSetup seats = kAllComputers;
            seats[static_cast<size_t>(person)] = SeatKind::Person;
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(seats));
            table.session.RunComputers();
            const HudFacts facts = FactsOf(table);
            EXPECT_EQ(facts.hasLastMove, !table.match.Hand().Moves().empty()) << "person " << person;
            EXPECT_FALSE(facts.matchOver);
        }
    }

}  // namespace BECore::Mahjong::Tests
