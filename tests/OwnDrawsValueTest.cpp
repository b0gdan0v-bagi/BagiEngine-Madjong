#include "HandTestSupport.h"

#include <Mahjong/Game/OwnDrawsValue.h>
#include <Mahjong/Game/Score.h>
#include <climits>
#include <cstdlib>

/**
 * The arithmetic under the value of a discard from the own draws: what a win pays and what a standing hand is worth against
 * `Settle`, the chance that nobody else has won, the rounding. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kScores[] = {0, 20, 70, 130, 500};
        constexpr int kHighScore = 90;
        constexpr int kLowScore = 10;

        TypicalScores TypicalOf(int winner, int loser) {
            return {winner, loser};
        }

        /// The change of `seat` when `winner` wins with `winnerScore`, `seat` stands at `standing` and the two others at `loser`.
        int DeltaOf(Seat seat, Seat winner, Seat east, int winnerScore, int standing, int loser) {
            SeatScores scores{};
            for (Seat other = 0; other < kSeatCount; ++other) {
                scores[other] = loser;
            }
            scores[seat] = standing;
            scores[winner] = winnerScore;
            return Settle(scores, winner, east)[seat];
        }

        /// The sum over the three seats that can win of the change of `seat` (not the winner), in points.
        int SumOverWinners(Seat seat, Seat east, int winnerScore, int standing, int loser) {
            int sum = 0;
            for (Seat winner = 0; winner < kSeatCount; ++winner) {
                if (winner != seat) {
                    sum += DeltaOf(seat, winner, east, winnerScore, standing, loser);
                }
            }
            return sum;
        }

    }  // namespace

    TEST(OwnDrawsValueTest, TheRoundingIsHalfUp) {
        EXPECT_EQ(RoundedDiv(0, 3), 0);
        EXPECT_EQ(RoundedDiv(1, 3), 0);
        EXPECT_EQ(RoundedDiv(2, 3), 1);
        EXPECT_EQ(RoundedDiv(1, 2), 1);
        EXPECT_EQ(RoundedDiv(3, 2), 2);
        EXPECT_EQ(RoundedDiv(5, 1), 5);
    }

    TEST(OwnDrawsValueTest, AWinPaysWhatSettleGivesTheWinner) {
        for (const int score : kScores) {
            for (Seat winner = 0; winner < kSeatCount; ++winner) {
                for (Seat east = 0; east < kSeatCount; ++east) {
                    const SeatScores scores = {score, score, score, score};
                    EXPECT_EQ(WinPayoff(score, winner == east), Settle(scores, winner, east)[winner] * kMilliPerPoint) << "score " << score << " winner " << winner << " east " << east;
                }
            }
        }
    }

    TEST(OwnDrawsValueTest, AWinOfEastIsSixTimesTheScoreAndOfAnotherSeatFourTimes) {
        // The factor is 2: East is paid 3 x 2 x s, another seat 2 x s from East and s from each of the two others.
        EXPECT_EQ(WinPayoff(130, true), 6 * 130 * kMilliPerPoint);
        EXPECT_EQ(WinPayoff(130, false), 4 * 130 * kMilliPerPoint);
    }

    TEST(OwnDrawsValueTest, ALimitHandScoreDoesNotOverflow) {
        const int cap = LimitCap(INT_MAX);

        EXPECT_GT(WinPayoff(cap, true), static_cast<int64_t>(cap));
        EXPECT_EQ(WinPayoff(cap, true), 6 * static_cast<int64_t>(cap) * kMilliPerPoint);
        EXPECT_GT(NotWinningValue(cap, true), 0);
        EXPECT_GT(NotWinningValue(cap, false), 0);
    }

    TEST(OwnDrawsValueTest, AStandingHandOfEastIsSettledExactlyAsTheTableDoes) {
        // East pays every winner at x2 and settles with the two losers at x2, so each winner gives the same change and the mean is exact.
        for (const int standing : kScores) {
            const TypicalScores typical = TypicalOf(kHighScore, kLowScore);
            const int sum = SumOverWinners(kSeat0, kSeat0, typical.winner, standing, typical.loser);

            EXPECT_EQ(sum % (kSeatCount - 1), 0) << "standing " << standing;
            EXPECT_EQ(NotWinningValue(standing, true, typical), sum / (kSeatCount - 1) * kMilliPerPoint) << "standing " << standing;
        }
    }

    TEST(OwnDrawsValueTest, AStandingHandOfAnotherSeatIsTheMeanOverTheWinnersOfTheTable) {
        // The mean over the three winners is sum / 3 points = sum x 1000 / 3 milli-points; the value is that rounded half up.
        for (const Seat seat : {kSeat1, kSeat2, kSeat3}) {
            for (const int standing : kScores) {
                const TypicalScores typical = TypicalOf(kHighScore, kLowScore);
                const int sum = SumOverWinners(seat, kSeat0, typical.winner, standing, typical.loser);
                const int64_t mean = sum * kMilliPerPoint;
                const int64_t value = NotWinningValue(standing, false, typical);

                EXPECT_LE(std::abs(value * (kSeatCount - 1) - mean), (kSeatCount - 1) / 2) << "seat " << seat << " standing " << standing;
            }
        }
    }

    TEST(OwnDrawsValueTest, TheWorkedStandingValues) {
        // Winner 90, loser 10, standing 70: points = 2 x (70 - 10) - 90 = 30. East: x2 = 60 points. Another seat: x4/3 = 40 points exactly.
        const TypicalScores typical = TypicalOf(kHighScore, kLowScore);

        EXPECT_EQ(NotWinningValue(70, true, typical), 60 * kMilliPerPoint);
        EXPECT_EQ(NotWinningValue(70, false, typical), 40 * kMilliPerPoint);
        // Standing 10 (= the loser): points = -90, a pure payment to the winner: East -180, another seat -120.
        EXPECT_EQ(NotWinningValue(10, true, typical), -180 * kMilliPerPoint);
        EXPECT_EQ(NotWinningValue(10, false, typical), -120 * kMilliPerPoint);
    }

    TEST(OwnDrawsValueTest, ARepeatingFractionIsRoundedToTheNearestMilliPoint) {
        // Another seat, standing 0, winner 10, loser 0: points = -10, x4/3 = -13.333 points, -13333 milli-points (away from zero at a half).
        EXPECT_EQ(NotWinningValue(0, false, TypicalOf(10, 0)), -13333);
        // Standing 20, winner 20, loser 0: points = 2 x 20 - 20 = 20, x4/3 = 26.667 points, 26667 milli-points.
        EXPECT_EQ(NotWinningValue(20, false, TypicalOf(20, 0)), 26667);
    }

    TEST(OwnDrawsValueTest, ABetterStandingHandIsWorthMore) {
        for (const bool east : {true, false}) {
            int64_t previous = NotWinningValue(0, east);
            for (const int score : {10, 40, 160, 500}) {
                const int64_t value = NotWinningValue(score, east);
                EXPECT_GT(value, previous) << "east " << east << " score " << score;
                previous = value;
            }
        }
    }

    TEST(OwnDrawsValueTest, NobodyHasWonAtTheStartAndTheChanceFallsWithEveryDraw) {
        for (int opponents = 0; opponents <= kMaxDeclaredOpponents; ++opponents) {
            EXPECT_EQ(Alive(0, opponents), kOwnDrawsProbBase) << "opponents " << opponents;
            int64_t previous = Alive(0, opponents);
            for (int draws = 1; draws <= 30; ++draws) {
                const int64_t alive = Alive(draws, opponents);
                EXPECT_LT(alive, previous) << "opponents " << opponents << " draws " << draws;
                EXPECT_GT(alive, 0);
                previous = alive;
            }
        }
    }

    TEST(OwnDrawsValueTest, MoreDeclaredOpponentsMeanLessChanceToStayAlive) {
        for (const int draws : {1, 5, 12}) {
            for (int opponents = 1; opponents <= kMaxDeclaredOpponents; ++opponents) {
                EXPECT_LT(Alive(draws, opponents), Alive(draws, opponents - 1)) << "draws " << draws << " opponents " << opponents;
            }
        }
    }

    TEST(OwnDrawsValueTest, OneRoundSurvivesWithTheComplementOfTheTableChance) {
        for (int opponents = 0; opponents <= kMaxDeclaredOpponents; ++opponents) {
            const int64_t expected = kOwnDrawsProbBase - kOpponentWinPerRoundPermille[opponents] * (kOwnDrawsProbBase / kPermilleBase);
            EXPECT_EQ(Alive(1, opponents), expected) << "opponents " << opponents;
        }
    }

    TEST(OwnDrawsValueTest, ANumberOfOpponentsOutsideTheTableIsClamped) {
        EXPECT_EQ(Alive(4, -1), Alive(4, 0));
        EXPECT_EQ(Alive(4, kMaxDeclaredOpponents + 5), Alive(4, kMaxDeclaredOpponents));
        EXPECT_EQ(Alive(-3, 1), kOwnDrawsProbBase);
    }

}  // namespace BECore::Mahjong::Tests
