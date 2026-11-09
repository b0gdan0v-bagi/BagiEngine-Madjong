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
 *
 * Easy (`kEasyProfile`: a discard without the acceptance in 60 percent of the turns, a calling hand not declared in 85 percent of the
 * turns that could, an acceptable claim passed in 35 percent, a value pung at equal shanten not taken) was measured on the same 240
 * hands: the Easy seat won 102 (Normal 186, 55 percent), the three random seats together 36, hands drawn 102 (Normal 27); it declared in
 * 143 hands (random 149 together, 50 per seat); its points +16560 (69 a hand; Normal +31730), the random seats -16560. On another range
 * of 60 seeds (12000..12059) it won 105 (Normal 191) for +21640 (Normal +33380), so the numbers are not tuned to the seeds of the
 * test. The first version of Easy (40 / 50 / 25 percent) won 161: too near Normal; a declaration skipped on 100 percent of the turns is
 * a seat that never wins (the win needs the declaration), so the knobs are sensitive and the percentages were found by measuring.
 * The margins for Easy are again far under the numbers: "clearly better than random, clearly worse than Normal".
 *
 * The Normal run of the 240 hands is played once per process and shared by the two tests.
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
        /// random seat. On every seat the computer wins at least a quarter of its hands there.
        constexpr int kMinWinShareOfHands = 3;  // one hand in three
        constexpr int kMinWinFactor = 4;
        constexpr int kMinDeclaredShareOfHands = 2;  // one hand in two
        constexpr int kMinDeclaredFactor = 2;
        constexpr int kMinPointsPerHand = 60;
        constexpr int kMinWinShareOnSeat = 4;  // one hand in four

        /// The margins of Easy: a fifth of the hands won, at least three times the wins of an average random seat, at least 30 points a
        /// hand. On the same hands it stays under two thirds of Normal's wins and points (the check is in the test).
        constexpr int kEasyMinWinShareOfHands = 5;
        constexpr int kEasyMinWinFactor = 3;
        constexpr int kEasyMinPointsPerHand = 30;

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

        /// Who sits on a seat: a computer player of a level, or the random "seek win" chooser.
        struct SeatPlayer {
            bool computer = false;
            ComputerLevel level = kDefaultLevel;

            static SeatPlayer Random() {
                return {};
            }
            static SeatPlayer Computer(ComputerLevel level) {
                return {true, level};
            }
        };

        /// Plays the hand of `seed` (the wall and East from the seed) with `focus` on `computerSeat` (the seat whose results `out.computer`
        /// holds) and `others` on the three other seats, and adds the result to `out`. A computer seat `i` is seeded `SeatSeed(seed, i)`.
        void PlayOne(uint32_t seed, Seat computerSeat, SeatPlayer focus, SeatPlayer others, Outcome& out) {
            const MahjongSettings settings;
            MahjongRng wallRng(seed);
            const Seat east = static_cast<Seat>(seed % kSeatCount);
            HandModel hand;
            ASSERT_TRUE(hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value());

            const eastl::array<SeatPlayer, kSeatCount> players = [&] {
                eastl::array<SeatPlayer, kSeatCount> seats;
                seats.fill(others);
                seats[computerSeat] = focus;
                return seats;
            }();
            eastl::vector<ComputerPlayer> computers;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                computers.push_back(ComputerPlayer(SeatSeed(seed, seat), players[seat].level));
            }
            MahjongRng randomRng(seed * 13 + 5);
            int steps = 0;
            while (hand.GetPhase() != Phase::Finished) {
                ASSERT_LT(++steps, kStepLimit) << "seed " << seed << ": the hand does not end";
                const Seat actor = ComputerTable::NextActor(hand);
                ASSERT_TRUE(IsSeat(actor));
                HandAction action;
                if (players[actor].computer) {
                    const std::optional<HandAction> chosen = computers[actor].Decide(hand, actor);
                    ASSERT_TRUE(chosen.has_value()) << "seed " << seed << ": the computer seat " << actor << " has no action";
                    action = *chosen;
                } else {
                    action = Choose(hand.LegalActions(actor), Mode::SeekWin, randomRng);
                }
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

        /// The 60 seeds with the computer of `focus` on each seat in turn and `others` on the rest.
        Outcome PlayAllSeeds(SeatPlayer focus, SeatPlayer others = SeatPlayer::Random()) {
            Outcome out;
            for (int index = 0; index < kSeedCount; ++index) {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    PlayOne(kFirstSeed + static_cast<uint32_t>(index), seat, focus, others, out);
                    if (::testing::Test::HasFatalFailure()) {
                        return out;
                    }
                }
            }
            return out;
        }

        /// Normal against three random seats on the 240 hands, played once per process: the first test and the Easy test both compare
        /// with it. (A failed run is kept too; the caller asks `HasFatalFailure` after the call.)
        const Outcome& NormalAgainstRandom() {
            static const Outcome outcome = PlayAllSeeds(SeatPlayer::Computer(ComputerLevel::Normal));
            return outcome;
        }

    }  // namespace

    TEST(ComputerStrengthTest, TheComputerSeatBeatsRandomSeatsOnTheSameHands) {
        const Outcome& out = NormalAgainstRandom();
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

        // The totals of the three random seats are compared against the computer's times three (no integer average).
        // (a) wins: the computer wins clearly more hands than an average random seat.
        EXPECT_GE(out.computer.wins * kMinWinShareOfHands, out.computer.hands) << "computer wins " << out.computer.wins;
        EXPECT_GE(out.computer.wins * kRandomSeats, kMinWinFactor * out.random.wins) << "random seats won " << out.random.wins << " together";

        // (b) the calling hand is declared in more hands than a random seat does.
        EXPECT_GE(out.computer.declared * kMinDeclaredShareOfHands, out.computer.hands) << "computer declared " << out.computer.declared;
        EXPECT_GE(out.computer.declared * kRandomSeats, kMinDeclaredFactor * out.random.declared) << "random seats declared " << out.random.declared << " together";

        // (c) the points of the computer seat over all hands are positive, well above a random seat's.
        EXPECT_GE(out.computer.points, kMinPointsPerHand * out.computer.hands) << "computer points " << out.computer.points;
        EXPECT_GT(out.computer.points * kRandomSeats, out.random.points) << "random seats scored " << out.random.points << " together";

        // The advantage is not a trick of one seat: on every seat the computer wins at least a quarter of its hands.
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_GE(out.winsOnSeat[seat] * kMinWinShareOnSeat, kSeedCount) << "computer on seat " << seat << " won " << out.winsOnSeat[seat];
        }
    }

    TEST(ComputerStrengthTest, EasyBeatsRandomSeatsButLosesToNormalOnTheSameHands) {
        const Outcome easy = PlayAllSeeds(SeatPlayer::Computer(ComputerLevel::Easy));
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        const Outcome& normal = NormalAgainstRandom();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_EQ(easy.computer.hands, kSeedCount * kSeatCount);
        RecordProperty("easyWins", easy.computer.wins);
        RecordProperty("easyRandomWins", easy.random.wins);
        RecordProperty("easyDeclared", easy.computer.declared);
        RecordProperty("easyPoints", easy.computer.points);
        RecordProperty("easyRandomPoints", easy.random.points);
        RecordProperty("easyDrawn", easy.drawn);

        // (a) clearly better than random: a fifth of the hands won, and three times the wins of an average random seat.
        EXPECT_GE(easy.computer.wins * kEasyMinWinShareOfHands, easy.computer.hands) << "easy wins " << easy.computer.wins;
        EXPECT_GE(easy.computer.wins * kRandomSeats, kEasyMinWinFactor * easy.random.wins) << "random seats won " << easy.random.wins << " together";
        // (b) its points are positive, well above a random seat's.
        EXPECT_GE(easy.computer.points, kEasyMinPointsPerHand * easy.computer.hands) << "easy points " << easy.computer.points;
        EXPECT_GT(easy.computer.points * kRandomSeats, easy.random.points) << "random seats scored " << easy.random.points << " together";

        // (c) on the same hands Easy wins at most two thirds of Normal's hands and earns at most two thirds of its points.
        EXPECT_LE(easy.computer.wins * 3, normal.computer.wins * 2) << "easy " << easy.computer.wins << ", normal " << normal.computer.wins;
        EXPECT_LE(easy.computer.points * 3, normal.computer.points * 2) << "easy " << easy.computer.points << ", normal " << normal.computer.points;
        // It also passes more hands to a draw: its declarations come late and its claims are passed.
        EXPECT_GT(easy.drawn, normal.drawn);
    }

    TEST(ComputerStrengthTest, TwentyWholeHandsByFourComputerPlayersEndLegally) {
        // The cost case: the player asks `LegalActions` once per decision (for the view). No timing is
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
