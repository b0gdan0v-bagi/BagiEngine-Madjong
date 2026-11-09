#include <EASTL/algorithm.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>

/**
 * The wall: the break, the loose tiles, live draws, loose draws with refill from the live tail, the dead wall. Pure logic: no engine
 * services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeed = 7;
        constexpr int kSeedsToTry = 30;
        constexpr size_t kFullWall = 136;
        constexpr size_t kFullWallWithBonus = 144;
        constexpr size_t kShortWallSize = 30;

        MahjongSettings Settings(bool flowers) {
            MahjongSettings settings;
            settings.flowers = flowers;
            return settings;
        }

        /// Tiles with distinct ids 0..count-1 (count <= 42) in that order, to see which tile a draw took.
        eastl::vector<Tile> Sequence(size_t count) {
            eastl::vector<Tile> tiles;
            for (size_t i = 0; i < count; ++i) {
                tiles.push_back(Tile{static_cast<uint8_t>(i)});
            }
            return tiles;
        }

        /// Every tile of a wall: the reserve, the live wall, the dead wall (a copy is taken; the wall is not changed).
        std::vector<int> AllTiles(const Wall& wall) {
            std::vector<int> ids;
            for (const Tile tile : wall.LooseTiles()) {
                ids.push_back(tile.Id());
            }
            for (const Tile tile : wall.LiveTiles()) {
                ids.push_back(tile.Id());
            }
            for (const Tile tile : wall.DeadWall()) {
                ids.push_back(tile.Id());
            }
            return ids;
        }

        std::vector<int> Ids(const eastl::vector<Tile>& tiles) {
            std::vector<int> ids;
            for (const Tile tile : tiles) {
                ids.push_back(tile.Id());
            }
            return ids;
        }

    }  // namespace

    static_assert(kDeadWallTiles == 14);
    static_assert(kLooseTiles == 2);
    static_assert(RingSize(kStacksPerSide) == 136);
    static_assert(RingSize(kStacksPerSideWithBonus) == 144);
    static_assert(StacksPerSide(false) == 17);
    static_assert(StacksPerSide(true) == 18);
    static_assert(BreakSide(5) == 0);

    TEST(WallTest, RingHas136Tiles) {
        MahjongRng rng(kSeed);
        const Wall wall = MakeWall(rng, Settings(false), 0);
        EXPECT_EQ(wall.Remaining(), kFullWall);
        EXPECT_EQ(AllTiles(wall).size(), kFullWall);
    }

    TEST(WallTest, RingHas144TilesWithFlowers) {
        MahjongRng rng(kSeed);
        const Wall wall = MakeWall(rng, Settings(true), 0);
        EXPECT_EQ(wall.Remaining(), kFullWallWithBonus);
        EXPECT_EQ(wall.LiveCount(), kFullWallWithBonus - kLooseTiles - kDeadWallTiles);
    }

    TEST(WallTest, RingHoldsTheWholeSet) {
        for (const bool flowers : {false, true}) {
            MahjongRng rng(kSeed);
            const Wall wall = MakeWall(rng, Settings(flowers), 0);
            std::vector<int> ids = AllTiles(wall);
            std::sort(ids.begin(), ids.end());
            std::vector<int> expected = Ids(BuildTileSet(flowers));
            std::sort(expected.begin(), expected.end());
            EXPECT_EQ(ids, expected);
        }
    }

    TEST(WallTest, BreakSideCountsFromEastsOwnWall) {
        // Side 1 is East's own wall: a roll of 5 or 9 (and 1) picks it.
        EXPECT_EQ(BreakSide(5), 0);
        EXPECT_EQ(BreakSide(9), 0);
        EXPECT_EQ(BreakSide(2), 1);
        EXPECT_EQ(BreakSide(6), 1);
        EXPECT_EQ(BreakSide(3), 2);
        EXPECT_EQ(BreakSide(7), 2);
        EXPECT_EQ(BreakSide(4), 3);
        EXPECT_EQ(BreakSide(12), 3);
    }

    TEST(WallTest, BreakIndexInsideEastsSide) {
        // East rolled 5 (his own wall), the owner 6: 11 stacks from the right end, 22 tiles in.
        EXPECT_EQ(BreakIndex(5, 6, kStacksPerSide), 22);
    }

    TEST(WallTest, SidesLieClockwiseInTheRing) {
        // Side s starts at ring position ((4 - s) % 4) * perSide stacks: East's side, then North's, West's and South's.
        // The owner rolls 2 on top of East's roll; the rolls 5, 4, 3, 2 pick sides 0, 3, 2, 1 (BreakSide).
        EXPECT_EQ(BreakIndex(5, 2, kStacksPerSide), (0 * kStacksPerSide + 5 + 2) * kTilesPerStack);
        EXPECT_EQ(BreakIndex(4, 2, kStacksPerSide), (1 * kStacksPerSide + 4 + 2) * kTilesPerStack);
        EXPECT_EQ(BreakIndex(3, 2, kStacksPerSide), (2 * kStacksPerSide + 3 + 2) * kTilesPerStack);
        EXPECT_EQ(BreakIndex(2, 2, kStacksPerSide), (3 * kStacksPerSide + 2 + 2) * kTilesPerStack);
    }

    TEST(WallTest, BreakIndexRunsIntoTheClockwiseNeighbour) {
        // 5 + 12 = 17 stacks is exactly the whole of East's side: the count goes on into the wall of North (side 3, the clockwise
        // neighbour), which starts at stack 17.
        EXPECT_EQ(BreakIndex(5, 12, kStacksPerSide), 34);
        // 9 + 12 = 21 stacks from the right end of East's side: four stacks into North's side.
        EXPECT_EQ(BreakIndex(9, 12, kStacksPerSide), 42);
        // Second side (roll 2, South's wall at stack 51), owner 12: 51 + 14 = 65 stacks, still inside South's side (51..67).
        EXPECT_EQ(BreakIndex(2, 12, kStacksPerSide), 130);
        // Second side, owner 2: 51 + 4 = 55 stacks.
        EXPECT_EQ(BreakIndex(2, 2, kStacksPerSide), 110);
        // Roll 3 (West's wall at stack 34), owner 12: 34 + 15 = 49 stacks; South's side starts at 51, so the count is still in West's.
        EXPECT_EQ(BreakIndex(3, 12, kStacksPerSide), 98);
    }

    TEST(WallTest, BreakIndexRunsOffTheEndOfTheRing) {
        // Roll 6 picks South's side too (stack 51..67): 51 + 6 + 12 = 69 stacks, past the 68 of the ring, so 1 stack.
        EXPECT_EQ(BreakIndex(6, 12, kStacksPerSide), 2);
        // 12 + 12: side 3 (North's, stack 17) + 24 = 41 stacks.
        EXPECT_EQ(BreakIndex(12, 12, kStacksPerSide), 82);
    }

    TEST(WallTest, BreakIndexWithFlowersUsesEighteenStacksPerSide) {
        // Side 1 starts at 3 * 18 = 54 stacks.
        EXPECT_EQ(BreakIndex(2, 2, kStacksPerSideWithBonus), (3 * kStacksPerSideWithBonus + 4) * kTilesPerStack);
        EXPECT_EQ(BreakIndex(5, 12, kStacksPerSideWithBonus), 34);
        // Side 3 starts at 18 stacks: 18 + 24 = 42 stacks.
        EXPECT_EQ(BreakIndex(12, 12, kStacksPerSideWithBonus), 84);
        // Side 1: 54 + 6 + 12 = 72 stacks, wrapped on the 72 of the ring.
        EXPECT_EQ(BreakIndex(6, 12, kStacksPerSideWithBonus), 0);
    }

    TEST(WallTest, BreakIndexIsAlwaysInsideTheRingAndOnAStackStart) {
        constexpr int kLowestDice = 2;
        constexpr int kHighestDice = 12;
        for (const int perSide : {kStacksPerSide, kStacksPerSideWithBonus}) {
            for (int east = kLowestDice; east <= kHighestDice; ++east) {
                for (int owner = kLowestDice; owner <= kHighestDice; ++owner) {
                    const int index = BreakIndex(east, owner, perSide);
                    EXPECT_GE(index, 0);
                    EXPECT_LT(index, RingSize(perSide));
                    EXPECT_EQ(index % kTilesPerStack, 0);
                }
            }
        }
    }

    TEST(WallTest, MakeWallIsTheShuffledSetReadFromTheBreak) {
        // Pins the order of the rolls: the shuffle, East's dice, the owner's dice; then the ring is read from the break index.
        for (const bool flowers : {false, true}) {
            MahjongRng rng(kSeed);
            const Wall wall = MakeWall(rng, Settings(flowers), 0);

            MahjongRng replay(kSeed);
            eastl::vector<Tile> ring = BuildTileSet(flowers);
            replay.Shuffle(ring);
            const int eastDice = replay.Roll2d6();
            const int ownerDice = replay.Roll2d6();
            const int index = BreakIndex(eastDice, ownerDice, StacksPerSide(flowers));
            std::rotate(ring.begin(), ring.begin() + index, ring.end());

            EXPECT_EQ(AllTiles(wall), Ids(ring));
            EXPECT_EQ(wall.GetBreak().eastDice, eastDice);
            EXPECT_EQ(wall.GetBreak().ownerDice, ownerDice);
            EXPECT_EQ(wall.GetBreak().index, index);
        }
    }

    TEST(WallTest, BreakSeatFollowsEast) {
        MahjongRng first(kSeed);
        MahjongRng second(kSeed);
        const Wall atSeatZero = MakeWall(first, Settings(false), 0);
        const Wall atSeatTwo = MakeWall(second, Settings(false), 2);
        // The tiles do not depend on who is East; the broken wall is counted from East's own.
        EXPECT_EQ(AllTiles(atSeatZero), AllTiles(atSeatTwo));
        EXPECT_EQ(atSeatZero.GetBreak().seat, BreakSide(atSeatZero.GetBreak().eastDice));
        EXPECT_EQ(atSeatTwo.GetBreak().seat, (2 + BreakSide(atSeatTwo.GetBreak().eastDice)) % kSeatCount);
    }

    TEST(WallTest, SameSeedSameWall) {
        MahjongRng first(kSeed);
        MahjongRng second(kSeed);
        EXPECT_EQ(AllTiles(MakeWall(first, Settings(false), 0)), AllTiles(MakeWall(second, Settings(false), 0)));
    }

    TEST(WallTest, DifferentSeedsGiveDifferentWalls) {
        MahjongRng reference(kSeed);
        const std::vector<int> expected = AllTiles(MakeWall(reference, Settings(false), 0));
        for (uint32_t seed = kSeed + 1; seed < kSeed + kSeedsToTry; ++seed) {
            MahjongRng rng(seed);
            EXPECT_NE(AllTiles(MakeWall(rng, Settings(false), 0)), expected) << "seed " << seed;
        }
    }

    TEST(WallTest, RightAfterMakeWall) {
        for (const bool flowers : {false, true}) {
            MahjongRng rng(kSeed);
            const Wall wall = MakeWall(rng, Settings(flowers), 0);
            const size_t total = flowers ? kFullWallWithBonus : kFullWall;
            EXPECT_EQ(wall.LooseCount(), kLooseTiles);
            EXPECT_EQ(wall.DeadWallSize(), kDeadWallTiles);
            EXPECT_EQ(wall.LiveCount(), total - kLooseTiles - kDeadWallTiles);
            EXPECT_EQ(wall.Remaining(), total);
        }
    }

    TEST(WallTest, FromOrderSplitsLooseLiveAndDead) {
        const Wall wall = Wall::FromOrder(Sequence(kShortWallSize));
        EXPECT_EQ(Ids(wall.LooseTiles()), (std::vector<int>{0, 1}));
        EXPECT_EQ(wall.LiveCount(), kShortWallSize - kLooseTiles - kDeadWallTiles);
        EXPECT_EQ(wall.LiveTiles().front().Id(), 2);
        EXPECT_EQ(wall.LiveTiles().back().Id(), 15);
        EXPECT_EQ(Ids(wall.DeadWall()), (std::vector<int>{16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29}));
    }

    TEST(WallTest, DrawsComeInOrderAndNeverTouchTheLast14) {
        Wall wall = Wall::FromOrder(Sequence(kShortWallSize));
        std::vector<int> drawn;
        while (const std::optional<Tile> tile = wall.Draw()) {
            drawn.push_back(tile->Id());
        }
        EXPECT_EQ(drawn, (std::vector<int>{2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}));
        EXPECT_EQ(wall.LiveCount(), 0U);
        EXPECT_FALSE(wall.Draw().has_value());
        // The dead wall and the reserve are still whole.
        EXPECT_EQ(wall.DeadWallSize(), kDeadWallTiles);
        EXPECT_EQ(wall.Remaining(), kLooseTiles + kDeadWallTiles);
        EXPECT_FALSE(wall.Draw().has_value());
    }

    TEST(WallTest, LooseDrawReturnsTheReserveFrontAndRefillsFromTheLiveTail) {
        Wall wall = Wall::FromOrder(Sequence(kShortWallSize));
        const std::optional<Tile> first = wall.DrawLoose();
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(first->Id(), 0);
        // The live tail was tile 15 (the one just before the dead wall); it joins the back of the reserve.
        EXPECT_EQ(Ids(wall.LooseTiles()), (std::vector<int>{1, 15}));
        EXPECT_EQ(wall.LiveCount(), kShortWallSize - kLooseTiles - kDeadWallTiles - 1);
        EXPECT_EQ(wall.LiveTiles().back().Id(), 14);

        const std::optional<Tile> second = wall.DrawLoose();
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->Id(), 1);
        EXPECT_EQ(Ids(wall.LooseTiles()), (std::vector<int>{15, 14}));
        EXPECT_EQ(wall.LiveCount(), kShortWallSize - kLooseTiles - kDeadWallTiles - 2);
        EXPECT_EQ(wall.DeadWallSize(), kDeadWallTiles);
    }

    TEST(WallTest, RegularDrawsAndLooseRefillsMeetWithoutOverlap) {
        Wall wall = Wall::FromOrder(Sequence(kShortWallSize));
        // Live is 2..15. Two regular draws take 2, 3; a loose draw moves the tail 15 into the reserve.
        EXPECT_EQ(wall.Draw()->Id(), 2);
        EXPECT_EQ(wall.Draw()->Id(), 3);
        EXPECT_EQ(wall.DrawLoose()->Id(), 0);
        EXPECT_EQ(wall.LiveCount(), kShortWallSize - kLooseTiles - kDeadWallTiles - 2 - 1);
        EXPECT_EQ(wall.Draw()->Id(), 4);
        EXPECT_EQ(wall.LiveTiles().back().Id(), 14);
    }

    TEST(WallTest, LooseDrawsBeyondWhatTheLiveWallCanRefillShrinkTheReserve) {
        // 18 tiles: 2 loose, 2 live (2, 3), 14 dead.
        Wall wall = Wall::FromOrder(Sequence(18));
        ASSERT_EQ(wall.LiveCount(), 2U);

        EXPECT_EQ(wall.DrawLoose()->Id(), 0);  // refill takes 3
        EXPECT_EQ(wall.LiveCount(), 1U);
        EXPECT_EQ(wall.LooseCount(), 2U);
        EXPECT_EQ(wall.DrawLoose()->Id(), 1);  // refill takes 2
        EXPECT_EQ(wall.LiveCount(), 0U);
        EXPECT_EQ(wall.LooseCount(), 2U);
        EXPECT_EQ(Ids(wall.LooseTiles()), (std::vector<int>{3, 2}));

        // The live wall is empty: the reserve is not refilled any more and shrinks.
        EXPECT_EQ(wall.DrawLoose()->Id(), 3);
        EXPECT_EQ(wall.LooseCount(), 1U);
        EXPECT_EQ(wall.DrawLoose()->Id(), 2);
        EXPECT_EQ(wall.LooseCount(), 0U);
        EXPECT_FALSE(wall.DrawLoose().has_value());
        EXPECT_FALSE(wall.Draw().has_value());
        // Through all of it the dead wall stayed whole.
        EXPECT_EQ(wall.DeadWallSize(), kDeadWallTiles);
        EXPECT_EQ(Ids(wall.DeadWall()), (std::vector<int>{4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17}));
        EXPECT_EQ(wall.Remaining(), kDeadWallTiles);
    }

    TEST(WallTest, ShortWallsGiveTheReserveFirstThenTheDeadWall) {
        const Wall five = Wall::FromOrder(Sequence(5));
        EXPECT_EQ(five.LooseCount(), 2U);
        EXPECT_EQ(five.DeadWallSize(), 3U);
        EXPECT_EQ(five.LiveCount(), 0U);

        const Wall one = Wall::FromOrder(Sequence(1));
        EXPECT_EQ(one.LooseCount(), 1U);
        EXPECT_EQ(one.DeadWallSize(), 0U);

        const Wall empty = Wall::FromOrder({});
        EXPECT_EQ(empty.Remaining(), 0U);
        Wall copy = empty;
        EXPECT_FALSE(copy.Draw().has_value());
        EXPECT_FALSE(copy.DrawLoose().has_value());
    }

    TEST(WallTest, DrainingAFullWallLeavesExactlyTheDeadWall) {
        MahjongRng rng(kSeed);
        Wall wall = MakeWall(rng, Settings(false), 0);
        const std::vector<Tile> dead(wall.DeadWall().begin(), wall.DeadWall().end());
        size_t taken = 0;
        while (wall.Draw().has_value()) {
            ++taken;
        }
        EXPECT_EQ(taken, kFullWall - kLooseTiles - kDeadWallTiles);
        while (wall.DrawLoose().has_value()) {
            ++taken;
        }
        EXPECT_EQ(taken, kFullWall - kDeadWallTiles);
        EXPECT_EQ(wall.Remaining(), kDeadWallTiles);
        EXPECT_TRUE(eastl::equal(dead.begin(), dead.end(), wall.DeadWall().begin()));
    }

    TEST(WallTest, FromPartsKeepsTheThreePartsExactlyAsGiven) {
        // Sizes that FromOrder would never choose: one loose tile, three live tiles, two dead ones.
        const eastl::vector<Tile> all = Sequence(6);
        const Wall wall = Wall::FromParts({all[0]}, {all[1], all[2], all[3]}, {all[4], all[5]});
        EXPECT_EQ(wall.LooseCount(), 1U);
        EXPECT_EQ(wall.LiveCount(), 3U);
        EXPECT_EQ(wall.DeadWallSize(), 2U);
        EXPECT_EQ(wall.Remaining(), 6U);
        EXPECT_EQ(wall.LooseTiles(), (eastl::vector<Tile>{all[0]}));
        EXPECT_EQ(wall.LiveTiles(), (eastl::vector<Tile>{all[1], all[2], all[3]}));
        EXPECT_EQ(wall.DeadWall(), (eastl::vector<Tile>{all[4], all[5]}));

        // It draws like any wall: the live front, the loose front with a refill from the live tail.
        Wall draw = wall;
        EXPECT_EQ(draw.Draw(), all[1]);
        EXPECT_EQ(draw.DrawLoose(), all[0]);
        EXPECT_EQ(draw.LooseTiles(), (eastl::vector<Tile>{all[3]}));
        EXPECT_EQ(draw.LiveTiles(), (eastl::vector<Tile>{all[2]}));
    }

    TEST(WallTest, FromPartsOfAWallsPartsIsTheSameWall) {
        MahjongRng rng(kSeed);
        Wall wall = MakeWall(rng, Settings(true), 0);
        ASSERT_TRUE(wall.Draw().has_value());
        ASSERT_TRUE(wall.DrawLoose().has_value());
        Wall rebuilt = Wall::FromParts(wall.LooseTiles(), wall.LiveTiles(), wall.DeadWall());
        EXPECT_EQ(rebuilt.LooseTiles(), wall.LooseTiles());
        EXPECT_EQ(rebuilt.LiveTiles(), wall.LiveTiles());
        EXPECT_EQ(rebuilt.DeadWall(), wall.DeadWall());
        // The same draws follow.
        while (true) {
            const std::optional<Tile> a = wall.Draw();
            const std::optional<Tile> b = rebuilt.Draw();
            ASSERT_EQ(a, b);
            if (!a.has_value()) {
                break;
            }
            ASSERT_EQ(wall.DrawLoose(), rebuilt.DrawLoose());
        }
    }

}  // namespace BECore::Mahjong::Tests
