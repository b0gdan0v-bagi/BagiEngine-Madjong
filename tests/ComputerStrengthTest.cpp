#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>

/**
 * The computer player against random legal play, on the same hands. One seat is a `ComputerPlayer`, the other three draw like the
 * "seek win" chooser of `HandTestSupport.h` (they win when they can, declare when they can, build sets when they can, otherwise play
 * anything legal). Every seed is played four times on the same wall, the computer on each seat in turn, so the advantage of East and of
 * a good wall cancels. 60 seeds (240 hands, 720 hands of random seats, about 1.5 s in Debug); the seeds are fixed, so the numbers are
 * the same on every run and every platform (only `MahjongRng` draws).
 *
 * Measured with the first version of the player (no tuning was needed), 240 hands: the computer seat won 186 (78 percent), the three
 * random seats together 27 (under 4 percent of their 720 hands, 9 per seat on average), 27 hands were drawn; the computer declared the
 * calling hand in 224 hands, the random seats in 103 (34 per seat on average, 14 percent of their hands); the points of the computer
 * seat over the 240 hands +31730 (132 a hand), the three random seats together -31730 (-44 a hand each). The margins below are far under
 * these numbers on purpose: they say "clearly better than random", and a change of the rules or of a weight should not flip them by a
 * few hands.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kSeedCount = 60;
        constexpr uint32_t kFirstSeed = 4000;
        /// A hand is at most about 4 x 70 actions; far more means a loop.
        constexpr int kStepLimit = 2000;
        constexpr int kRandomSeats = kSeatCount - 1;

        /// The margins. Wins: at least a third of the hands, and at least four times the wins of an average random seat. Declarations:
        /// at least half of the hands, and at least twice an average random seat. Points: at least 60 a hand, and above an average
        /// random seat.
        constexpr int kMinWinShareOfHands = 3;  // one hand in three
        constexpr int kMinWinFactor = 4;
        constexpr int kMinDeclaredShareOfHands = 2;  // one hand in two
        constexpr int kMinDeclaredFactor = 2;
        constexpr int kMinPointsPerHand = 60;

        constexpr int kCostHands = 20;
        constexpr uint32_t kCostFirstSeed = 9100;

        struct Tally {
            int hands = 0;
            int wins = 0;
            int declared = 0;
            int points = 0;
        };

        struct Outcome {
            Tally computer;
            /// The three random seats of every hand together.
            Tally random;
            /// Hands the computer played on seat `i` and won.
            eastl::array<int, kSeatCount> winsOnSeat{};
            int drawn = 0;
        };

        /// Plays the hand of `seed` (the wall and East from the seed) with the computer on `computerSeat` and adds the result to `out`.
        void PlayOne(uint32_t seed, Seat computerSeat, Outcome& out) {
            const MahjongSettings settings;
            MahjongRng wallRng(seed);
            const Seat east = static_cast<Seat>(seed % kSeatCount);
            HandModel hand;
            ASSERT_TRUE(hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value());

            ComputerPlayer computer(SeatSeed(seed, computerSeat));
            MahjongRng randomRng(seed * 13 + 5);
            int steps = 0;
            while (hand.GetPhase() != Phase::Finished) {
                ASSERT_LT(++steps, kStepLimit) << "seed " << seed << ": the hand does not end";
                const Seat actor = ComputerTable::NextActor(hand);
                ASSERT_TRUE(IsSeat(actor));
                const std::optional<HandAction> chosen = actor == computerSeat ? computer.Decide(hand, actor) : std::nullopt;
                const HandAction action = chosen ? *chosen : Choose(hand.LegalActions(actor), Mode::SeekWin, randomRng);
                ASSERT_TRUE(hand.Apply(actor, action).has_value()) << "seed " << seed << " seat " << actor;
            }

            const HandSettlement settlement = SettleHand(hand);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                Tally& tally = seat == computerSeat ? out.computer : out.random;
                ++tally.hands;
                tally.wins += settlement.winner == seat ? 1 : 0;
                tally.declared += hand.GetHand(seat).callingDeclared ? 1 : 0;
                tally.points += settlement.deltas[seat];
            }
            out.winsOnSeat[computerSeat] += settlement.winner == computerSeat ? 1 : 0;
            out.drawn += settlement.kind == ResultKind::Drawn ? 1 : 0;
        }

        Outcome PlayAllSeeds() {
            Outcome out;
            for (int index = 0; index < kSeedCount; ++index) {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    PlayOne(kFirstSeed + static_cast<uint32_t>(index), seat, out);
                    if (::testing::Test::HasFatalFailure()) {
                        return out;
                    }
                }
            }
            return out;
        }

    }  // namespace

    TEST(ComputerStrengthTest, TheComputerSeatBeatsRandomSeatsOnTheSameHands) {
        const Outcome out = PlayAllSeeds();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_EQ(out.computer.hands, kSeedCount * kSeatCount);
        ASSERT_EQ(out.random.hands, out.computer.hands * kRandomSeats);
        RecordProperty("computerWins", out.computer.wins);
        RecordProperty("randomWins", out.random.wins);
        RecordProperty("computerDeclared", out.computer.declared);
        RecordProperty("randomDeclared", out.random.declared);
        RecordProperty("computerPoints", out.computer.points);
        RecordProperty("randomPoints", out.random.points);
        RecordProperty("drawn", out.drawn);

        // (a) wins: the computer wins clearly more hands than an average random seat (the total of the three, divided by three).
        const int averageRandomWins = out.random.wins / kRandomSeats;
        EXPECT_GE(out.computer.wins * kMinWinShareOfHands, out.computer.hands) << "computer wins " << out.computer.wins;
        EXPECT_GE(out.computer.wins, kMinWinFactor * averageRandomWins) << "random seats won " << averageRandomWins << " each on average";

        // (b) the calling hand is declared in more hands than a random seat does.
        const int averageRandomDeclared = out.random.declared / kRandomSeats;
        EXPECT_GE(out.computer.declared * kMinDeclaredShareOfHands, out.computer.hands) << "computer declared " << out.computer.declared;
        EXPECT_GE(out.computer.declared, kMinDeclaredFactor * averageRandomDeclared) << "random seats declared " << averageRandomDeclared;

        // (c) the points of the computer seat over all hands are positive, well above a random seat's.
        const int averageRandomPoints = out.random.points / kRandomSeats;
        EXPECT_GE(out.computer.points, kMinPointsPerHand * out.computer.hands) << "computer points " << out.computer.points;
        EXPECT_GT(out.computer.points, averageRandomPoints) << "random seats scored " << averageRandomPoints << " each on average";

        // The advantage is not a trick of one seat: on every seat the computer wins at least a quarter of its hands.
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_GE(out.winsOnSeat[seat] * kSeatCount, kSeedCount) << "computer on seat " << seat << " won " << out.winsOnSeat[seat];
        }
    }

    TEST(ComputerStrengthTest, TwentyWholeHandsByFourComputerPlayersEndLegally) {
        // The cost case: the player asks `LegalActions` at every decision (once for the view, once in `Decide`'s caller). No timing is
        // asserted; the hands must end within the step limit with every action accepted.
        const MahjongSettings settings;
        for (int index = 0; index < kCostHands; ++index) {
            const uint32_t seed = kCostFirstSeed + static_cast<uint32_t>(index);
            MahjongRng wallRng(seed);
            const Seat east = static_cast<Seat>(seed % kSeatCount);
            HandModel hand;
            ASSERT_TRUE(hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value());
            ComputerTable table(seed);

            int steps = 0;
            while (hand.GetPhase() != Phase::Finished) {
                ASSERT_LT(++steps, kStepLimit) << "seed " << seed << ": the hand does not end";
                const StepResult step = table.Step(hand);
                ASSERT_TRUE(step.has_value()) << "seed " << seed << ": " << Name(step.error());
            }
            EXPECT_TRUE(hand.Result().has_value()) << "seed " << seed;
        }
    }

}  // namespace BECore::Mahjong::Tests
