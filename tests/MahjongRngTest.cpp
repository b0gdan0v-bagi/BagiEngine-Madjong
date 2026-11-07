#include <EASTL/sort.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <gtest/gtest.h>
#include <set>

/**
 * The seeded random source: reproducible sequences, dice, the hand-written shuffle, and golden values that pin the stream so a seed
 * gives the same game on every platform. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kGoldenSeed = 1;
        constexpr uint32_t kGoldenRange = 1000;
        constexpr size_t kGoldenTileCount = 20;
        constexpr int kDiceRolls = 6000;
        constexpr int kShuffleSeeds = 20;

        std::vector<uint32_t> BelowSequence(uint32_t seed, uint32_t n, size_t count) {
            MahjongRng rng(seed);
            std::vector<uint32_t> values;
            for (size_t i = 0; i < count; ++i) {
                values.push_back(rng.Below(n));
            }
            return values;
        }

        eastl::vector<Tile> ShuffledSet(uint32_t seed, bool flowers = false) {
            MahjongRng rng(seed);
            eastl::vector<Tile> tiles = BuildTileSet(flowers);
            rng.Shuffle(tiles);
            return tiles;
        }

        std::string Join(const eastl::vector<Tile>& tiles, size_t count) {
            std::string text;
            for (size_t i = 0; i < count && i < tiles.size(); ++i) {
                if (i > 0) {
                    text += ' ';
                }
                text += ToString(tiles[i]);
            }
            return text;
        }

    }  // namespace

    TEST(MahjongRngTest, SameSeedSameSequence) {
        EXPECT_EQ(BelowSequence(7, 1000, 50), BelowSequence(7, 1000, 50));
    }

    TEST(MahjongRngTest, DifferentSeedsDiffer) {
        EXPECT_NE(BelowSequence(1, 1000, 20), BelowSequence(2, 1000, 20));
    }

    TEST(MahjongRngTest, SeedRestartsTheSequence) {
        MahjongRng rng(5);
        const uint32_t first = rng.Below(1000);
        const uint32_t second = rng.Below(1000);
        rng.Seed(5);
        EXPECT_EQ(rng.GetSeed(), 5u);
        EXPECT_EQ(rng.Below(1000), first);
        EXPECT_EQ(rng.Below(1000), second);
    }

    TEST(MahjongRngTest, BelowStaysInRange) {
        MahjongRng rng(3);
        for (int i = 0; i < 1000; ++i) {
            EXPECT_LT(rng.Below(7), 7u);
        }
        EXPECT_EQ(rng.Below(1), 0u);
        EXPECT_EQ(rng.Below(0), 0u);
    }

    TEST(MahjongRngTest, DiceAreOneToSixAndEveryFaceAppears) {
        MahjongRng rng(11);
        std::set<int> faces;
        std::set<int> totals;
        for (int i = 0; i < kDiceRolls; ++i) {
            const int die = rng.RollDie();
            ASSERT_GE(die, 1);
            ASSERT_LE(die, kDieFaces);
            faces.insert(die);
            const int total = rng.Roll2d6();
            ASSERT_GE(total, 2);
            ASSERT_LE(total, 2 * kDieFaces);
            totals.insert(total);
        }
        EXPECT_EQ(faces.size(), static_cast<size_t>(kDieFaces));
        EXPECT_EQ(totals.size(), 11u);  // 2..12
    }

    TEST(MahjongRngTest, Roll2d6IsTwoDieRolls) {
        MahjongRng a(9);
        MahjongRng b(9);
        for (int i = 0; i < 100; ++i) {
            const int first = b.RollDie();
            const int second = b.RollDie();
            EXPECT_EQ(a.Roll2d6(), first + second);
        }
    }

    TEST(MahjongRngTest, ShuffleKeepsTheMultiset) {
        for (const bool flowers : {false, true}) {
            eastl::vector<Tile> original = BuildTileSet(flowers);
            eastl::vector<Tile> shuffled = ShuffledSet(4, flowers);
            ASSERT_EQ(shuffled.size(), original.size());
            EXPECT_NE(shuffled, original);
            eastl::sort(original.begin(), original.end());
            eastl::sort(shuffled.begin(), shuffled.end());
            EXPECT_EQ(shuffled, original);
        }
    }

    TEST(MahjongRngTest, ShuffleIsTheSameForTheSameSeedAndDiffersBetweenSeeds) {
        EXPECT_EQ(ShuffledSet(8), ShuffledSet(8));
        for (int seed = 1; seed < kShuffleSeeds; ++seed) {
            EXPECT_NE(ShuffledSet(seed), ShuffledSet(seed + 1)) << "seed " << seed;
        }
    }

    TEST(MahjongRngTest, ShuffleHandlesSmallInputs) {
        MahjongRng rng(1);
        eastl::vector<int> empty;
        rng.Shuffle(empty);
        EXPECT_TRUE(empty.empty());
        eastl::vector<int> single = {42};
        rng.Shuffle(single);
        ASSERT_EQ(single.size(), 1u);
        EXPECT_EQ(single[0], 42);
    }

    // Golden values: the numbers come from the first run and are pinned (the first eight are the known mt19937 outputs for seed 1,
    // 1791095845 4282876139 3093770124 ..., taken modulo 1000). The mt19937 stream is fixed by the standard and the shuffle
    // is written by hand, so they are the same on every platform; a change here means a seed no longer gives the same game.
    TEST(MahjongRngTest, GoldenBelowOfSeedOne) {
        const std::vector<uint32_t> expected = {845, 139, 124, 368, 263, 313, 491, 341};
        EXPECT_EQ(BelowSequence(kGoldenSeed, kGoldenRange, expected.size()), expected);
    }

    TEST(MahjongRngTest, GoldenShuffledSetOfSeedOne) {
        EXPECT_EQ(Join(ShuffledSet(kGoldenSeed), kGoldenTileCount), "2p 1m 5m 3m 6p 8m 2s 1p Ww 7s 3m 6p Sw 8p Ew Rd Wd 7m 9s 8p");
    }

}  // namespace BECore::Mahjong::Tests
