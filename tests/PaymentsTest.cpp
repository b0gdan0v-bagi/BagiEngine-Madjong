#include "HandTestSupport.h"

#include <Mahjong/Game/Payments.h>

/**
 * `Settle`: every loser pays the winner the winner's score, the losers settle pairwise by the difference, East pays and receives
 * double. Pure logic: no engine services. The numbers of the worked payment are those of the plan, worked out by hand.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // The worked payment: South (seat 1) wins with 130; East (seat 0) scored 60, seat 2 30, seat 3 nothing.
        constexpr int kWorkedWinnerScore = 130;
        constexpr int kWorkedEastScore = 60;
        constexpr int kWorkedSeat2Score = 30;
        constexpr int kWorkedSeat3Score = 0;
        constexpr int kWorkedWinnerDelta = 520;
        constexpr int kWorkedEastDelta = -80;
        constexpr int kWorkedSeat2Delta = -160;
        constexpr int kWorkedSeat3Delta = -280;

        // Scores in steps of the rounding, to try every shape of table.
        constexpr int kScoreSteps[] = {0, 10, 30, 130, 500};

        void ExpectDeltas(const SeatScores& actual, const SeatScores& expected) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(actual[seat], expected[seat]) << "seat " << seat;
            }
        }

    }  // namespace

    TEST(PaymentsTest, TheWorkedPayment) {
        const SeatScores scores = {kWorkedEastScore, kWorkedWinnerScore, kWorkedSeat2Score, kWorkedSeat3Score};

        const SeatScores deltas = Settle(scores, kSeat1, kSeat0);

        ExpectDeltas(deltas, {kWorkedEastDelta, kWorkedWinnerDelta, kWorkedSeat2Delta, kWorkedSeat3Delta});
        EXPECT_EQ(Sum(deltas), 0);
    }

    TEST(PaymentsTest, EastAsTheWinnerIsPaidDoubleByEveryLoserAndTheLosersSettleSingle) {
        // East 100: each loser pays 100 x 2. Seat 1 (40) is above seats 2 and 3 (20 each), which pay it 20; seats 2 and 3 are equal.
        const SeatScores deltas = Settle({100, 40, 20, 20}, kSeat0, kSeat0);

        ExpectDeltas(deltas, {600, -160, -220, -220});
    }

    TEST(PaymentsTest, EastAsALoserPaysDoubleToTheWinnerAndInEveryPairItIsIn) {
        // Seat 2 wins with 100; seat 0 (East) 60, seat 1 20, seat 3 30. Seat 0 pays 200, seats 1 and 3 pay 100. East is above both:
        // seat 1 pays it (60 - 20) x 2 = 80 and seat 3 (60 - 30) x 2 = 60. Seat 1 pays seat 3 the single 10: East is not in that pair.
        const SeatScores deltas = Settle({60, 20, 100, 30}, kSeat2, kSeat0);

        ExpectDeltas(deltas, {-200 + 80 + 60, -100 - 80 - 10, 400, -100 - 60 + 10});
    }

    TEST(PaymentsTest, EastThatIsLowerPaysTheDifferenceDoubled) {
        // Seat 1 wins with 50; East (seat 0) has 0, seat 2 30, seat 3 30. East pays 100 and (30 - 0) x 2 = 60 to each of seats 2 and 3.
        const SeatScores deltas = Settle({0, 50, 30, 30}, kSeat1, kSeat0);

        ExpectDeltas(deltas, {-100 - 60 - 60, 100 + 50 + 50, -50 + 60, -50 + 60});
    }

    TEST(PaymentsTest, TheLosersSettleSingleWhenEastIsTheWinner) {
        // East (seat 0) wins with 10; seats 1, 2, 3 have 0, 40, 80: seat 1 pays 40 to seat 2 and 80 to seat 3, seat 2 pays 40 to seat 3.
        const SeatScores deltas = Settle({10, 0, 40, 80}, kSeat0, kSeat0);

        ExpectDeltas(deltas, {60, -20 - 40 - 80, -20 + 40 - 40, -20 + 80 + 40});
    }

    TEST(PaymentsTest, EqualLoserScoresMoveNothingBetweenThem) {
        // Seat 1 wins with 100; East (seat 0) and seats 2 and 3 all have 50: each pays the winner only, East double.
        const SeatScores deltas = Settle({50, 100, 50, 50}, kSeat1, kSeat0);

        ExpectDeltas(deltas, {-200, 400, -100, -100});
    }

    TEST(PaymentsTest, ALoserThatOutscoredTheWinnerStillPaysTheWinner) {
        // Seat 1 wins with 30; seat 2 has 200, seat 0 (East) and seat 3 have 0.
        // Seat 0 pays 30 x 2 = 60 and (200 - 0) x 2 = 400 to seat 2; seat 3 pays 30 and 200 to seat 2.
        const SeatScores deltas = Settle({0, 30, 200, 0}, kSeat1, kSeat0);

        ExpectDeltas(deltas, {-60 - 400, 120, -30 + 400 + 200, -30 - 200});
    }

    TEST(PaymentsTest, ADrawnHandPaysNothing) {
        const SeatScores scores = {kWorkedEastScore, kWorkedWinnerScore, kWorkedSeat2Score, kWorkedSeat3Score};

        ExpectDeltas(Settle(scores, kNoSeat, kSeat0), {0, 0, 0, 0});
    }

    TEST(PaymentsTest, TheChangeSumsToZeroForEveryTableOfScoresWinnerAndEast) {
        for (const int a : kScoreSteps) {
            for (const int b : kScoreSteps) {
                for (const int c : kScoreSteps) {
                    for (const int d : kScoreSteps) {
                        const SeatScores scores = {a, b, c, d};
                        for (Seat winner = 0; winner < kSeatCount; ++winner) {
                            for (Seat east = 0; east < kSeatCount; ++east) {
                                const SeatScores deltas = Settle(scores, winner, east);
                                ASSERT_EQ(Sum(deltas), 0) << a << " " << b << " " << c << " " << d << " winner " << winner << " east " << east;
                                // The winner is never out of pocket: it is paid by every loser, whatever the losers settle between them.
                                EXPECT_GE(deltas[winner], 0);
                            }
                        }
                    }
                }
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
