#include "ScoreTestSupport.h"

/**
 * The doubles for everyone, the total of a score and the readings (`ScoreSeat`): pungs and kongs of dragons and winds, one suit,
 * flowers, `points << doubles` with the limit and the rounding, and the choice of the best reading of a hand with several. Pure
 * logic: no engine services. The expected numbers are the table of the rules, in `ScoreTestSupport.h`; the winner's lines are in
 * `ScoreWinnerTest`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Four concealed kongs of honours: 4 x 32 points, two doubles for East's wind and three for the dragons.
        constexpr int kFourKongsPoints = 4 * kHonourKongConcealed;
        constexpr int kFourKongsDoubles = 5;
        constexpr int kFourKongsRaw = 4096;
        /// Flowers of the number of East for East in the East round: 4 points and 2 doubles each.
        constexpr int kManyFlowers = 40;
        constexpr int kManyFlowersDoubles = 80;
        /// A limit that is not a multiple of 10 caps at the multiple below it.
        constexpr int kOddLimit = 495;
        constexpr int kOddLimitCap = 490;
        constexpr int kLimitBelowTen = 9;
        constexpr int kStepOfTen = 10;
        /// The largest multiple of 10 an `int` holds.
        constexpr int kLargestRoundedScore = 2147483640;

        // The hands of the readings: three pungs or three chows of 1p 2p 3p, a chow of bamboo, a pair of dragons.
        constexpr const char* kPungsOrChows = "1p 1p 1p 2p 2p 2p 3p 3p 3p 4s 5s 6s Rd Rd";
        // One suit of dots: all nine ranks and two more tiles.
        constexpr const char* kDots = "1p 2p 3p 4p 5p 6p 7p 8p 9p 2p 3p";

        ScoreBreakdown Score(const std::string& tiles, Wind seatWind = Wind::East, Wind roundWind = Wind::East) {
            return ScoreSeat(MakeInput(std::string(kQuietChows) + " " + tiles, {}, "", seatWind, roundWind));
        }

        ScoreBreakdown ScoreWithMelds(std::initializer_list<Meld> melds, Wind seatWind = Wind::East, Wind roundWind = Wind::East) {
            return ScoreSeat(MakeInput(kQuietChows, melds, "", seatWind, roundWind));
        }

        ScoreBreakdown ScoreBonus(const char* bonus, Wind seatWind, Wind roundWind) {
            return ScoreSeat(MakeInput(kQuietChows, {}, bonus, seatWind, roundWind));
        }

    }  // namespace

    // ----- pungs and kongs of dragons and winds -----

    TEST(ScoreDoublesTest, PungAndKongOfDragonsDoubleOnce) {
        EXPECT_EQ(Score("Rd Rd Rd").doubles, kOneDouble);
        EXPECT_EQ(LineDoubles(Score("Wd Wd Wd"), ScoreKind::DragonSet), kOneDouble);
        EXPECT_EQ(ScoreWithMelds({OpenPung(T("Gd"))}).doubles, kOneDouble);
        EXPECT_EQ(ScoreWithMelds({OpenKong(T("Gd"))}).doubles, kOneDouble);
        EXPECT_EQ(ScoreWithMelds({ConcealedKong(T("Rd"))}).doubles, kOneDouble);
    }

    TEST(ScoreDoublesTest, EachSetOfDragonsDoublesOnce) {
        const ScoreBreakdown breakdown = ScoreWithMelds({OpenPung(T("Rd")), OpenPung(T("Wd")), OpenKong(T("Gd"))});

        EXPECT_EQ(breakdown.doubles, 3 * kOneDouble);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::DragonSet), 3);
    }

    TEST(ScoreDoublesTest, PairOfDragonsDoesNotDouble) {
        EXPECT_EQ(Score("Rd Rd").doubles, 0);
    }

    TEST(ScoreDoublesTest, PungOfTheOwnWindDoublesOnce) {
        const ScoreBreakdown breakdown = Score("Sw Sw Sw", Wind::South);

        EXPECT_EQ(breakdown.doubles, kOneDouble);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::OwnWindSet), kOneDouble);
        EXPECT_EQ(ScoreWithMelds({OpenKong(T("Sw"))}, Wind::South).doubles, kOneDouble);
    }

    TEST(ScoreDoublesTest, PungOfTheRoundWindDoublesOnce) {
        const ScoreBreakdown breakdown = Score("Sw Sw Sw", Wind::West, Wind::South);

        EXPECT_EQ(breakdown.doubles, kOneDouble);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::RoundWindSet), kOneDouble);
    }

    TEST(ScoreDoublesTest, PungOfTheOwnAndTheRoundWindAtOnceDoublesTwice) {
        // East in the East round with a pung of East: two doubles, two lines.
        const ScoreBreakdown breakdown = Score("Ew Ew Ew");

        EXPECT_EQ(breakdown.doubles, kTwoDoubles);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::OwnWindSet), 1);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::RoundWindSet), 1);
        EXPECT_EQ(Score("Sw Sw Sw", Wind::South, Wind::South).doubles, kTwoDoubles);
    }

    TEST(ScoreDoublesTest, PungOfAnotherSeatsWindDoesNotDouble) {
        EXPECT_EQ(Score("Ww Ww Ww").doubles, 0);
        EXPECT_EQ(Score("Ew Ew Ew", Wind::South, Wind::South).doubles, 0);
        EXPECT_EQ(ScoreWithMelds({OpenPung(T("Nw"))}, Wind::South, Wind::West).doubles, 0);
    }

    // ----- one suit -----

    TEST(ScoreDoublesTest, OneSuitWithHonoursDoublesOnce) {
        const ScoreBreakdown breakdown = ScoreSeat(MakeInput(std::string(kDots) + " Ww Ww"));

        EXPECT_EQ(breakdown.doubles, kOneDouble);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::OneSuitWithHonours), kOneDouble);
    }

    TEST(ScoreDoublesTest, OneSuitWithoutHonoursDoublesThrice) {
        const ScoreBreakdown breakdown = ScoreSeat(MakeInput(kDots));

        EXPECT_EQ(breakdown.doubles, kOneSuitNoHonoursDoubles);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::OneSuitNoHonours), kOneSuitNoHonoursDoubles);
    }

    TEST(ScoreDoublesTest, TheTilesOfTheMeldsCountForTheSuit) {
        // Concealed dots: a meld of dots keeps the suit, a meld of another suit spoils it, an honour makes it "with honours".
        EXPECT_EQ(ScoreSeat(MakeInput("1p 2p 3p", {OpenPung(T("5p")), OpenChow(T("7p"))})).doubles, kOneSuitNoHonoursDoubles);
        EXPECT_EQ(ScoreSeat(MakeInput("1p 2p 3p", {OpenPung(T("5p")), OpenChow(T("7s"))})).doubles, 0);
        const ScoreBreakdown withHonour = ScoreSeat(MakeInput("1p 2p 3p", {OpenPung(T("5p")), OpenPung(T("Ww"))}));
        EXPECT_EQ(LineDoubles(withHonour, ScoreKind::OneSuitWithHonours), kOneDouble);
        // A hand of melds only.
        EXPECT_EQ(ScoreSeat(MakeInput("", {OpenChow(T("1s")), OpenPung(T("9s"))})).doubles, kOneSuitNoHonoursDoubles);
    }

    TEST(ScoreDoublesTest, TwoSuitsAndHonoursOnlyDoNotDouble) {
        const ScoreBreakdown twoSuits = ScoreSeat(MakeInput(kQuietChows));
        const ScoreBreakdown honours = ScoreSeat(MakeInput("Ww Ww Ww Nw Nw Nw Gd Gd"));

        EXPECT_EQ(twoSuits.doubles, 0);
        EXPECT_EQ(CountLines(honours, ScoreKind::OneSuitWithHonours), 0);
        EXPECT_EQ(CountLines(honours, ScoreKind::OneSuitNoHonours), 0);
    }

    TEST(ScoreDoublesTest, BonusTilesAreNotLookedAtForTheSuit) {
        EXPECT_EQ(ScoreSeat(MakeInput(kDots, {}, "F3")).doubles, kOneSuitNoHonoursDoubles);
    }

    TEST(ScoreDoublesTest, ASeatThatDoesNotWinGetsThemFromItsThirteenTiles) {
        const ScoreBreakdown breakdown = ScoreSeat(MakeInput("1p 2p 3p 4p 5p 6p 7p 8p 9p 1p 2p 3p Ww"));

        EXPECT_FALSE(breakdown.won);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::OneSuitWithHonours), kOneDouble);
    }

    // ----- flowers -----

    TEST(ScoreDoublesTest, FlowerOfTheOwnNumberDoubles) {
        // Flower or season 1 = East ... 4 = North.
        const ScoreBreakdown flower = ScoreBonus("F2", Wind::South, Wind::East);
        const ScoreBreakdown season = ScoreBonus("T4", Wind::North, Wind::East);

        EXPECT_EQ(LineDoubles(flower, ScoreKind::OwnBonus), kOneDouble);
        EXPECT_EQ(flower.doubles, kOneDouble);
        EXPECT_EQ(flower.points, kBonusTile);
        EXPECT_EQ(LineDoubles(season, ScoreKind::OwnBonus), kOneDouble);
    }

    TEST(ScoreDoublesTest, FlowerOfTheRoundNumberDoubles) {
        const ScoreBreakdown breakdown = ScoreBonus("T3", Wind::East, Wind::West);

        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::RoundBonus), kOneDouble);
        EXPECT_EQ(breakdown.doubles, kOneDouble);
    }

    TEST(ScoreDoublesTest, FlowerOfTheSameNumberForOwnAndRoundDoublesTwice) {
        const ScoreBreakdown breakdown = ScoreBonus("F2", Wind::South, Wind::South);

        EXPECT_EQ(breakdown.doubles, kTwoDoubles);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::OwnBonus), 1);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::RoundBonus), 1);
    }

    TEST(ScoreDoublesTest, EachMatchingFlowerDoubles) {
        // The flower and the season of the own number double each.
        EXPECT_EQ(ScoreBonus("F2 T2", Wind::South, Wind::East).doubles, kTwoDoubles);
        EXPECT_EQ(ScoreBonus("F2 T2", Wind::South, Wind::South).doubles, 2 * kTwoDoubles);
    }

    TEST(ScoreDoublesTest, FlowerOfAnotherNumberDoesNotDouble) {
        const ScoreBreakdown breakdown = ScoreBonus("F3 T4", Wind::South, Wind::East);

        EXPECT_EQ(breakdown.doubles, 0);
        EXPECT_EQ(breakdown.points, 2 * kBonusTile);
    }

    // ----- the total -----

    TEST(ScoreDoublesTest, RawIsPointsShiftedByTheDoubles) {
        // A concealed pung of East for East in the East round: 8 points, two doubles.
        const ScoreBreakdown breakdown = Score("Ew Ew Ew");

        EXPECT_EQ(breakdown.points, kHonourPungConcealed);
        EXPECT_EQ(breakdown.doubles, kTwoDoubles);
        EXPECT_EQ(breakdown.raw, kHonourPungConcealed << kTwoDoubles);
        EXPECT_EQ(breakdown.score, RoundUpToTen(breakdown.raw));
    }

    TEST(ScoreDoublesTest, ScoreIsRoundedUpToTen) {
        // A concealed pung of simples and a pair of dragons: 6 points, no doubles.
        const ScoreBreakdown breakdown = Score("5p 5p 5p Rd Rd");

        EXPECT_EQ(breakdown.points, kSimplePungConcealed + kScoringPair);
        EXPECT_EQ(breakdown.raw, breakdown.points);
        EXPECT_EQ(breakdown.score, 10);
    }

    TEST(ScoreDoublesTest, TheLimitCapsTheScore) {
        const auto kongs = {ConcealedKong(T("Ew")), ConcealedKong(T("Rd")), ConcealedKong(T("Wd")), ConcealedKong(T("Gd"))};
        const ScoreBreakdown breakdown = ScoreSeat(MakeInput("", kongs));

        EXPECT_EQ(breakdown.points, kFourKongsPoints);
        EXPECT_EQ(breakdown.doubles, kFourKongsDoubles);
        EXPECT_EQ(breakdown.raw, kFourKongsRaw);
        EXPECT_EQ(breakdown.score, kLimit);

        ScoreInput lower = MakeInput("", kongs);
        lower.limit = kLowerLimit;
        EXPECT_EQ(ScoreSeat(lower).score, kLowerLimit);
        EXPECT_EQ(ScoreSeat(lower).raw, kFourKongsRaw);
    }

    TEST(ScoreDoublesTest, ALimitThatIsNotAMultipleOfTenCapsDownToOne) {
        ScoreInput input = MakeInput("", {ConcealedKong(T("Ew")), ConcealedKong(T("Rd")), ConcealedKong(T("Wd")), ConcealedKong(T("Gd"))});
        input.limit = kOddLimit;

        EXPECT_EQ(ScoreSeat(input).score, kOddLimitCap);
        static_assert(LimitCap(kOddLimit) == kOddLimitCap);
    }

    TEST(ScoreDoublesTest, ALimitBelowTenOrBelowZeroScoresNothing) {
        ScoreInput input = MakeInput(std::string(kQuietChows) + " 5p 5p 5p");
        for (const int limit : {kLimitBelowTen, 0, -kLowerLimit}) {
            input.limit = limit;
            EXPECT_EQ(ScoreSeat(input).score, 0) << limit;
        }
    }

    TEST(ScoreDoublesTest, ALimitOfIntMaxDoesNotOverflow) {
        std::string flowers = "F1";
        for (int count = 1; count < kManyFlowers; ++count) {
            flowers += " F1";
        }
        ScoreInput input = MakeInput(kQuietChows, {}, flowers);
        input.limit = std::numeric_limits<int>::max();
        const ScoreBreakdown breakdown = ScoreSeat(input);

        EXPECT_EQ(breakdown.raw, std::numeric_limits<int>::max());
        EXPECT_EQ(breakdown.score, kLargestRoundedScore);
        EXPECT_EQ(breakdown.score % kStepOfTen, 0);
    }

    TEST(ScoreDoublesTest, TheRawScoreSaturatesAndTheLimitStillCaps) {
        std::string flowers = "F1";
        for (int count = 1; count < kManyFlowers; ++count) {
            flowers += " F1";
        }
        const ScoreBreakdown breakdown = ScoreSeat(MakeInput(kQuietChows, {}, flowers));

        EXPECT_EQ(breakdown.points, kManyFlowers * kBonusTile);
        EXPECT_EQ(breakdown.doubles, kManyFlowersDoubles);
        EXPECT_EQ(breakdown.raw, std::numeric_limits<int>::max());
        EXPECT_EQ(breakdown.score, kLimit);
    }

    TEST(ScoreDoublesTest, RoundUpToTen) {
        EXPECT_EQ(RoundUpToTen(0), 0);
        EXPECT_EQ(RoundUpToTen(1), 10);
        EXPECT_EQ(RoundUpToTen(10), 10);
        EXPECT_EQ(RoundUpToTen(11), 20);
        EXPECT_EQ(RoundUpToTen(495), 500);
        EXPECT_EQ(RoundUpToTen(500), 500);
        static_assert(RoundUpToTen(31) == 40);
    }

    // ----- the readings -----

    TEST(ScoreDoublesTest, AHandWithTwoDecompositionsScoresTheBetterOne) {
        // 1p 1p 1p 2p 2p 2p 3p 3p 3p is three pungs or three chows 1p 2p 3p; the pungs score (8 + 4 + 4), the chows nothing.
        const ScoreBreakdown breakdown = ScoreSeat(MakeWinnerInput(kPungsOrChows, "Rd"));

        EXPECT_TRUE(breakdown.won);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pung), kHonourPungConcealed + 2 * kSimplePungConcealed);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::Pung), 3);
    }

    TEST(ScoreDoublesTest, AWinnersPairOfDragonsIsFound) {
        const ScoreBreakdown breakdown = ScoreSeat(MakeWinnerInput("4s 5s 6s 7m 8m 9m 2m 2m 2m 1p 2p 3p Rd Rd", "Rd"));

        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pair), kScoringPair);
        EXPECT_EQ(breakdown.points, kWinning + kWallTile + kOnlyTile + kSimplePungConcealed + kScoringPair);
    }

    TEST(ScoreDoublesTest, AWinnersMeldsAreSetsAndTheRestIsDecomposed) {
        // An open pung of the red dragon and 11 concealed tiles.
        const ScoreInput input = MakeWinnerInput("4s 5s 6s 7m 8m 9m 2p 3p 4p Sw Sw", "4p", WinSource::SelfDraw, {OpenPung(T("Rd"))});
        const ScoreBreakdown breakdown = ScoreSeat(input);

        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pung), kHonourPungOpen);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::DragonSet), kOneDouble);
    }

    TEST(ScoreDoublesTest, AWinningTileFromADiscardOpensTheSetThatHoldsIt) {
        // 1p 1p 1p completed by a discard of 1p is a claimed pung (4), not a concealed one (8); the pungs still beat the chows.
        const ScoreBreakdown byDiscard = ScoreSeat(MakeWinnerInput(kPungsOrChows, "1p", WinSource::Discard));
        const ScoreBreakdown selfDrawn = ScoreSeat(MakeWinnerInput(kPungsOrChows, "1p", WinSource::SelfDraw));

        EXPECT_EQ(LinePoints(byDiscard, ScoreKind::Pung), kHonourPungOpen + 2 * kSimplePungConcealed);
        EXPECT_EQ(LinePoints(selfDrawn, ScoreKind::Pung), kHonourPungConcealed + 2 * kSimplePungConcealed);
    }

    TEST(ScoreDoublesTest, ARobbedKongOpensTheSetThatHoldsTheTileToo) {
        const ScoreBreakdown robbed = ScoreSeat(MakeWinnerInput(kPungsOrChows, "1p", WinSource::RobbedKong));

        EXPECT_EQ(LinePoints(robbed, ScoreKind::Pung), kHonourPungOpen + 2 * kSimplePungConcealed);
    }

    TEST(ScoreDoublesTest, AWinningTileThatIsThePairLeavesEverySetConcealed) {
        for (const WinSource source : {WinSource::Discard, WinSource::RobbedKong}) {
            const ScoreBreakdown breakdown = ScoreSeat(MakeWinnerInput(kPungsOrChows, "Rd", source));
            EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pung), kHonourPungConcealed + 2 * kSimplePungConcealed);
        }
    }

    TEST(ScoreDoublesTest, TheWinningTileSittingInTwoSetsTakesTheBetterPlacement) {
        // 4p 4p 4p 4p 3p 5p is a pung of 4p and the chow 3p 4p 5p. A discard of 4p opens one of them: the chow (it scores nothing
        // either way) is the better one to open, so the pung of 4p stays concealed.
        const ScoreBreakdown breakdown = ScoreSeat(MakeWinnerInput("3p 4p 4p 4p 4p 5p 7s 7s 7s 1m 2m 3m 9m 9m", "4p", WinSource::Discard));

        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pung), 2 * kSimplePungConcealed);
        EXPECT_EQ(breakdown.points, kWinning + 2 * kSimplePungConcealed);
    }

    TEST(ScoreDoublesTest, ASeatThatDoesNotWinScoresItsBestPair) {
        // Three pairs: of East (own and round wind, 4), of the red dragon (2) and of the west wind (0).
        const ScoreBreakdown breakdown = ScoreSeat(MakeInput("Ew Ew Rd Rd Ww Ww 1p 2p 3p 7m 8m 9m"));

        EXPECT_EQ(CountLines(breakdown, ScoreKind::Pair), 1);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pair), kPairOfOwnAndRoundWind);
        EXPECT_EQ(breakdown.lines[0].tile, T("Ew"));
    }

    TEST(ScoreDoublesTest, AWinnerThatIsNoFourSetsAndAPairIsReadLikeAnyOtherSeat) {
        // Seven pairs are a limit hand (`ScoreLimitTest`); a winner whose tiles are no win at all (a single in place of the last pair)
        // is read by the pairs that stand, and the doubles that need four sets are left out.
        const ScoreBreakdown breakdown = ScoreSeat(MakeWinnerInput("Ew Ew Rd Rd Ww Ww Nw Nw Sw Sw Wd Wd Gd 1p", "1p"));

        EXPECT_TRUE(breakdown.won);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pair), kPairOfOwnAndRoundWind);
    }

}  // namespace BECore::Mahjong::Tests
