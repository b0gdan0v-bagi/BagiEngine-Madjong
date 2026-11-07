#include "FixedWall.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>

/**
 * The deal: 13 tiles per seat, the order the wall is dealt in, bonus tiles replaced from the loose tiles. Pure logic: no engine
 * services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kFirstSeed = 1;
        constexpr int kSeedsToTry = 20;
        constexpr size_t kDealtTotal = static_cast<size_t>(kDealtTiles) * kSeatCount;
        constexpr size_t kFullWall = 136;

        MahjongSettings Settings(bool flowers) {
            MahjongSettings settings;
            settings.flowers = flowers;
            return settings;
        }

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

        std::vector<int> SortedIds(const eastl::vector<Tile>& tiles) {
            std::vector<int> ids = Ids(tiles);
            std::sort(ids.begin(), ids.end());
            return ids;
        }

        std::vector<int> SortedIds(const std::string& text) {
            return SortedIds(ParseTiles(text).value());
        }

        /// Every tile held by the four hands (concealed and bonus) and left in the wall, sorted.
        std::vector<int> EveryTile(const eastl::array<PlayerHand, kSeatCount>& hands, const Wall& wall) {
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

        /// A wall of `size` tiles whose tile at position k is the kind `k % kKindCount`, so a position can be told from the tile.
        Wall PositionWall(size_t size) {
            eastl::vector<Tile> order;
            for (size_t k = 0; k < size; ++k) {
                order.push_back(Tile{static_cast<uint8_t>(k % kKindCount)});
            }
            return Wall::FromOrder(eastl::move(order));
        }

        Tile PositionTile(size_t position) {
            return Tile{static_cast<uint8_t>(position % kKindCount)};
        }

        /// Four hands without a bonus tile and with at most two copies of each kind, to build the rigged walls from.
        FixedWallSpec PlainSpec() {
            FixedWallSpec spec;
            spec.hands[0] = "1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s";
            spec.hands[1] = "1m 2m 3m 4m 5m 6m 7m 8m 9m Ew Sw Ww Nw";
            spec.hands[2] = "1p 2p 3p 4p 5p 6p 7p 8p 9p Rd Wd Gd Ew";
            spec.hands[3] = "5s 6s 7s 8s 9s 1m 2m 3m 4m 5m 6m 7m 8m";
            return spec;
        }

    }  // namespace

    TEST(DealTest, EverySeatHoldsThirteenTiles) {
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kSeedsToTry; ++seed) {
            for (const Seat east : {0, 1, 2, 3}) {
                MahjongRng rng(seed);
                Wall wall = MakeWall(rng, Settings(false), east);
                const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, east, false);
                for (const PlayerHand& hand : hands) {
                    EXPECT_EQ(hand.concealed.size(), static_cast<size_t>(kDealtTiles)) << "seed " << seed;
                    EXPECT_TRUE(hand.bonus.empty());
                    EXPECT_TRUE(hand.melds.empty());
                    EXPECT_TRUE(std::is_sorted(hand.concealed.begin(), hand.concealed.end()));
                }
                EXPECT_EQ(wall.Remaining(), kFullWall - kDealtTotal) << "seed " << seed;
                EXPECT_EQ(wall.LooseCount(), kLooseTiles);
                EXPECT_EQ(wall.DeadWallSize(), kDeadWallTiles);
            }
        }
    }

    TEST(DealTest, TheDealAndTheWallHoldTheWholeSet) {
        for (const bool flowers : {false, true}) {
            for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kSeedsToTry; ++seed) {
                MahjongRng rng(seed);
                Wall wall = MakeWall(rng, Settings(flowers), 0);
                const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, flowers);
                EXPECT_EQ(EveryTile(hands, wall), SortedIds(BuildTileSet(flowers))) << "seed " << seed;
            }
        }
    }

    TEST(DealTest, WithFlowersEverySeatStillHoldsThirteenAndTheWallLosesOnePerBonusTile) {
        bool sawBonus = false;
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kSeedsToTry; ++seed) {
            MahjongRng rng(seed);
            Wall wall = MakeWall(rng, Settings(true), 0);
            const size_t before = wall.Remaining();
            const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, true);
            size_t bonus = 0;
            for (const PlayerHand& hand : hands) {
                EXPECT_EQ(hand.concealed.size(), static_cast<size_t>(kDealtTiles)) << "seed " << seed;
                for (const Tile tile : hand.concealed) {
                    EXPECT_FALSE(tile.IsBonus());
                }
                for (const Tile tile : hand.bonus) {
                    EXPECT_TRUE(tile.IsBonus());
                }
                bonus += hand.bonus.size();
            }
            sawBonus = sawBonus || bonus > 0;
            // 52 tiles stay in the hands, each bonus tile is a tile more that left the wall.
            EXPECT_EQ(wall.Remaining(), before - kDealtTotal - bonus) << "seed " << seed;
        }
        EXPECT_TRUE(sawBonus);
    }

    TEST(DealTest, DealOrderIsFourAtATimeFromEastThenOneEach) {
        // The first two tiles of the wall are the loose tiles; the deal draws from position 2 on.
        constexpr size_t kFirstLive = kLooseTiles;
        for (const Seat east : {0, 2}) {
            Wall wall = PositionWall(kFullWall);
            const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, east, false);

            for (int step = 0; step < kSeatCount; ++step) {
                const Seat seat = (east + step) % kSeatCount;
                eastl::vector<Tile> expected;
                for (int round = 0; round < kDealBlockRounds; ++round) {
                    for (int i = 0; i < kDealBlock; ++i) {
                        expected.push_back(PositionTile(kFirstLive + static_cast<size_t>(round * kSeatCount * kDealBlock + step * kDealBlock + i)));
                    }
                }
                expected.push_back(PositionTile(kFirstLive + static_cast<size_t>(kDealBlockRounds * kSeatCount * kDealBlock + step)));
                EXPECT_EQ(SortedIds(hands[seat].concealed), SortedIds(expected)) << "east " << east << " seat " << seat;
            }
        }
    }

    TEST(DealTest, ABonusTileInTheDealIsReplacedByTheFirstLooseTile) {
        FixedWallSpec spec = PlainSpec();
        spec.hands[0] = "F1 1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s";
        spec.loose = "4s 5s 6s 7s";  // reserve 4s, 5s; the refills 6s, 7s
        Wall wall = MakeFixedWall(spec, true, 0);
        const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, true);

        EXPECT_EQ(hands[0].concealed.size(), static_cast<size_t>(kDealtTiles));
        EXPECT_EQ(Ids(hands[0].bonus), (std::vector<int>{Tile::FromFlower(0).Id()}));
        EXPECT_EQ(Ids(hands[0].concealed), SortedIds("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s"));
        // The loose tile 4s left the reserve and the first refill (6s, the live tail) joined it.
        EXPECT_EQ(Ids(wall.LooseTiles()), Ids("5s 6s"));
        for (Seat seat = 1; seat < kSeatCount; ++seat) {
            EXPECT_EQ(Ids(hands[seat].concealed), SortedIds(spec.hands[seat]));
            EXPECT_TRUE(hands[seat].bonus.empty());
        }
    }

    TEST(DealTest, AReplacementThatIsABonusTileIsLaidOpenToo) {
        FixedWallSpec spec = PlainSpec();
        spec.hands[0] = "F1 1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s";
        spec.loose = "T1 5s 7s 8s";  // reserve T1, 5s; the refills 7s, 8s
        Wall wall = MakeFixedWall(spec, true, 0);
        const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, true);

        EXPECT_EQ(hands[0].concealed.size(), static_cast<size_t>(kDealtTiles));
        EXPECT_EQ(Ids(hands[0].bonus), (std::vector<int>{Tile::FromFlower(0).Id(), Tile::FromSeason(0).Id()}));
        EXPECT_EQ(Ids(hands[0].concealed), SortedIds("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 5s"));
        EXPECT_EQ(Ids(wall.LooseTiles()), Ids("7s 8s"));
    }

    TEST(DealTest, BonusTilesOfTwoSeatsUseTheLooseTilesInDealOrder) {
        FixedWallSpec spec = PlainSpec();
        spec.hands[0] = "F1 1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s";
        spec.hands[1] = "F2 2m 3m 4m 5m 6m 7m 8m 9m Ew Sw Ww Nw";
        spec.loose = "4s 5s 6s 7s";
        Wall wall = MakeFixedWall(spec, true, 0);
        const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, true);

        // East's slot comes first in the first round: it gets the first loose tile (4s), seat 1 the second (5s).
        EXPECT_EQ(Ids(hands[0].concealed), SortedIds("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s"));
        EXPECT_EQ(Ids(hands[1].concealed), SortedIds("2m 3m 4m 5m 6m 7m 8m 9m Ew Sw Ww Nw 5s"));
        EXPECT_EQ(Ids(hands[0].bonus), (std::vector<int>{Tile::FromFlower(0).Id()}));
        EXPECT_EQ(Ids(hands[1].bonus), (std::vector<int>{Tile::FromFlower(1).Id()}));
        EXPECT_EQ(Ids(wall.LooseTiles()), Ids("6s 7s"));
    }

    TEST(DealTest, WithoutFlowersTheBonusTilesAreNotTouched) {
        // A set without bonus tiles has none to deal; the flag only matters for the set the wall was made from.
        MahjongRng rng(kFirstSeed);
        Wall wall = MakeWall(rng, Settings(false), 0);
        const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, false);
        for (const PlayerHand& hand : hands) {
            EXPECT_TRUE(hand.bonus.empty());
        }
    }

    TEST(DealTest, AShortWallEndsTheDealEarly) {
        Wall wall = Wall::FromOrder({});
        const eastl::array<PlayerHand, kSeatCount> hands = DealHands(wall, 0, false);
        for (const PlayerHand& hand : hands) {
            EXPECT_TRUE(hand.concealed.empty());
        }
    }

    TEST(PlayerHandTest, AddConcealedKeepsTheHandSortedAndCountsCopies) {
        PlayerHand hand;
        const eastl::vector<Tile> tiles = ParseTiles("5p Ew 1s 5p 1p Rd 5p").value();
        for (const Tile tile : tiles) {
            hand.AddConcealed(tile);
        }
        EXPECT_EQ(Ids(hand.concealed), SortedIds("1p 5p 5p 5p 1s Ew Rd"));
        EXPECT_EQ(hand.CountConcealed(ParseTiles("5p").value().front()), 3);
        EXPECT_EQ(hand.CountConcealed(ParseTiles("9p").value().front()), 0);
    }

}  // namespace BECore::Mahjong::Tests
