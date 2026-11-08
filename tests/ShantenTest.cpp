#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/Shanten.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * `Shanten` and `ImprovingTiles`: values worked out by hand for hands written in the tile notation, then properties on seeded random
 * hands against `Decompose` (the reference for "complete" and "waiting"). Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Hands of the cheap properties (at most one `Shanten` and one `Decompose` probe per kind), and of the ones that call `Shanten`
        /// for every kind added or taken away (about 70 searches a hand, 2 to 6 microseconds each in Debug).
        constexpr int kHandsPerProperty = 2000;
        constexpr int kHandsPerProbingProperty = 2000;
        /// The property that adds every kind and discards every kind held (about 400 searches a hand) runs on fewer hands.
        constexpr int kHandsPerExchangeProperty = 1000;
        constexpr int kShantenOfNothing = 2 * kSetsInHand;
        /// One tile in this many of a crowded hand is of any kind.
        constexpr uint32_t kStrangerOneIn = 6;

        TileCounts Counts(const std::string& text) {
            return CountTiles(ParseTiles(text).value());
        }

        int ShantenOf(const std::string& hand, int meldCount = 0) {
            return Shanten(Counts(hand), meldCount);
        }

        /// The improving tiles as the notation in the order of the tile ids.
        std::string Improving(const std::string& hand, int meldCount = 0) {
            std::string text;
            for (const Tile tile : ImprovingTiles(Counts(hand), meldCount)) {
                text += text.empty() ? "" : " ";
                text += ToString(tile);
            }
            return text;
        }

        /// Whether the concealed tiles are four sets and a pair (with `meldCount` melds), by the reference `Decompose`.
        bool IsComplete(const TileCounts& counts, int meldCount) {
            return !Decompose(counts, kSetsInHand - meldCount).empty();
        }

        /// Whether one more tile of some kind (the copy limit ignored) makes the hand complete, by the reference `Decompose`.
        bool IsOneAway(const TileCounts& counts, int meldCount) {
            TileCounts probe = counts;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                ++probe[id];
                const bool complete = IsComplete(probe, meldCount);
                --probe[id];
                if (complete) {
                    return true;
                }
            }
            return false;
        }

        /// How `FillRandom` picks the kinds.
        CORE_ENUM(Spread, uint8_t, AnyKind, MostlyOneSuit, OneSuit)

        /// The four kinds of hand `HandMaker::Make` draws: the three spreads, and sets with a pair and a few swaps.
        CORE_ENUM(HandStyle, uint8_t, Random, MostlyOneSuit, OneSuit, NearComplete)

        /// Seeded hands of `3n + 1` or `3n + 2` concealed tiles for `meldCount` melds: random, near-complete, mostly one suit or one
        /// suit (the hands where the search has the most readings to walk).
        class HandMaker {
        public:
            explicit HandMaker(uint32_t seed) : _rng(seed) {}

            /// A hand of `size` concealed tiles; never more than four of a kind.
            TileCounts Make(int size) {
                TileCounts counts{};
                const auto style = static_cast<HandStyle>(_rng.Below(static_cast<uint32_t>(EnumUtils<HandStyle>::Values().size())));
                if (style == HandStyle::Random) {
                    FillRandom(counts, size, Spread::AnyKind);
                } else if (style == HandStyle::MostlyOneSuit) {
                    FillRandom(counts, size, Spread::MostlyOneSuit);
                } else if (style == HandStyle::OneSuit) {
                    FillRandom(counts, size, Spread::OneSuit);
                } else {
                    FillNearComplete(counts, size);
                }
                return counts;
            }

        private:
            /// Tiles of any kind, or mostly of one suit with a few strangers, or of one suit alone: chows and partial sets are likely.
            void FillRandom(TileCounts& counts, int size, Spread spread) {
                const uint8_t suitBase = static_cast<uint8_t>(_rng.Below(kSuitCount) * kRanksPerSuit);
                int placed = 0;
                while (placed < size) {
                    const bool stranger = spread == Spread::AnyKind || (spread == Spread::MostlyOneSuit && _rng.Below(kStrangerOneIn) == 0);
                    const uint8_t id = stranger ? RandomKind() : static_cast<uint8_t>(suitBase + _rng.Below(kRanksPerSuit));
                    placed += Add(counts, id, 1) ? 1 : 0;
                }
            }

            /// Sets and a pair of random kinds, then a few tiles swapped for random ones: many of them are complete or waiting.
            void FillNearComplete(TileCounts& counts, int size) {
                const int sets = (size - kTilesInPair) / kTilesPerSet + 1;
                int placed = 0;
                for (int i = 0; i < sets && placed + kTilesPerSet <= size; ++i) {
                    placed += AddSet(counts);
                }
                if (placed + kTilesInPair <= size && Add(counts, RandomKind(), kTilesInPair)) {
                    placed += kTilesInPair;
                }
                while (placed < size) {
                    placed += Add(counts, RandomKind(), 1) ? 1 : 0;
                }
                const int swaps = static_cast<int>(_rng.Below(3));
                for (int i = 0; i < swaps; ++i) {
                    Swap(counts);
                }
            }

            int AddSet(TileCounts& counts) {
                const uint8_t id = RandomKind();
                const Tile tile{id};
                if (_rng.Below(3) == 0 || !tile.IsSuit() || tile.Rank() > kLastChowStartRank) {
                    return Add(counts, id, kTilesPerSet) ? kTilesPerSet : 0;
                }
                if (counts[id] < kCopiesPerTile && counts[id + 1] < kCopiesPerTile && counts[id + 2] < kCopiesPerTile) {
                    ++counts[id];
                    ++counts[id + 1];
                    ++counts[id + 2];
                    return kTilesPerSet;
                }
                return 0;
            }

            bool Add(TileCounts& counts, uint8_t id, int copies) {
                if (counts[id] + copies > kCopiesPerTile) {
                    return false;
                }
                counts[id] = static_cast<uint8_t>(counts[id] + copies);
                return true;
            }

            /// Replaces one held tile by a random other kind (nothing happens when the new kind is full).
            void Swap(TileCounts& counts) {
                const uint8_t out = RandomKind();
                const uint8_t in = RandomKind();
                if (counts[out] > 0 && counts[in] < kCopiesPerTile) {
                    --counts[out];
                    ++counts[in];
                }
            }

            uint8_t RandomKind() {
                return static_cast<uint8_t>(_rng.Below(kKindCount));
            }

            MahjongRng _rng;
        };

        /// The concealed tiles of a hand with `meldCount` melds: `3n + 1` (waiting at best) or `3n + 2` (can be complete).
        int HandSize(int meldCount, bool extraTile) {
            return kWinningHandTiles - 1 - meldCount * kTilesPerSet + (extraTile ? 1 : 0);
        }

        template <class Check>
        void ForEachRandomHand(uint32_t seed, int hands, Check check) {
            HandMaker maker(seed);
            for (int i = 0; i < hands; ++i) {
                const int meldCount = i % kSetsInHand;
                const bool extraTile = (i / kSetsInHand) % 2 == 1;
                const TileCounts counts = maker.Make(HandSize(meldCount, extraTile));
                std::string text;
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    for (int c = 0; c < counts[id]; ++c) {
                        text += std::string{ToString(Tile{id})} + " ";
                    }
                }
                SCOPED_TRACE(testing::Message() << "hand " << i << ", " << meldCount << " melds: " << text);
                check(counts, meldCount);
            }
        }

    }  // namespace

    // ---- Values worked out by hand ------------------------------------------------------------------------------------------------

    TEST(ShantenTest, ACompleteHandIsMinusOne) {
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"), kShantenComplete);
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Rd Rd", 1), kShantenComplete);
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s Rd Rd", 2), kShantenComplete);
        EXPECT_EQ(ShantenOf("1p 2p 3p Rd Rd", 3), kShantenComplete);
        EXPECT_EQ(ShantenOf("Rd Rd", 4), kShantenComplete);
        EXPECT_EQ(ShantenOf("1p 1p 1p 2p 3p 4p 5p 6p 7p 8p 8p 8p 9p 9p"), kShantenComplete);
    }

    TEST(ShantenTest, AWaitingHandIsZero) {
        const std::string sets = "4s 5s 6s 7m 8m 9m Rd Rd Rd";
        EXPECT_EQ(ShantenOf(sets + " Ew Ew 4p 5p"), 0) << "two-sided";
        EXPECT_EQ(ShantenOf(sets + " Ew Ew 1p 2p"), 0) << "edge";
        EXPECT_EQ(ShantenOf(sets + " Ew Ew 4p 6p"), 0) << "middle";
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Rd Rd"), 0) << "pair wait; the second pair is a partial set";
        EXPECT_EQ(ShantenOf(sets + " Ew Sw Sw Sw"), 0) << "single";
        EXPECT_EQ(ShantenOf("4s 5s 6s 7m 8m 9m Rd Rd Rd Ew Ew Ew Ew"), 0) << "a dead wait (the fifth East does not exist) is still 0";
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s Ew", 2), 0) << "a waiting hand with melds";
    }

    TEST(ShantenTest, FartherHandsCountTheMissingTiles) {
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Rd 5p 7p"), 1) << "three sets, one partial set";
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m 2s 9s Ew Rd"), 2) << "three sets and floating tiles";
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Rd 5p 7p 9s"), 1) << "14 tiles: after the best discard the same as 13";
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 1m 3m 9m 9s Ew Sw Ww"), 3) << "two sets, one partial set and singles";
    }

    TEST(ShantenTest, ThirteenIsolatedTilesAreTheWorst) {
        EXPECT_EQ(ShantenOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Ew Sw Ww Nw"), kShantenOfNothing);
        EXPECT_EQ(ShantenOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Ew", 1), 2 * 3);
        EXPECT_EQ(ShantenOf("1p 4p 7p 1s 4s 7s 1m", 2), 2 * 2);
        EXPECT_EQ(ShantenOf("1p 4p 7p 1s", 3), 2 * 1);
        EXPECT_EQ(ShantenOf("1p", 4), 0) << "no sets needed: the single tile becomes the pair";
    }

    TEST(ShantenTest, ThePairIsCountedOnce) {
        // Two pairs and three sets wait on either pair; the second pair is only a partial set.
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Rd Rd"), 0);
        // Without a pair at all the same sets are a step further: three sets and two singles.
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Sw Rd Gd"), 2);
        // Three pairs and two sets: one pair is the pair, two are partial sets, 8 - 4 - 2 - 1.
        EXPECT_EQ(ShantenOf("1p 2p 3p 4s 5s 6s Ew Ew Sw Sw Rd Rd 9m"), 1);
    }

    TEST(ShantenTest, MoreThanFourBlocksDoNotLowerTheNumber) {
        EXPECT_EQ(ShantenOf("1p 2p 4p 5p 7p 8p 1s 2s Ew Sw Ww Nw Rd"), 4) << "four partial sets";
        EXPECT_EQ(ShantenOf("1p 2p 4p 5p 7p 8p 1s 2s 4s 5s Ew Sw Ww"), 4) << "five partial sets";
        EXPECT_EQ(ShantenOf("1p 2p 4p 5p 7p 8p 1s 2s 4s 5s 7s 8s 9m"), 4) << "six partial sets";
    }

    TEST(ShantenTest, APungIsASetAndFourOfAKindIsAPungAndASingle) {
        EXPECT_EQ(ShantenOf("1p 1p 1p 1p", 3), 0);
        EXPECT_EQ(ShantenOf("1p 1p 1p 1p 2p 3p 4s 5s 6s 7m 8m 9m Rd"), 0) << "a pung of 1p, the chow 1 2 3 and Rd single";
        EXPECT_EQ(ShantenOf("1p 1p 1p 1p 5p 6p 7p 9s 9s 9s Rd Rd Rd"), 0) << "three sets and 1p 1p 1p 1p: a pung and a single";
        EXPECT_EQ(ShantenOf("1p 1p 1p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd"), 0);
    }

    TEST(ShantenTest, MeldsAreNotInTheCountsButCountAsSets) {
        const std::string tiles = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Rd";
        EXPECT_EQ(ShantenOf(tiles, 1), 0) << "one meld: three sets and two singles are enough to wait";
        EXPECT_EQ(ShantenOf(tiles, 0), 2) << "the same tiles with no meld need a fourth set";
    }

    TEST(ShantenTest, AMeldCountOutOfRangeIsClamped) {
        EXPECT_EQ(Shanten(Counts("1p 2p 3p"), -2), Shanten(Counts("1p 2p 3p"), 0));
        EXPECT_EQ(Shanten(Counts("Rd Rd"), 9), Shanten(Counts("Rd Rd"), kSetsInHand));
    }

    TEST(ShantenTest, SevenPairsAndOrphansAreNotLookedAt) {
        EXPECT_GE(ShantenOf("1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2s 2s 4s"), 2) << "six pairs: a high number although it waits for seven pairs";
        EXPECT_GE(ShantenOf("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd"), 6) << "thirteen orphans";
    }

    TEST(ShantenTest, ImprovingTilesOfWaitingAndFartherHands) {
        EXPECT_EQ(Improving("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew 4p 5p"), "3p 6p") << "the waiting tiles";
        EXPECT_EQ(Improving("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew 1s 3s"), "2s") << "middle wait: the one tile (4s neither)";
        EXPECT_EQ(Improving("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Rd 5p 7p"), "6p Ew Rd") << "one-shanten: the gap, or a pair of a single";
        EXPECT_EQ(Improving("4s 5s 6s 7m 8m 9m Rd Rd Rd Ew Ew Ew Ew"), "Ew") << "the dead wait is still listed";
        EXPECT_EQ(Improving("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Rd Rd"), "Ew Rd");
        EXPECT_EQ(Improving("1p 2p 3p 4s 5s 6s 7m 8m 9m Rd Rd", 1), "") << "already complete: nothing improves it";
    }

    TEST(ShantenTest, ImprovingTilesOfACompleteHandAreEmpty) {
        EXPECT_TRUE(ImprovingTiles(Counts("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"), 0).empty());
        EXPECT_TRUE(ImprovingTiles(Counts("Rd Rd"), 4).empty());
    }

    // ---- Properties on seeded random hands ------------------------------------------------------------------------------------------

    TEST(ShantenTest, MinusOneExactlyWhenTheHandIsComplete) {
        int complete = 0;
        ForEachRandomHand(11, kHandsPerProperty, [&](const TileCounts& counts, int meldCount) {
            const bool isComplete = IsComplete(counts, meldCount);
            EXPECT_EQ(Shanten(counts, meldCount) == kShantenComplete, isComplete);
            complete += isComplete ? 1 : 0;
        });
        EXPECT_GT(complete, 50) << "the generator has to produce complete hands";
    }

    TEST(ShantenTest, ZeroExactlyWhenOneTileCompletesAHandOfThreeNPlusOne) {
        int waiting = 0;
        int farther = 0;
        ForEachRandomHand(12, kHandsPerProperty, [&](const TileCounts& counts, int meldCount) {
            if (TotalTiles(counts) != HandSize(meldCount, false)) {
                return;
            }
            const int shanten = Shanten(counts, meldCount);
            EXPECT_GE(shanten, 0) << "a hand of 3n + 1 tiles is never complete";
            EXPECT_EQ(shanten == 0, IsOneAway(counts, meldCount));
            if (shanten == 0) {
                ++waiting;
            } else {
                ++farther;
            }
        });
        EXPECT_GT(waiting, 50);
        EXPECT_GT(farther, 50);
    }

    TEST(ShantenTest, ImprovingTilesAreExactlyThoseThatLowerTheNumber) {
        int listed = 0;
        ForEachRandomHand(13, kHandsPerProbingProperty, [&](const TileCounts& counts, int meldCount) {
            const int shanten = Shanten(counts, meldCount);
            const eastl::vector<Tile> improving = ImprovingTiles(counts, meldCount);
            EXPECT_EQ(improving.empty(), shanten == kShantenComplete);
            EXPECT_TRUE(std::is_sorted(improving.begin(), improving.end()));
            TileCounts probe = counts;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                ++probe[id];
                const int after = Shanten(probe, meldCount);
                --probe[id];
                const bool isListed = std::find(improving.begin(), improving.end(), Tile{id}) != improving.end();
                const bool lowers = shanten != kShantenComplete && after < shanten;
                EXPECT_EQ(isListed, lowers) << "kind " << ToString(Tile{id});
                if (lowers) {
                    EXPECT_EQ(after, shanten - 1);
                }
            }
            listed += static_cast<int>(improving.size());
        });
        EXPECT_GT(listed, 500);
    }

    TEST(ShantenTest, EveryValueIsTheFewestExchangesFromACompleteHand) {
        // A hand of 3n + 1 tiles at k > 0 reaches k - 1 by taking one tile and giving one back, and no exchange reaches less: with the
        // anchors above (-1 and 0 against `Decompose`) this pins every value.
        int checked = 0;
        ForEachRandomHand(15, kHandsPerExchangeProperty, [&](const TileCounts& counts, int meldCount) {
            if (TotalTiles(counts) != HandSize(meldCount, false)) {
                return;
            }
            const int shanten = Shanten(counts, meldCount);
            if (shanten <= 0) {
                return;
            }
            int bestAfterExchange = kShantenOfNothing;
            TileCounts probe = counts;
            for (uint8_t added = 0; added < kKindCount; ++added) {
                ++probe[added];
                for (uint8_t given = 0; given < kKindCount; ++given) {
                    if (probe[given] == 0) {
                        continue;
                    }
                    --probe[given];
                    bestAfterExchange = std::min(bestAfterExchange, Shanten(probe, meldCount));
                    ++probe[given];
                }
                --probe[added];
            }
            EXPECT_EQ(bestAfterExchange, shanten - 1);
            ++checked;
        });
        EXPECT_GT(checked, 200);
    }

    TEST(ShantenTest, AddingATileNeverRaisesTheNumberAndDiscardingNeverLowersItByMoreThanOne) {
        ForEachRandomHand(14, kHandsPerProbingProperty, [](const TileCounts& counts, int meldCount) {
            const int shanten = Shanten(counts, meldCount);
            TileCounts probe = counts;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                ++probe[id];
                EXPECT_LE(Shanten(probe, meldCount), shanten) << "adding " << ToString(Tile{id});
                --probe[id];
                if (counts[id] == 0) {
                    continue;
                }
                --probe[id];
                EXPECT_GE(Shanten(probe, meldCount), shanten) << "discarding " << ToString(Tile{id}) << " lowered the number";
                EXPECT_LE(Shanten(probe, meldCount), shanten + 1) << "discarding " << ToString(Tile{id}) << " raised it by two";
                ++probe[id];
            }
        });
    }

}  // namespace BECore::Mahjong::Tests
