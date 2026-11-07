#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/Seats.h>
#include <gtest/gtest.h>
#include <set>

/**
 * Seat arithmetic and the draw for East. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kSeedsToTry = 300;

        /// A scripted draw: the totals each call returns, in call order; also records which seats rolled.
        struct ScriptedRolls {
            std::vector<int> totals;
            std::vector<Seat> rolled;
            size_t next = 0;

            SeatRoller AsRoller() {
                return [this](Seat seat) {
                    rolled.push_back(seat);
                    return totals.at(next++);
                };
            }
        };

    }  // namespace

    static_assert(NextSeat(0) == 1);
    static_assert(NextSeat(3) == 0);
    static_assert(Offset(0, 1) == 1);
    static_assert(Offset(3, 0) == 1);
    static_assert(Offset(1, 0) == 3);

    TEST(SeatsTest, NextSeatWraps) {
        EXPECT_EQ(NextSeat(0), 1);
        EXPECT_EQ(NextSeat(1), 2);
        EXPECT_EQ(NextSeat(2), 3);
        EXPECT_EQ(NextSeat(3), 0);
    }

    TEST(SeatsTest, OffsetIsOneToThreeForDifferentSeats) {
        for (Seat from = 0; from < kSeatCount; ++from) {
            EXPECT_EQ(Offset(from, from), 0);
            std::set<int> seen;
            for (Seat to = 0; to < kSeatCount; ++to) {
                if (to == from) {
                    continue;
                }
                const int offset = Offset(from, to);
                EXPECT_GE(offset, 1);
                EXPECT_LE(offset, 3);
                seen.insert(offset);
                // Walking `offset` seats counter-clockwise from `from` arrives at `to`.
                Seat walked = from;
                for (int step = 0; step < offset; ++step) {
                    walked = NextSeat(walked);
                }
                EXPECT_EQ(walked, to);
            }
            EXPECT_EQ(seen.size(), 3u);
        }
    }

    TEST(SeatsTest, MakeSeatWindsFollowCounterClockwiseFromEast) {
        for (Seat east = 0; east < kSeatCount; ++east) {
            const SeatWinds winds = MakeSeatWinds(east);
            EXPECT_EQ(winds[east], Wind::East);
            EXPECT_EQ(winds[NextSeat(east)], Wind::South);
            EXPECT_EQ(winds[NextSeat(NextSeat(east))], Wind::West);
            EXPECT_EQ(winds[NextSeat(NextSeat(NextSeat(east)))], Wind::North);
        }
    }

    TEST(SeatsTest, RollSeatWindsGivesEachWindOnce) {
        std::set<Seat> easts;
        for (uint32_t seed = 0; seed < kSeedsToTry; ++seed) {
            MahjongRng rng(seed);
            const SeatWinds winds = RollSeatWinds(rng);
            std::set<Wind> seen(winds.begin(), winds.end());
            ASSERT_EQ(seen.size(), static_cast<size_t>(kSeatCount)) << "seed " << seed;
            Seat east = -1;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (winds[seat] == Wind::East) {
                    east = seat;
                }
            }
            ASSERT_GE(east, 0);
            EXPECT_EQ(winds, MakeSeatWinds(east)) << "seed " << seed;
            easts.insert(east);
        }
        EXPECT_EQ(easts.size(), static_cast<size_t>(kSeatCount));  // every seat can be East
    }

    TEST(SeatsTest, RollSeatWindsIsReproducible) {
        for (uint32_t seed = 0; seed < 20; ++seed) {
            MahjongRng a(seed);
            MahjongRng b(seed);
            EXPECT_EQ(RollSeatWinds(a), RollSeatWinds(b));
        }
    }

    TEST(SeatsTest, HighestRollIsEast) {
        ScriptedRolls script{{5, 9, 7, 3}, {}, 0};
        EXPECT_EQ(DecideEast(script.AsRoller()), 1);
        EXPECT_EQ(script.rolled, (std::vector<Seat>{0, 1, 2, 3}));
    }

    TEST(SeatsTest, TieAmongTheHighestRollsAgainAmongTheTiedSeatsOnly) {
        // Seats 1 and 3 tie at 10; seat 0 (9) and seat 2 (4) are out. Seat 1 then rolls 6, seat 3 rolls 8: seat 3 is East.
        ScriptedRolls script{{9, 10, 4, 10, 6, 8}, {}, 0};
        EXPECT_EQ(DecideEast(script.AsRoller()), 3);
        EXPECT_EQ(script.rolled, (std::vector<Seat>{0, 1, 2, 3, 1, 3}));
    }

    TEST(SeatsTest, RepeatedTiesRollAgain) {
        // All four tie at 7, then seats 0 and 2 tie at 8 (1 and 3 are out), then seat 0 wins with 11 against 5.
        ScriptedRolls script{{7, 7, 7, 7, 8, 3, 8, 2, 11, 5}, {}, 0};
        EXPECT_EQ(DecideEast(script.AsRoller()), 0);
        EXPECT_EQ(script.rolled, (std::vector<Seat>{0, 1, 2, 3, 0, 1, 2, 3, 0, 2}));
    }

    TEST(SeatsTest, RealDiceTieIsResolvedToo) {
        // Find a seed whose first round has a tie for the highest roll (a real stream, not a script), and check that the draw
        // still ends with exactly one East who is among the tied seats of that round.
        int tiesFound = 0;
        for (uint32_t seed = 0; seed < kSeedsToTry && tiesFound < 3; ++seed) {
            MahjongRng probe(seed);
            std::vector<int> first;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                first.push_back(probe.Roll2d6());
            }
            const int best = *std::max_element(first.begin(), first.end());
            std::set<Seat> tied;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (first[seat] == best) {
                    tied.insert(seat);
                }
            }
            if (tied.size() < 2) {
                continue;
            }
            ++tiesFound;
            MahjongRng rng(seed);
            const SeatWinds winds = RollSeatWinds(rng);
            Seat east = -1;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (winds[seat] == Wind::East) {
                    east = seat;
                }
            }
            EXPECT_TRUE(tied.contains(east)) << "seed " << seed;
        }
        EXPECT_GT(tiesFound, 0);
    }

}  // namespace BECore::Mahjong::Tests
