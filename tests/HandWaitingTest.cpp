#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * `IsWaiting` and the pruned `WinningTiles` against a reference kept here: the loop over all 34 kinds with `IsWinningShape` that
 * `WinningTiles` was before it probed only the kinds next to a held tile. Hands come from a seeded `MahjongRng`: random ones and
 * near-complete ones (sets and a pair with one tile taken out, so that most of them are waiting), seven pairs, thirteen orphans, hands
 * with copies in melds. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kSamplesPerKind = 700;

        /// A hand under test: the concealed tiles, the tiles of the melds and the number of the melds.
        struct Sample {
            TileCounts counts{};
            TileCounts meldTiles{};
            int meldCount = 0;
        };

        TileCounts Counts(const std::string& text) {
            return CountTiles(ParseTiles(text).value());
        }

        eastl::vector<Tile> WaitsOn(const std::string& text) {
            return eastl::vector<Tile>{ParseTiles(text).value()[0]};
        }

        std::string Describe(const eastl::vector<Tile>& tiles) {
            std::string text;
            for (const Tile tile : tiles) {
                text += std::string{ToString(tile)} + " ";
            }
            return text;
        }

        /// What `WinningTiles` returned before the prune: every kind whose four copies are not all held is added and tried.
        eastl::vector<Tile> ReferenceWinningTiles(const Sample& sample) {
            eastl::vector<Tile> tiles;
            if (TotalTiles(sample.counts) != kWinningHandTiles - 1 - sample.meldCount * kTilesPerSet) {
                return tiles;
            }
            TileCounts probe = sample.counts;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (sample.counts[id] + sample.meldTiles[id] >= kCopiesPerTile) {
                    continue;
                }
                ++probe[id];
                if (IsWinningShape(probe, sample.meldCount)) {
                    tiles.push_back(Tile{id});
                }
                --probe[id];
            }
            return tiles;
        }

        /// Builds the hands: every tile taken counts against the four copies of its kind (concealed tiles and melds together).
        class SampleBuilder {
        public:
            explicit SampleBuilder(uint32_t seed) : _rng(seed) {}

            uint32_t Below(uint32_t n) {
                return _rng.Below(n);
            }

            /// `meldCount` random melds (chow, pung or kong); false when the copies ran out.
            bool AddMelds(int meldCount) {
                _sample.meldCount = meldCount;
                for (int i = 0; i < meldCount; ++i) {
                    const uint32_t shape = Below(4);
                    if (shape == 0) {
                        if (!TakeMeld(RandomKind(), kCopiesPerTile)) {
                            return false;
                        }
                    } else if (shape == 1) {
                        if (!TakeMeld(RandomKind(), kTilesPerSet)) {
                            return false;
                        }
                    } else if (!TakeChow(true)) {
                        return false;
                    }
                }
                return true;
            }

            /// `count` random concealed tiles.
            bool AddRandomTiles(int count) {
                for (int i = 0; i < count; ++i) {
                    if (!Take(RandomKind(), false)) {
                        return false;
                    }
                }
                return true;
            }

            /// `sets` random sets (pungs and chows) and a pair, concealed, then one tile out: a hand that is often waiting.
            bool AddNearComplete(int sets) {
                for (int i = 0; i < sets; ++i) {
                    if (Below(3) == 0 ? !TakeSet(RandomKind(), kTilesPerSet) : !TakeChow(false)) {
                        return false;
                    }
                }
                return TakeSet(RandomKind(), kTilesInPair) && RemoveRandomTile();
            }

            /// Six pairs of one suit (or of honours, or of any kinds) and a seventh single tile, no melds.
            bool AddSevenPairsShape() {
                const uint32_t style = Below(3);
                const uint8_t suitBase = static_cast<uint8_t>(Below(kSuitCount) * kRanksPerSuit);
                for (int pair = 0; pair < kPairsInSevenPairs - 1; ++pair) {
                    uint8_t id = 0;
                    if (style == 0) {
                        id = static_cast<uint8_t>(suitBase + Below(kRanksPerSuit));
                    } else if (style == 1) {
                        id = static_cast<uint8_t>(kSuitTileCount + Below(kKindCount - kSuitTileCount));
                    } else {
                        id = RandomKind();
                    }
                    if (!TakeSet(id, kTilesInPair)) {
                        return false;
                    }
                }
                return Take(style == 2 ? RandomKind() : static_cast<uint8_t>(suitBase + Below(kRanksPerSuit)), false);
            }

            /// Thirteen orphans or near it: all 13 kinds once, or 12 kinds with one of them twice; one time in five a random tile is swapped in.
            bool AddOrphansShape() {
                const uint32_t skipped = Below(kOrphanKinds + 1);  // kOrphanKinds: none skipped
                int index = 0;
                eastl::vector<uint8_t> held;
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    const Tile tile{id};
                    if (!tile.IsTerminal() && !tile.IsHonour()) {
                        continue;
                    }
                    if (static_cast<uint32_t>(index++) != skipped && !Take(id, false)) {
                        return false;
                    }
                    if (static_cast<uint32_t>(index - 1) != skipped) {
                        held.push_back(id);
                    }
                }
                if (skipped < kOrphanKinds && !Take(held[Below(static_cast<uint32_t>(held.size()))], false)) {
                    return false;
                }
                return Below(5) != 0 || (RemoveRandomTile() && Take(RandomKind(), false));
            }

            const Sample& Get() const {
                return _sample;
            }

        private:
            uint8_t RandomKind() {
                return static_cast<uint8_t>(Below(kKindCount));
            }

            bool Take(uint8_t id, bool inMeld) {
                if (_used[id] >= kCopiesPerTile) {
                    return false;
                }
                ++_used[id];
                ++(inMeld ? _sample.meldTiles : _sample.counts)[id];
                return true;
            }

            bool TakeSet(uint8_t id, int copies) {
                for (int i = 0; i < copies; ++i) {
                    if (!Take(id, false)) {
                        return false;
                    }
                }
                return true;
            }

            bool TakeMeld(uint8_t id, int copies) {
                for (int i = 0; i < copies; ++i) {
                    if (!Take(id, true)) {
                        return false;
                    }
                }
                return true;
            }

            bool TakeChow(bool inMeld) {
                const uint8_t first = static_cast<uint8_t>(Below(kSuitCount) * kRanksPerSuit + Below(kLastChowStartRank));
                for (const Tile tile : ChowTiles(Tile{first})) {
                    if (!Take(tile.Id(), inMeld)) {
                        return false;
                    }
                }
                return true;
            }

            /// Takes one concealed tile out of the hand (it goes back into the pool).
            bool RemoveRandomTile() {
                int total = TotalTiles(_sample.counts);
                if (total == 0) {
                    return false;
                }
                int pick = static_cast<int>(Below(static_cast<uint32_t>(total)));
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    pick -= _sample.counts[id];
                    if (pick < 0) {
                        --_sample.counts[id];
                        --_used[id];
                        return true;
                    }
                }
                return false;
            }

            MahjongRng _rng;
            Sample _sample;
            TileCounts _used{};
        };

        /// `IsWaiting` and `WinningTiles` against the reference on one hand; returns whether the hand waits.
        bool ExpectSameAsReference(const Sample& sample) {
            const eastl::vector<Tile> expected = ReferenceWinningTiles(sample);
            EXPECT_EQ(WinningTiles(sample.counts, sample.meldCount, sample.meldTiles), expected) << "reference: " << Describe(expected);
            EXPECT_EQ(IsWaiting(sample.counts, sample.meldCount, sample.meldTiles), !expected.empty()) << "reference: " << Describe(expected);
            return !expected.empty();
        }

        struct Tally {
            int hands = 0;
            int waiting = 0;
            int notWaiting = 0;
            int deadKinds = 0;  ///< kinds that would complete the hand but for the four copies already held
        };

        /// Dead kinds of a hand: the kinds whose four copies are held (concealed and in melds) that would still complete it as a fifth.
        int CountDeadKinds(const Sample& sample) {
            TileCounts probe = sample.counts;
            int dead = 0;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (sample.counts[id] + sample.meldTiles[id] < kCopiesPerTile) {
                    continue;
                }
                ++probe[id];
                dead += IsWinningShape(probe, sample.meldCount) ? 1 : 0;
                --probe[id];
            }
            return dead;
        }

        /// `kSamplesPerKind` hands with `meldCount` melds, the concealed tiles added by `fill(builder, meldCount)` (a hand it cannot build,
        /// for the copies ran out, is skipped); each is checked against the reference.
        template <class Fill>
        Tally CheckHands(uint32_t seed, int meldCount, Fill fill) {
            Tally tally;
            for (int i = 0; i < kSamplesPerKind; ++i) {
                SampleBuilder builder(seed * 100003 + static_cast<uint32_t>(i));
                if (!builder.AddMelds(meldCount) || !fill(builder, meldCount)) {
                    continue;
                }
                const Sample& sample = builder.Get();
                ++tally.hands;
                (ExpectSameAsReference(sample) ? tally.waiting : tally.notWaiting)++;
                tally.deadKinds += CountDeadKinds(sample);
            }
            return tally;
        }

        int ConcealedFor(int meldCount) {
            return kWinningHandTiles - 1 - meldCount * kTilesPerSet;
        }

    }  // namespace

    TEST(HandWaitingTest, RandomHandsAgreeWithTheReference) {
        int notWaiting = 0;
        for (int meldCount = 0; meldCount <= kSetsInHand; ++meldCount) {
            const Tally tally = CheckHands(11 + meldCount, meldCount, [](SampleBuilder& builder, int melds) { return builder.AddRandomTiles(ConcealedFor(melds)); });
            EXPECT_GT(tally.hands, kSamplesPerKind / 2) << "melds " << meldCount;
            notWaiting += tally.notWaiting;
        }
        EXPECT_GT(notWaiting, kSamplesPerKind);
    }

    TEST(HandWaitingTest, NearCompleteHandsAgreeWithTheReference) {
        int waitingTotal = 0;
        int deadTotal = 0;
        for (int meldCount = 0; meldCount <= kSetsInHand; ++meldCount) {
            const Tally tally = CheckHands(31 + meldCount, meldCount, [](SampleBuilder& builder, int melds) { return builder.AddNearComplete(kSetsInHand - melds); });
            EXPECT_GT(tally.hands, kSamplesPerKind / 2) << "melds " << meldCount;
            EXPECT_GT(tally.waiting, tally.hands / 2) << "melds " << meldCount;  // the hands are built to be waiting
            waitingTotal += tally.waiting;
            deadTotal += tally.deadKinds;
        }
        EXPECT_GT(waitingTotal, kSamplesPerKind);
        EXPECT_GT(deadTotal, 0) << "no dead wait came up: the copy rule was not exercised";
    }

    TEST(HandWaitingTest, SevenPairsShapesAgreeWithTheReference) {
        const Tally tally = CheckHands(51, 0, [](SampleBuilder& builder, int) { return builder.AddSevenPairsShape(); });
        EXPECT_GT(tally.hands, kSamplesPerKind / 2);
        EXPECT_GT(tally.waiting, 0);
        EXPECT_GT(tally.notWaiting, 0);  // pairs of mixed suits do not win
    }

    TEST(HandWaitingTest, ThirteenOrphansShapesAgreeWithTheReference) {
        const Tally tally = CheckHands(61, 0, [](SampleBuilder& builder, int) { return builder.AddOrphansShape(); });
        EXPECT_GT(tally.hands, kSamplesPerKind / 2);
        EXPECT_GT(tally.waiting, 0);
    }

    TEST(HandWaitingTest, ADeadWaitIsNotOffered) {
        // Waits on 3p (1p 2p 3p, 1p 1p 1p, 2p 2p) and on 1p, but all four 1p are held: only 3p is offered.
        const TileCounts counts = Counts("1p 1p 1p 1p 2p 2p 2p 5s 6s 7s Rd Rd Rd");
        EXPECT_EQ(WinningTiles(counts, 0), WaitsOn("3p"));
        EXPECT_TRUE(IsWaiting(counts, 0, TileCounts{}));
    }

    TEST(HandWaitingTest, AWaitDeadByTheCopiesInTheMeldsIsNoWait) {
        // Three sets and a lone 5s wait on 5s (the pair); with a pung of 5s as the meld the fourth copy is the one held, so 5s is dead.
        const TileCounts tiles = Counts("1p 2p 3p 4p 5p 6p 7m 8m 9m 5s");
        EXPECT_TRUE(IsWaiting(tiles, 1, TileCounts{}));
        EXPECT_EQ(WinningTiles(tiles, 1, TileCounts{}), WaitsOn("5s"));
        const TileCounts pungOfFives = Counts("5s 5s 5s");
        EXPECT_FALSE(IsWaiting(tiles, 1, pungOfFives));
        EXPECT_TRUE(WinningTiles(tiles, 1, pungOfFives).empty());
    }

    TEST(HandWaitingTest, ThirteenOrphansWaitWithAKindTheHandDoesNotHold) {
        // Twelve different orphans and a pair: the missing 9m is not next to any held tile but completes the hand.
        const TileCounts counts = Counts("1p 1p 9p 1s 9s 1m Ew Sw Ww Nw Rd Wd Gd");
        EXPECT_TRUE(IsWaiting(counts, 0, TileCounts{}));
        EXPECT_EQ(WinningTiles(counts, 0), WaitsOn("9m"));
    }

    TEST(HandWaitingTest, AWrongTileCountOrMeldCountIsNeverWaiting) {
        EXPECT_FALSE(IsWaiting(Counts("1p 2p 3p"), 0, TileCounts{}));
        EXPECT_FALSE(IsWaiting(Counts("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s 5s"), 0, TileCounts{}));
        EXPECT_FALSE(IsWaiting(Counts("1p"), 5, TileCounts{}));
        EXPECT_FALSE(IsWaiting(Counts("1p"), -1, TileCounts{}));
        EXPECT_TRUE(IsWaiting(Counts("1p"), 4, TileCounts{}));
    }

}  // namespace BECore::Mahjong::Tests
