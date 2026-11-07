#include "FixedWall.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

/**
 * The test helper `FixedWall.h` itself: the layout of the wall, the padding, the cut live wall. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr size_t kFullWall = 136;
        constexpr size_t kFullWallWithBonus = 144;
        constexpr size_t kDealtTotal = static_cast<size_t>(kDealtTiles) * kSeatCount;

        std::vector<int> Ids(const eastl::vector<Tile>& tiles) {
            std::vector<int> ids;
            for (const Tile tile : tiles) {
                ids.push_back(tile.Id());
            }
            return ids;
        }

        std::vector<int> Ids(const std::string& text) {
            return Ids(ParseTiles(text).value());
        }

        FixedWallSpec PlainSpec() {
            FixedWallSpec spec;
            spec.hands[0] = "1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s";
            spec.hands[1] = "1m 2m 3m 4m 5m 6m 7m 8m 9m Ew Sw Ww Nw";
            spec.hands[2] = "1p 2p 3p 4p 5p 6p 7p 8p 9p Rd Wd Gd Ew";
            spec.hands[3] = "5s 6s 7s 8s 9s 1m 2m 3m 4m 5m 6m 7m 8m";
            return spec;
        }

        /// Every tile of the wall (reserve, live, dead) and of the hands, sorted.
        std::vector<int> EveryTile(const Wall& wall, const eastl::array<PlayerHand, kSeatCount>& hands) {
            std::vector<int> ids;
            for (const PlayerHand& hand : hands) {
                for (const Tile tile : hand.concealed) {
                    ids.push_back(tile.Id());
                }
                for (const Tile tile : hand.bonus) {
                    ids.push_back(tile.Id());
                }
            }
            for (const Tile tile : wall.LooseTiles()) {
                ids.push_back(tile.Id());
            }
            for (const Tile tile : wall.LiveTiles()) {
                ids.push_back(tile.Id());
            }
            for (const Tile tile : wall.DeadWall()) {
                ids.push_back(tile.Id());
            }
            std::sort(ids.begin(), ids.end());
            return ids;
        }

    }  // namespace

    TEST(FixedWallTest, LayoutIsLooseThenTheDealThenLiveThenPaddingThenDead) {
        FixedWallSpec spec = PlainSpec();
        spec.live = "Gd Gd 9m";
        spec.loose = "Nw Nw";
        const Wall wall = MakeFixedWall(spec, false);

        EXPECT_EQ(wall.Remaining(), kFullWall);
        EXPECT_EQ(Ids(wall.LooseTiles()), Ids("Nw Nw"));
        EXPECT_EQ(wall.DeadWallSize(), kDeadWallTiles);
        EXPECT_EQ(wall.LiveCount(), kFullWall - kLooseTiles - kDeadWallTiles);

        // The deal takes 52 tiles from the front of the live wall, then `live` follows in order.
        Wall dealing = wall;
        (void)DealHands(dealing, 0, false);
        const eastl::vector<Tile> after = dealing.LiveTiles();
        ASSERT_GE(after.size(), 3U);
        EXPECT_EQ(Ids(eastl::vector<Tile>(after.begin(), after.begin() + 3)), Ids("Gd Gd 9m"));
    }

    TEST(FixedWallTest, TheDealGivesEachSeatItsHandForEveryEast) {
        const FixedWallSpec spec = PlainSpec();
        for (const Seat east : {0, 1, 2, 3}) {
            Wall wall = MakeFixedWall(spec, false, east);
            const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, east, false);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                eastl::vector<Tile> expected = ParseTiles(spec.hands[seat]).value();
                std::sort(expected.begin(), expected.end());
                EXPECT_EQ(Ids(hands[seat].concealed), Ids(expected)) << "east " << east << " seat " << seat;
            }
        }
    }

    TEST(FixedWallTest, TheWallAndTheHandsAreExactlyTheSet) {
        // Padding never makes a fifth copy: the whole thing is the set, once.
        for (const bool flowers : {false, true}) {
            FixedWallSpec spec = PlainSpec();
            spec.live = "Rd Rd Rd";
            Wall wall = MakeFixedWall(spec, flowers);
            const size_t total = flowers ? kFullWallWithBonus : kFullWall;
            EXPECT_EQ(wall.Remaining(), total);
            const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, flowers);
            std::vector<int> expected = Ids(BuildTileSet(flowers));
            std::sort(expected.begin(), expected.end());
            EXPECT_EQ(EveryTile(wall, hands), expected);
        }
    }

    TEST(FixedWallTest, TheRefillsSitAtTheEndOfTheLiveWallJustBeforeTheDeadWall) {
        FixedWallSpec spec = PlainSpec();
        spec.loose = "8s 9s 1s 2s";  // reserve 8s, 9s; the refills 1s, 2s in this order
        Wall wall = MakeFixedWall(spec, false);
        (void)DealHands(wall, 0, false);

        EXPECT_EQ(wall.DrawLoose()->Id(), Ids("8s").front());
        EXPECT_EQ(Ids(wall.LooseTiles()), Ids("9s 1s"));
        EXPECT_EQ(wall.DrawLoose()->Id(), Ids("9s").front());
        EXPECT_EQ(Ids(wall.LooseTiles()), Ids("1s 2s"));
    }

    TEST(FixedWallTest, LiveAfterDealCutsTheLiveWall) {
        FixedWallSpec spec = PlainSpec();
        spec.live = "1p 2p";
        spec.loose = "8s 9s 1s";
        spec.liveAfterDeal = 5;
        Wall wall = MakeFixedWall(spec, false);
        EXPECT_EQ(wall.Remaining(), kLooseTiles + kDealtTotal + 5 + kDeadWallTiles);
        (void)DealHands(wall, 0, false);

        EXPECT_EQ(wall.LiveCount(), 5U);
        const eastl::vector<Tile> live = wall.LiveTiles();
        EXPECT_EQ(Ids(eastl::vector<Tile>(live.begin(), live.begin() + 2)), Ids("1p 2p"));
        EXPECT_EQ(live.back().Id(), Ids("1s").front());  // the refill is the last live tile
        EXPECT_EQ(wall.DeadWallSize(), kDeadWallTiles);

        // Draining the live wall leaves the reserve and the dead wall.
        while (wall.Draw().has_value()) {
        }
        EXPECT_EQ(wall.Remaining(), kLooseTiles + kDeadWallTiles);
    }

    TEST(FixedWallTest, BonusTilesInASpecNeedTheFlowersSet) {
        FixedWallSpec spec = PlainSpec();
        spec.hands[0] = "F1 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s";
        EXPECT_NONFATAL_FAILURE((void)MakeFixedWall(spec, false), "more copies of F1");
        Wall wall = MakeFixedWall(spec, true);
        EXPECT_EQ(wall.Remaining(), kFullWallWithBonus);
    }

    TEST(FixedWallTest, AFifthCopyFailsTheTest) {
        FixedWallSpec spec = PlainSpec();
        spec.live = "1p 1p 1p";  // 1p is in seat 0 and seat 2 already: five copies
        EXPECT_NONFATAL_FAILURE((void)MakeFixedWall(spec, false), "more copies of 1p");
    }

    TEST(FixedWallTest, ABadHandFailsTheTest) {
        FixedWallSpec spec = PlainSpec();
        spec.hands[3] = "1p 2p";
        EXPECT_NONFATAL_FAILURE((void)MakeFixedWall(spec, false), "needs 13 tiles");
        FixedWallSpec garbled = PlainSpec();
        garbled.live = "1p zz";
        EXPECT_NONFATAL_FAILURE((void)MakeFixedWall(garbled, false), "cannot read the tiles");
    }

}  // namespace BECore::Mahjong::Tests
