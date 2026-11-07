#include <EASTL/algorithm.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <gtest/gtest.h>
#include <set>

/**
 * Tile ids and predicates, the notation, the full tile set and the settings defaults. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(kKindCount == 34);
    static_assert(kBonusCount == 8);
    static_assert(kCopiesPerTile == 4);
    static_assert(kTileIdCount == 42);
    static_assert(kTileSetSize == 136);
    static_assert(kTileSetSizeWithBonus == 144);
    static_assert(Tile::Suited(TileSuit::Dots, 1).Id() == 0);
    static_assert(Tile::Suited(TileSuit::Dots, 9).Id() == 8);
    static_assert(Tile::Suited(TileSuit::Bamboo, 1).Id() == 9);
    static_assert(Tile::Suited(TileSuit::Characters, 9).Id() == 26);
    static_assert(Tile::FromWind(0).Id() == 27);
    static_assert(Tile::FromWind(3).Id() == 30);
    static_assert(Tile::FromDragon(0).Id() == 31);
    static_assert(Tile::FromDragon(2).Id() == 33);
    static_assert(Tile::FromFlower(0).Id() == 34);
    static_assert(Tile::FromFlower(3).Id() == 37);
    static_assert(Tile::FromSeason(0).Id() == 38);
    static_assert(Tile::FromSeason(3).Id() == 41);
    static_assert(!Tile{}.IsValid());
    static_assert(!Tile{kTileIdCount}.IsValid());
    static_assert(!Tile::Suited(TileSuit::Dots, 0).IsValid());
    static_assert(!Tile::Suited(TileSuit::Dots, 10).IsValid());
    static_assert(!Tile::Suited(static_cast<TileSuit>(4), 1).IsValid());
    static_assert(!Tile::Suited(TileSuit::None, 1).IsValid());
    static_assert(!Tile::FromWind(4).IsValid());
    static_assert(Tile::Suited(TileSuit::Bamboo, 2) < Tile::Suited(TileSuit::Characters, 1));
    static_assert(Tile::Suited(TileSuit::Dots, 5).Suit() == TileSuit::Dots);
    static_assert(Tile::Suited(TileSuit::Dots, 5).Rank() == 5);

    namespace {

        /// The green tiles by id: bamboo 2, 3, 4, 6, 8 and the green dragon.
        const std::set<uint8_t> kGreenIds = {10, 11, 12, 14, 16, 33};

        std::string Str(Tile tile) {
            return std::string(ToString(tile));
        }

    }  // namespace

    TEST(TileTest, CompileTimeChecks) {
        // The static_asserts above are the checks; the case makes them show up in the report.
        SUCCEED();
    }

    TEST(TileTest, EveryIdHasOneCategory) {
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile{id};
            ASSERT_TRUE(tile.IsValid()) << static_cast<int>(id);
            EXPECT_EQ(tile.Id(), id);
            const int groups = static_cast<int>(tile.IsSuit()) + static_cast<int>(tile.IsWind()) + static_cast<int>(tile.IsDragon()) + static_cast<int>(tile.IsBonus());
            EXPECT_EQ(groups, 1) << Str(tile);
            EXPECT_EQ(tile.IsHonour(), tile.IsWind() || tile.IsDragon()) << Str(tile);
            EXPECT_EQ(tile.IsPlayable(), id < kKindCount) << Str(tile);
            EXPECT_EQ(tile.IsSuit(), id < 27) << Str(tile);
            EXPECT_EQ(tile.IsWind(), id >= 27 && id <= 30) << Str(tile);
            EXPECT_EQ(tile.IsDragon(), id >= 31 && id <= 33) << Str(tile);
            EXPECT_EQ(tile.IsBonus(), id >= 34) << Str(tile);
        }
    }

    TEST(TileTest, SuitAndRank) {
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile{id};
            if (id < 27) {
                EXPECT_EQ(tile.Rank(), id % 9 + 1) << Str(tile);
                EXPECT_EQ(static_cast<int>(tile.Suit()), id / 9 + 1) << Str(tile);
                EXPECT_EQ(Tile::Suited(tile.Suit(), tile.Rank()), tile);
            } else {
                EXPECT_EQ(tile.Rank(), 0) << Str(tile);
                EXPECT_EQ(tile.Suit(), TileSuit::None) << Str(tile);
            }
        }
    }

    TEST(TileTest, TerminalsAndSimples) {
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile{id};
            const bool terminal = id < 27 && (id % 9 == 0 || id % 9 == 8);
            const bool simple = id < 27 && !terminal;
            EXPECT_EQ(tile.IsTerminal(), terminal) << Str(tile);
            EXPECT_EQ(tile.IsSimple(), simple) << Str(tile);
        }
        EXPECT_FALSE(Tile::FromWind(0).IsTerminal());
        EXPECT_FALSE(Tile::FromDragon(0).IsSimple());
    }

    TEST(TileTest, GreenSetIsExact) {
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            EXPECT_EQ(Tile{id}.IsGreen(), kGreenIds.contains(id)) << Str(Tile{id});
        }
    }

    TEST(TileTest, InvalidTileIsNothing) {
        const Tile none;
        EXPECT_FALSE(none.IsValid());
        EXPECT_FALSE(none.IsSuit());
        EXPECT_FALSE(none.IsHonour());
        EXPECT_FALSE(none.IsWind());
        EXPECT_FALSE(none.IsDragon());
        EXPECT_FALSE(none.IsBonus());
        EXPECT_FALSE(none.IsPlayable());
        EXPECT_FALSE(none.IsTerminal());
        EXPECT_FALSE(none.IsSimple());
        EXPECT_FALSE(none.IsGreen());
        EXPECT_EQ(none.Suit(), TileSuit::None);
        EXPECT_EQ(none.Rank(), 0);
        EXPECT_EQ(Str(none), "??");
    }

    TEST(TileTest, NotationOfKnownTiles) {
        EXPECT_EQ(Str(Tile::Suited(TileSuit::Dots, 1)), "1p");
        EXPECT_EQ(Str(Tile::Suited(TileSuit::Bamboo, 9)), "9s");
        EXPECT_EQ(Str(Tile::Suited(TileSuit::Characters, 5)), "5m");
        EXPECT_EQ(Str(Tile::FromWind(0)), "Ew");
        EXPECT_EQ(Str(Tile::FromWind(1)), "Sw");
        EXPECT_EQ(Str(Tile::FromWind(2)), "Ww");
        EXPECT_EQ(Str(Tile::FromWind(3)), "Nw");
        EXPECT_EQ(Str(Tile::FromDragon(0)), "Rd");
        EXPECT_EQ(Str(Tile::FromDragon(1)), "Wd");
        EXPECT_EQ(Str(Tile::FromDragon(2)), "Gd");
        EXPECT_EQ(Str(Tile::FromFlower(0)), "F1");
        EXPECT_EQ(Str(Tile::FromFlower(3)), "F4");
        EXPECT_EQ(Str(Tile::FromSeason(0)), "T1");
        EXPECT_EQ(Str(Tile::FromSeason(3)), "T4");
    }

    TEST(TileTest, NotationRoundTripForAllIds) {
        std::set<std::string> names;
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile{id};
            const auto parsed = TryParse(ToString(tile));
            ASSERT_TRUE(parsed.has_value()) << static_cast<int>(id);
            EXPECT_EQ(*parsed, tile);
            names.insert(Str(tile));
        }
        EXPECT_EQ(names.size(), size_t{kTileIdCount});  // every id has its own name
    }

    TEST(TileTest, BadNotationIsRejected) {
        for (const char* text : {"", "1", "0p", "10p", "1x", "p1", "1P", "ew", "EW", "Xw", "Bd", "F0", "F5", "T0", "T5", "1p ", " 1p", "1pp", "Ew1", "??"}) {
            EXPECT_FALSE(TryParse(text).has_value()) << '"' << text << '"';
        }
    }

    TEST(TileTest, ParseTilesReadsSpaceSeparatedList) {
        const auto tiles = ParseTiles("1p 2p 3p Ew Rd F1");
        ASSERT_TRUE(tiles.has_value());
        ASSERT_EQ(tiles->size(), 6u);
        EXPECT_EQ((*tiles)[0], Tile::Suited(TileSuit::Dots, 1));
        EXPECT_EQ((*tiles)[2], Tile::Suited(TileSuit::Dots, 3));
        EXPECT_EQ((*tiles)[3], Tile::FromWind(0));
        EXPECT_EQ((*tiles)[4], Tile::FromDragon(0));
        EXPECT_EQ((*tiles)[5], Tile::FromFlower(0));

        const auto single = ParseTiles("9m");
        ASSERT_TRUE(single.has_value());
        EXPECT_EQ(single->size(), 1u);

        const auto empty = ParseTiles("");
        ASSERT_TRUE(empty.has_value());
        EXPECT_TRUE(empty->empty());
    }

    TEST(TileTest, ParseTilesRejectsBadInput) {
        EXPECT_FALSE(ParseTiles("1p 2x 3p").has_value());
        EXPECT_FALSE(ParseTiles("1p  2p").has_value());  // a doubled space is an empty token
        EXPECT_FALSE(ParseTiles(" 1p").has_value());
        EXPECT_FALSE(ParseTiles("1p ").has_value());
        EXPECT_FALSE(ParseTiles("1p2p").has_value());
    }

    TEST(TileTest, SetSizeWithoutAndWithFlowers) {
        EXPECT_EQ(BuildTileSet(false).size(), 136u);
        EXPECT_EQ(BuildTileSet(true).size(), 144u);
    }

    TEST(TileTest, SetHasFourOfEachPlayableKindAndOneOfEachBonus) {
        for (const bool flowers : {false, true}) {
            eastl::array<int, kTileIdCount> counts{};
            for (const Tile tile : BuildTileSet(flowers)) {
                ASSERT_TRUE(tile.IsValid());
                ++counts[tile.Id()];
            }
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const Tile tile{id};
                const int expected = tile.IsPlayable() ? kCopiesPerTile : (flowers ? 1 : 0);
                EXPECT_EQ(counts[id], expected) << Str(tile) << " flowers=" << flowers;
            }
        }
    }

    TEST(TileTest, SetOrderIsFixed) {
        const auto plain = BuildTileSet(false);
        const auto withBonus = BuildTileSet(true);
        EXPECT_EQ(plain.front(), Tile::Suited(TileSuit::Dots, 1));
        EXPECT_EQ(plain[3], Tile::Suited(TileSuit::Dots, 1));
        EXPECT_EQ(plain[4], Tile::Suited(TileSuit::Dots, 2));
        EXPECT_EQ(plain.back(), Tile::FromDragon(2));
        EXPECT_TRUE(eastl::equal(plain.begin(), plain.end(), withBonus.begin()));  // the flowers only append
        EXPECT_EQ(withBonus[136], Tile::FromFlower(0));
        EXPECT_EQ(withBonus.back(), Tile::FromSeason(3));
    }

    TEST(TileTest, SettingsDefaults) {
        const MahjongSettings settings;
        EXPECT_EQ(settings.handsPerMatch, 16);
        EXPECT_FALSE(settings.flowers);
        EXPECT_TRUE(settings.replayDrawnHand);
        EXPECT_EQ(settings.limit, 500);
    }

}  // namespace BECore::Mahjong::Tests
