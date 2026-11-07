#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * The hand shape: which splits into sets and a pair a hand has, which hands win, which tiles complete a waiting hand. Pure logic: no
 * engine services. Hands are written in the tile notation of `Tile.h`; waits are written as the notation of the tiles in id order.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kNoMelds = 0;
        constexpr int kOneMeld = 1;
        constexpr int kTwoMelds = 2;
        constexpr int kThreeMelds = 3;
        constexpr int kFourMelds = kSetsInHand;

        TileCounts Counts(const std::string& text) {
            return CountTiles(ParseTiles(text).value());
        }

        /// "<pair>: <set> <set> ...", a chow as C<first> and a pung as P<first> (the order `Decompose` returns).
        std::string Describe(const Decomposition& decomposition) {
            std::string text{ToString(decomposition.pair)};
            text += ":";
            for (const ShapeSet& set : decomposition.sets) {
                text += set.kind == MeldKind::Chow ? " C" : " P";
                text += ToString(set.first);
            }
            return text;
        }

        std::vector<std::string> DescribeAll(const eastl::vector<Decomposition>& decompositions) {
            std::vector<std::string> texts;
            for (const Decomposition& decomposition : decompositions) {
                texts.push_back(Describe(decomposition));
            }
            std::sort(texts.begin(), texts.end());
            return texts;
        }

        /// The winning tiles as the notation separated by spaces.
        std::string Waits(const std::string& hand, int meldCount = kNoMelds) {
            std::string text;
            for (const Tile tile : WinningTiles(Counts(hand), meldCount)) {
                text += text.empty() ? "" : " ";
                text += ToString(tile);
            }
            return text;
        }

        bool Wins(const std::string& hand, int meldCount = kNoMelds) {
            return IsWinningShape(Counts(hand), meldCount);
        }

        constexpr const char* kPlainWin = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd";

    }  // namespace

    TEST(HandShapeTest, CountTilesCountsPlayableKindsOnly) {
        const TileCounts counts = CountTiles(ParseTiles("1p 1p Ew F1 T2 Gd Gd Gd").value());

        EXPECT_EQ(counts[Tile::Suited(TileSuit::Dots, 1).Id()], 2);
        EXPECT_EQ(counts[Tile::FromWind(0).Id()], 1);
        EXPECT_EQ(counts[Tile::FromDragon(2).Id()], 3);
        EXPECT_EQ(TotalTiles(counts), 6);
    }

    TEST(HandShapeTest, PlainHandOfFourSetsAndAPairWins) {
        EXPECT_TRUE(Wins(kPlainWin));

        const eastl::vector<Decomposition> found = Decompose(Counts(kPlainWin), kSetsInHand);
        ASSERT_EQ(found.size(), 1u);
        EXPECT_EQ(Describe(found[0]), "Rd: C1p C4s C7m PEw");
    }

    TEST(HandShapeTest, SetsOfAllSuitsAndHonoursAreFound) {
        const eastl::vector<Decomposition> found = Decompose(Counts("1p 1p 1p 4s 5s 6s 9m 9m 9m Sw Sw Sw Gd Gd"), kSetsInHand);

        ASSERT_EQ(found.size(), 1u);
        EXPECT_EQ(found[0].pair, Tile::FromDragon(2));
        ASSERT_EQ(found[0].sets.size(), static_cast<size_t>(kSetsInHand));
        EXPECT_EQ(found[0].sets[0], (ShapeSet{MeldKind::Pung, Tile::Suited(TileSuit::Dots, 1)}));
        EXPECT_EQ(found[0].sets[1], (ShapeSet{MeldKind::Chow, Tile::Suited(TileSuit::Bamboo, 4)}));
        EXPECT_EQ(found[0].sets[2], (ShapeSet{MeldKind::Pung, Tile::Suited(TileSuit::Characters, 9)}));
        EXPECT_EQ(found[0].sets[3], (ShapeSet{MeldKind::Pung, Tile::FromWind(1)}));
    }

    TEST(HandShapeTest, HandThatIsNotFourSetsAndAPairDoesNotWin) {
        EXPECT_FALSE(Wins("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Wd"));
        EXPECT_FALSE(Wins("1p 2p 4p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"));
        EXPECT_TRUE(Decompose(Counts("1p 2p 4p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"), kSetsInHand).empty());
    }

    TEST(HandShapeTest, ChowsNeverWrapAroundTheSuit) {
        // 8m 9m 1p and 9p 1s 2s are not sequences even though the ids run on.
        EXPECT_FALSE(Wins("8m 9m 1p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"));
        EXPECT_FALSE(Wins("9p 1s 2s 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"));
        EXPECT_FALSE(Wins("8p 9p 1s 2s 3s 4s 7m 8m 9m Ew Ew Ew Rd Rd"));
    }

    TEST(HandShapeTest, HonoursNeverFormAChow) {
        EXPECT_FALSE(Wins("Ew Sw Ww 4s 5s 6s 7m 8m 9m 1p 2p 3p Rd Rd"));
        EXPECT_FALSE(Wins("Rd Wd Gd 4s 5s 6s 7m 8m 9m 1p 2p 3p Ew Ew"));
        EXPECT_FALSE(Wins("Ww Nw Rd 4s 5s 6s 7m 8m 9m 1p 2p 3p Ew Ew"));
    }

    TEST(HandShapeTest, OneHandWithTwoSplitsGivesBoth) {
        // Three pungs of 1p 2p 3p or three chows 1p 2p 3p.
        const TileCounts counts = Counts("1p 1p 1p 2p 2p 2p 3p 3p 3p 4s 5s 6s Ew Ew");

        EXPECT_EQ(DescribeAll(Decompose(counts, kSetsInHand)), (std::vector<std::string>{"Ew: C1p C1p C1p C4s", "Ew: P1p P2p P3p C4s"}));
    }

    TEST(HandShapeTest, SplitsWithDifferentPairsAreAllReturned) {
        // Pair 4p with three pungs or three chows, or pair 1p with the chows 1p 2p 3p, 2p 3p 4p, 2p 3p 4p.
        const TileCounts counts = Counts("1p 1p 1p 2p 2p 2p 3p 3p 3p 4p 4p");

        EXPECT_EQ(DescribeAll(Decompose(counts, kSetsInHand - kOneMeld)), (std::vector<std::string>{"1p: C1p C2p C2p", "4p: C1p C1p C1p", "4p: P1p P2p P3p"}));
    }

    TEST(HandShapeTest, EachSplitIsReturnedOnce) {
        // 1p x4 is a pung and a chow with 2p 3p; the two orders of taking them are one split.
        const eastl::vector<Decomposition> found = Decompose(Counts("1p 1p 1p 1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew"), kSetsInHand);

        ASSERT_EQ(found.size(), 1u);
        EXPECT_EQ(Describe(found[0]), "Ew: P1p C1p C4s C7m");
    }

    TEST(HandShapeTest, DecomposeRefusesAWrongTileCount) {
        EXPECT_TRUE(Decompose(Counts("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd"), kSetsInHand).empty());
        EXPECT_TRUE(Decompose(Counts(kPlainWin), kSetsInHand - kOneMeld).empty());
        EXPECT_TRUE(Decompose(Counts(kPlainWin), kSetsInHand + 1).empty());
        EXPECT_TRUE(Decompose(Counts(kPlainWin), -1).empty());
    }

    TEST(HandShapeTest, OneSidedWait) {
        EXPECT_EQ(Waits("1p 2p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"), "3p");
        EXPECT_EQ(Waits("8p 9p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd Rd"), "7p");
    }

    TEST(HandShapeTest, TwoSidedWait) {
        EXPECT_EQ(Waits("4p 5p 1s 2s 3s 7m 8m 9m Ew Ew Ew Rd Rd"), "3p 6p");
    }

    TEST(HandShapeTest, MiddleWait) {
        EXPECT_EQ(Waits("4p 6p 1s 2s 3s 7m 8m 9m Ew Ew Ew Rd Rd"), "5p");
    }

    TEST(HandShapeTest, PairWait) {
        EXPECT_EQ(Waits("Ew Ew Rd Rd 1p 2p 3p 4s 5s 6s 7m 8m 9m"), "Ew Rd");
        EXPECT_EQ(Waits("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd"), "Rd");
    }

    TEST(HandShapeTest, WaitOfAPungAndATile) {
        // 1p 1p 1p 2p waits on 2p (pair) and 3p (chow 1p 2p 3p with the pair 1p 1p).
        EXPECT_EQ(Waits("1p 1p 1p 2p 4s 5s 6s 7m 8m 9m Ew Ew Ew"), "2p 3p");
    }

    TEST(HandShapeTest, NineGatesWaitsOnEveryRank) {
        EXPECT_EQ(Waits("1p 1p 1p 2p 3p 4p 5p 6p 7p 8p 9p 9p 9p"), "1p 2p 3p 4p 5p 6p 7p 8p 9p");
    }

    TEST(HandShapeTest, ATileHeldFourTimesIsNotOffered) {
        // 1p 1p 1p is a pung and the fourth 1p would be the pair partner, but all four are already in the hand.
        EXPECT_EQ(Waits("1p 1p 1p 1p 4m 5m 6m 7s 8s 9s Ew Ew Ew"), "");
    }

    TEST(HandShapeTest, HandThatIsNotWaitingOrHasAWrongSizeOffersNothing) {
        EXPECT_EQ(Waits("1p 3p 5p 7p 9p 2s 4s 6s 8s 1m 3m 5m 7m"), "");
        EXPECT_EQ(Waits(kPlainWin), "");
        EXPECT_EQ(Waits("1p 2p 3p"), "");
    }

    TEST(HandShapeTest, MeldsReduceTheSetsNeeded) {
        EXPECT_TRUE(Wins("1p 2p 3p 4s 5s 6s Ew Ew Ew Rd Rd", kOneMeld));
        EXPECT_TRUE(Wins("1p 2p 3p 4s 5s 6s Rd Rd", kTwoMelds));
        EXPECT_TRUE(Wins("1p 2p 3p Rd Rd", kThreeMelds));
        EXPECT_TRUE(Wins("Rd Rd", kFourMelds));

        // The same tiles with the wrong number of melds are not a win.
        EXPECT_FALSE(Wins("1p 2p 3p 4s 5s 6s Ew Ew Ew Rd Rd", kNoMelds));
        EXPECT_FALSE(Wins("1p 2p 3p 4s 5s 6s Ew Ew Ew Rd Rd", kTwoMelds));
        EXPECT_FALSE(Wins("Rd Rd", kThreeMelds));
        EXPECT_FALSE(Wins("Rd Wd", kFourMelds));
        EXPECT_FALSE(Wins("Rd Rd", kFourMelds + 1));
        EXPECT_FALSE(Wins("Rd Rd", -1));
    }

    TEST(HandShapeTest, WaitsWithMelds) {
        EXPECT_EQ(Waits("1p 2p 3p 4s 5s 6s Ew Ew Ew Rd", kOneMeld), "Rd");
        EXPECT_EQ(Waits("1p 2p 3p 4s 5s Rd Rd", kTwoMelds), "3s 6s");
        EXPECT_EQ(Waits("4p 5p Rd Rd", kThreeMelds), "3p 6p");
        EXPECT_EQ(Waits("Rd", kFourMelds), "Rd");
        EXPECT_EQ(Waits("Rd", kThreeMelds), "");
    }

    TEST(HandShapeTest, FourMeldsLeaveALonePairToDecompose) {
        const eastl::vector<Decomposition> found = Decompose(Counts("Rd Rd"), 0);

        ASSERT_EQ(found.size(), 1u);
        EXPECT_EQ(found[0].pair, Tile::FromDragon(0));
        EXPECT_TRUE(found[0].sets.empty());
    }

    TEST(HandShapeTest, ThirteenOrphansWithARepeatedTile) {
        EXPECT_TRUE(Wins("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd Rd"));
        EXPECT_TRUE(Wins("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd 1p"));
        EXPECT_TRUE(IsThirteenOrphans(Counts("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd Gd")));
    }

    TEST(HandShapeTest, ThirteenOrphansNeedsEveryOrphanAndNothingElse) {
        EXPECT_FALSE(Wins("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Rd Rd"));
        EXPECT_FALSE(Wins("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd 2p"));
        EXPECT_FALSE(Wins("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd Rd", kOneMeld));
    }

    TEST(HandShapeTest, ThirteenOrphansWaitOnEveryOrphanWhenAllAreHeldOnce) {
        EXPECT_EQ(Waits("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd"), "1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd");
    }

    TEST(HandShapeTest, ThirteenOrphansWaitOnTheMissingOrphanWhenOneIsPaired) {
        EXPECT_EQ(Waits("1p 1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd"), "Gd");
    }

    TEST(HandShapeTest, SevenPairsOfOneSuitWin) {
        EXPECT_TRUE(Wins("1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2p 2p 4p 4p"));
        EXPECT_TRUE(IsSevenPairs(Counts("1s 1s 2s 2s 3s 3s 4s 4s 5s 5s 6s 6s 8s 8s")));
    }

    TEST(HandShapeTest, SevenPairsOfHonoursWin) {
        EXPECT_TRUE(Wins("Ew Ew Sw Sw Ww Ww Nw Nw Rd Rd Wd Wd Gd Gd"));
    }

    TEST(HandShapeTest, SevenPairsOfMixedKindsDoNotWin) {
        EXPECT_FALSE(IsSevenPairs(Counts("1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2s 2s 4s 4s")));
        EXPECT_FALSE(Wins("1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2s 2s 4s 4s"));
        EXPECT_FALSE(IsSevenPairs(Counts("1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2p 2p Ew Ew")));
        EXPECT_FALSE(Wins("1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2p 2p Ew Ew"));
    }

    TEST(HandShapeTest, SevenPairsWithAFourOfAKindDoNotWin) {
        EXPECT_FALSE(IsSevenPairs(Counts("1p 1p 1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2p 2p")));
        EXPECT_FALSE(Wins("1p 1p 1p 1p 3p 3p 5p 5p 7p 7p 9p 9p 2p 2p"));
    }

    TEST(HandShapeTest, SevenPairsOnlyWinWithoutMelds) {
        EXPECT_FALSE(Wins("Ew Ew Sw Sw Ww Ww Nw Nw Rd Rd Wd Wd Gd Gd", kOneMeld));
    }

    TEST(HandShapeTest, SixPairsAndATileWaitOnTheSingleOne) {
        EXPECT_EQ(Waits("Ew Ew Sw Sw Ww Ww Nw Nw Rd Rd Wd Wd Gd"), "Gd");
    }

    TEST(HandShapeTest, TwoFoursOfAKindGiveBothSplits) {
        // 1p 2p 3p four times each: four chows, or three pungs and one chow.
        const TileCounts counts = Counts("1p 1p 1p 1p 2p 2p 2p 2p 3p 3p 3p 3p Ew Ew");

        EXPECT_EQ(DescribeAll(Decompose(counts, kSetsInHand)), (std::vector<std::string>{"Ew: C1p C1p C1p C1p", "Ew: P1p C1p P2p P3p"}));
    }

    TEST(HandShapeTest, SevenPairsAndThirteenOrphansWinWithoutADecomposition) {
        const char* const sevenPairs = "Ew Ew Sw Sw Ww Ww Nw Nw Rd Rd Wd Wd Gd Gd";
        const char* const thirteenOrphans = "1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd Rd";

        EXPECT_TRUE(Wins(sevenPairs));
        EXPECT_TRUE(Decompose(Counts(sevenPairs), kSetsInHand).empty());
        EXPECT_TRUE(Wins(thirteenOrphans));
        EXPECT_TRUE(Decompose(Counts(thirteenOrphans), kSetsInHand).empty());
    }

    TEST(HandShapeTest, CountMeldTilesCountsEveryTileOfEveryMeld) {
        eastl::vector<Meld> melds;
        melds.push_back({MeldKind::Chow, false, Tile::Suited(TileSuit::Dots, 7)});
        melds.push_back({MeldKind::Pung, false, Tile::FromWind(0)});
        melds.push_back({MeldKind::Kong, true, Tile::FromDragon(0)});
        const TileCounts counts = CountMeldTiles(melds);

        EXPECT_EQ(TotalTiles(counts), kTilesPerSet + kTilesPerSet + kCopiesPerTile);
        EXPECT_EQ(counts[Tile::Suited(TileSuit::Dots, 7).Id()], 1);
        EXPECT_EQ(counts[Tile::Suited(TileSuit::Dots, 8).Id()], 1);
        EXPECT_EQ(counts[Tile::Suited(TileSuit::Dots, 9).Id()], 1);
        EXPECT_EQ(counts[Tile::FromWind(0).Id()], kTilesPerSet);
        EXPECT_EQ(counts[Tile::FromDragon(0).Id()], kCopiesPerTile);
    }

    TEST(HandShapeTest, TheCopiesInMeldsAreNotOfferedAsAFifthCopy) {
        // An open pung of 5p, and the hand waits on 5p for its pair: the fourth copy is in the hand, the fifth does not exist.
        PlayerHand hand;
        hand.melds.push_back({MeldKind::Pung, false, Tile::Suited(TileSuit::Dots, 5)});
        const eastl::vector<Tile> concealed = ParseTiles("5p 1s 2s 3s 4s 5s 6s Ew Ew Ew").value();
        for (const Tile tile : concealed) {
            hand.AddConcealed(tile);
        }
        const TileCounts counts = CountTiles(hand.concealed);

        EXPECT_EQ(WinningTiles(counts, kOneMeld).size(), 1u);  // without the meld tiles the fifth copy would be offered
        EXPECT_TRUE(WinningTiles(counts, kOneMeld, CountMeldTiles(hand.melds)).empty());
        EXPECT_TRUE(WinningTiles(hand).empty());
    }

    TEST(HandShapeTest, TheSeatOverloadOffersTheOtherWaits) {
        PlayerHand hand;
        hand.melds.push_back({MeldKind::Pung, false, Tile::Suited(TileSuit::Dots, 5)});
        const eastl::vector<Tile> concealed = ParseTiles("1s 2s 4s 5s 6s Ew Ew Ew 7m 7m").value();
        for (const Tile tile : concealed) {
            hand.AddConcealed(tile);
        }
        const eastl::vector<Tile> waits = WinningTiles(hand);

        ASSERT_EQ(waits.size(), 1u);
        EXPECT_EQ(waits[0], Tile::Suited(TileSuit::Bamboo, 3));
    }

}  // namespace BECore::Mahjong::Tests
