#include "ScoreTestSupport.h"

/**
 * The lines only a winner has (`ScoreSeat`): the 20 for winning, the wall tile and the only winning tile, and the doubles for the
 * winner, each on a hand that has it and on a near miss that does not; the choice of the reading that the winner's lines decide; the
 * worked hand of the plan. Pure logic: no engine services. The expected numbers are the table of the rules, in `ScoreTestSupport.h`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kFourWindsDoubles = 2;
        constexpr int kWorkedPoints = kWinning + kWallTile + kHonourPungOpen + kSimplePungConcealed + kScoringPair;
        constexpr int kWorkedRaw = kWorkedPoints << kTwoDoubles;
        constexpr int kWorkedScore = 130;
        constexpr int kNoPointsScore = 40;
        constexpr int kVisibleCopies = 3;

        // Three chows of two suits; each hand adds more tiles after them (a pair, or a set and a pair): no point, no double of their own.
        constexpr const char* kThreeChows = "5s 6s 7s 7m 8m 9m 3m 4m 5m";
        constexpr const char* kNoPointsHand = "5s 6s 7s 7m 8m 9m 3m 4m 5m 2p 3p 4p Ww Ww";

        ScoreBreakdown Win(const std::string& concealed, const char* winningTile, WinSource source = WinSource::SelfDraw, std::initializer_list<Meld> melds = {}) {
            return ScoreSeat(MakeWinnerInput(concealed, winningTile, source, melds));
        }

        std::string WithChows(const char* tiles) {
            return std::string(kThreeChows) + " " + tiles;
        }

    }  // namespace

    // ----- the points of the winner -----

    TEST(ScoreWinnerTest, TheTwentyAloneIsTheTotalOfANoPointsHandAndDoublesOnce) {
        // A win on a discard of 4p (a wait on 1p and 4p): chows and a pair of West wind (neither the own nor the round wind).
        const ScoreBreakdown breakdown = Win(kNoPointsHand, "4p", WinSource::Discard);

        EXPECT_EQ(breakdown.points, kWinning);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Win), kWinning);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::NoPoints), kOneDouble);
        EXPECT_EQ(breakdown.doubles, kOneDouble);
        EXPECT_EQ(breakdown.raw, kNoPointsScore);
        EXPECT_EQ(breakdown.score, kNoPointsScore);
        EXPECT_EQ(breakdown.lines[0].kind, ScoreKind::Win);
    }

    TEST(ScoreWinnerTest, TheWinningTileFromTheWallScoresTwoAlsoWhenLoose) {
        const ScoreBreakdown selfDrawn = Win(kNoPointsHand, "4p");
        ScoreInput loose = MakeWinnerInput(kNoPointsHand, "4p");
        loose.result.loose = true;

        EXPECT_EQ(LinePoints(selfDrawn, ScoreKind::WinTileFromWall), kWallTile);
        EXPECT_EQ(selfDrawn.points, kWinning + kWallTile);
        EXPECT_EQ(LinePoints(ScoreSeat(loose), ScoreKind::WinTileFromWall), kWallTile);
    }

    TEST(ScoreWinnerTest, TheWinningTileFromADiscardOrARobbedKongScoresNoWallTile) {
        EXPECT_EQ(CountLines(Win(kNoPointsHand, "4p", WinSource::Discard), ScoreKind::WinTileFromWall), 0);
        EXPECT_EQ(CountLines(Win(kNoPointsHand, "4p", WinSource::RobbedKong), ScoreKind::WinTileFromWall), 0);
    }

    TEST(ScoreWinnerTest, OnlyWinningTileForThePairWait) {
        const ScoreBreakdown breakdown = Win(kNoPointsHand, "Ww", WinSource::Discard);

        EXPECT_EQ(LinePoints(breakdown, ScoreKind::OnlyWinningTile), kOnlyTile);
        EXPECT_EQ(breakdown.points, kWinning + kOnlyTile);
    }

    TEST(ScoreWinnerTest, OnlyWinningTileForTheMiddleOfAChow) {
        EXPECT_EQ(LinePoints(Win(kNoPointsHand, "3p", WinSource::Discard), ScoreKind::OnlyWinningTile), kOnlyTile);
    }

    TEST(ScoreWinnerTest, OnlyWinningTileForTheOneEndOfAChow) {
        // 3 of 1 2 3 and 7 of 7 8 9 (the waits 1 2 / 8 9 have the one end).
        EXPECT_EQ(LinePoints(Win(WithChows("1p 2p 3p Ww Ww"), "3p", WinSource::Discard), ScoreKind::OnlyWinningTile), kOnlyTile);
        EXPECT_EQ(LinePoints(Win(WithChows("7p 8p 9p Ww Ww"), "7p", WinSource::Discard), ScoreKind::OnlyWinningTile), kOnlyTile);
    }

    TEST(ScoreWinnerTest, NoOnlyWinningTileForATwoSidedWait) {
        EXPECT_EQ(CountLines(Win(kNoPointsHand, "4p", WinSource::Discard), ScoreKind::OnlyWinningTile), 0);
        EXPECT_EQ(CountLines(Win(kNoPointsHand, "2p", WinSource::Discard), ScoreKind::OnlyWinningTile), 0);
    }

    TEST(ScoreWinnerTest, NoOnlyWinningTileWhenTwoTilesCompleteTheHand) {
        // Two pairs wait on either: 1p 1p and 4s 4s.
        EXPECT_EQ(CountLines(Win(WithChows("1p 1p 1p 4s 4s"), "1p", WinSource::Discard), ScoreKind::OnlyWinningTile), 0);
    }

    TEST(ScoreWinnerTest, TheOnlyWinningTileIsReadWithTheMeldsCounted) {
        // An open pung of 3p is laid and the chow 1 2 3 is completed with a fourth 3p: the wait on 3p is the only one.
        const ScoreBreakdown breakdown = Win("5s 6s 7s 7m 8m 9m 1p 2p 3p Ww Ww", "3p", WinSource::Discard, {OpenPung(T("3p"))});

        EXPECT_EQ(LinePoints(breakdown, ScoreKind::OnlyWinningTile), kOnlyTile);
    }

    TEST(ScoreWinnerTest, AKindWithAllFourCopiesInHandIsNoWait) {
        // Before the 1p the hand waits on 1p and 4p, but all four 4p lie in the open kong: the 1p is the only wait.
        const ScoreBreakdown kongOf4p = Win("2p 3p 5s 6s 7s 7m 8m 9m Ww Ww 1p", "1p", WinSource::Discard, {OpenKong(T("4p"))});
        // The same hand without the kong (a fourth chow instead): the wait on 1p and 4p is two-sided.
        const ScoreBreakdown twoSided = Win(WithChows("1p 2p 3p Ww Ww"), "1p", WinSource::Discard);

        EXPECT_EQ(LinePoints(kongOf4p, ScoreKind::OnlyWinningTile), kOnlyTile);
        EXPECT_EQ(CountLines(twoSided, ScoreKind::OnlyWinningTile), 0);
    }

    // ----- the doubles for the winner, each on a hand that has it and a near miss -----

    TEST(ScoreWinnerTest, OnlyPungsNeedsAllFourSetsToBePungsOrKongs) {
        const ScoreBreakdown pungs = Win("2p 2p 2p 5s 5s 5s 7m 7m 7m 9m 9m 9m 4s 4s", "4s");
        const ScoreBreakdown withKong = Win("2p 2p 2p 5s 5s 5s 7m 7m 7m 4s 4s", "4s", WinSource::SelfDraw, {OpenKong(T("9m"))});
        const ScoreBreakdown oneChow = Win("2p 2p 2p 5s 5s 5s 7m 8m 9m 9m 9m 9m 4s 4s", "4s");

        EXPECT_EQ(LineDoubles(pungs, ScoreKind::OnlyPungs), kOneDouble);
        EXPECT_EQ(LineDoubles(withKong, ScoreKind::OnlyPungs), kOneDouble);
        EXPECT_EQ(CountLines(oneChow, ScoreKind::OnlyPungs), 0);
    }

    TEST(ScoreWinnerTest, LittleFourWindsIsThreeSetsOfWindsAndAPairOfWinds) {
        const ScoreBreakdown breakdown = Win("Ew Ew Ew Sw Sw Sw Ww Ww Ww Nw Nw 1p 2p 3p", "1p");

        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::LittleFourWinds), kFourWindsDoubles);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::LittleFourWinds), 1);
    }

    TEST(ScoreWinnerTest, LittleFourWindsNearMisses) {
        // Two sets of winds and a pair of winds; three sets of winds and a pair of simples.
        EXPECT_EQ(CountLines(Win("Ew Ew Ew Sw Sw Sw Nw Nw 1p 2p 3p 4s 5s 6s", "1p"), ScoreKind::LittleFourWinds), 0);
        EXPECT_EQ(CountLines(Win("Ew Ew Ew Sw Sw Sw Ww Ww Ww 5p 5p 1p 2p 3p", "1p"), ScoreKind::LittleFourWinds), 0);
    }

    TEST(ScoreWinnerTest, LittleThreeDragonsIsTwoSetsOfDragonsAndAPairOfDragons) {
        const ScoreBreakdown breakdown = Win("Rd Rd Rd Wd Wd Wd Gd Gd 1p 2p 3p 4s 5s 6s", "1p");

        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::LittleThreeDragons), kOneDouble);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::DragonSet), 2 * kOneDouble);
    }

    TEST(ScoreWinnerTest, LittleThreeDragonsNearMisses) {
        EXPECT_EQ(CountLines(Win("Rd Rd Rd Wd Wd Wd Nw Nw 1p 2p 3p 4s 5s 6s", "1p"), ScoreKind::LittleThreeDragons), 0);
        EXPECT_EQ(CountLines(Win("Rd Rd Rd Gd Gd 1p 2p 3p 4s 5s 6s 7m 8m 9m", "1p"), ScoreKind::LittleThreeDragons), 0);
    }

    TEST(ScoreWinnerTest, TerminalsOrHonoursEverywhereCountsTheChowsOfOneAndSevenAndThePair) {
        const ScoreBreakdown breakdown = Win("1p 2p 3p 7m 8m 9m 9s 9s 9s Ew Ew Ew 1m 1m", "1m");

        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::TerminalsOrHonoursEverywhere), kOneDouble);
    }

    TEST(ScoreWinnerTest, TerminalsOrHonoursEverywhereNearMisses) {
        // A chow 2 3 4; a pair of simples.
        EXPECT_EQ(CountLines(Win("2p 3p 4p 7m 8m 9m 9s 9s 9s Ew Ew Ew 1m 1m", "1m"), ScoreKind::TerminalsOrHonoursEverywhere), 0);
        EXPECT_EQ(CountLines(Win("1p 2p 3p 7m 8m 9m 9s 9s 9s Ew Ew Ew 5m 5m", "5m"), ScoreKind::TerminalsOrHonoursEverywhere), 0);
    }

    TEST(ScoreWinnerTest, FullyConcealedNeedsNoOpenSet) {
        const ScoreBreakdown concealed = Win(kNoPointsHand, "4p");
        const ScoreBreakdown withConcealedKong = Win(WithChows("Ww Ww"), "Ww", WinSource::SelfDraw, {ConcealedKong(T("Rd"))});

        EXPECT_EQ(LineDoubles(concealed, ScoreKind::FullyConcealed), kOneDouble);
        EXPECT_EQ(LineDoubles(withConcealedKong, ScoreKind::FullyConcealed), kOneDouble);
    }

    TEST(ScoreWinnerTest, AnOpenSetBreaksFullyConcealed) {
        const ScoreBreakdown open = Win("5s 6s 7s 7m 8m 9m 3m 4m 5m Ww Ww", "Ww", WinSource::SelfDraw, {OpenChow(T("2p"))});
        const ScoreBreakdown kongOpen = Win(WithChows("Ww Ww"), "Ww", WinSource::SelfDraw, {OpenKong(T("Rd"))});

        EXPECT_EQ(CountLines(open, ScoreKind::FullyConcealed), 0);
        EXPECT_EQ(CountLines(kongOpen, ScoreKind::FullyConcealed), 0);
    }

    TEST(ScoreWinnerTest, APungCompletedByADiscardIsOpenButTheSameTileAsThePairIsStillConcealed) {
        // 2p 2p 2p won with a discard of 2p: the pung is claimed, open.
        const ScoreBreakdown asPung = Win(WithChows("2p 2p 2p Ww Ww"), "2p", WinSource::Discard);
        // 2p 2p as the pair, Ww Ww Ww a pung: nothing is claimed.
        const ScoreBreakdown asPair = Win(WithChows("2p 2p Ww Ww Ww"), "2p", WinSource::Discard);

        EXPECT_EQ(CountLines(asPung, ScoreKind::FullyConcealed), 0);
        EXPECT_EQ(LinePoints(asPung, ScoreKind::Pung), kSimplePungOpen);
        EXPECT_EQ(LineDoubles(asPair, ScoreKind::FullyConcealed), kOneDouble);
        EXPECT_EQ(LinePoints(asPair, ScoreKind::Pung), kHonourPungConcealed);
    }

    TEST(ScoreWinnerTest, ARobbedKongOpensTheSetThatHoldsTheTile) {
        // The robbed tile completes the pung of 2p: it is a claimed set, open, as with a discard.
        const ScoreBreakdown robbed = Win(WithChows("2p 2p 2p Ww Ww"), "2p", WinSource::RobbedKong);
        const ScoreBreakdown selfDrawn = Win(WithChows("2p 2p 2p Ww Ww"), "2p", WinSource::SelfDraw);

        EXPECT_EQ(LinePoints(robbed, ScoreKind::Pung), kSimplePungOpen);
        EXPECT_EQ(CountLines(robbed, ScoreKind::FullyConcealed), 0);
        EXPECT_EQ(LinePoints(selfDrawn, ScoreKind::Pung), kSimplePungConcealed);
        EXPECT_EQ(LineDoubles(selfDrawn, ScoreKind::FullyConcealed), kOneDouble);
    }

    TEST(ScoreWinnerTest, SeveralKongsIsTwoOrThreeKongsOpenOrConcealed) {
        const ScoreBreakdown two = Win("5s 6s 7s 3m 4m 5m Ww Ww", "Ww", WinSource::SelfDraw, {OpenKong(T("2p")), ConcealedKong(T("9s"))});
        const ScoreBreakdown three = Win("5s 6s 7s 8m 8m", "8m", WinSource::SelfDraw, {OpenKong(T("2p")), ConcealedKong(T("9s")), ConcealedKong(T("Ew"))});
        const ScoreBreakdown one = Win("5s 6s 7s 3m 4m 5m Ww Ww 1p 2p 3p", "Ww", WinSource::SelfDraw, {OpenKong(T("2p"))});

        EXPECT_EQ(LineDoubles(two, ScoreKind::SeveralKongs), kOneDouble);
        EXPECT_EQ(LineDoubles(three, ScoreKind::SeveralKongs), kOneDouble);
        EXPECT_EQ(CountLines(one, ScoreKind::SeveralKongs), 0);
    }

    TEST(ScoreWinnerTest, TheCircumstancesOfTheWinDoubleOnceEach) {
        ScoreInput loose = MakeWinnerInput(kNoPointsHand, "4p");
        loose.result.loose = true;
        ScoreInput last = MakeWinnerInput(kNoPointsHand, "4p");
        last.result.lastLiveTile = true;
        const ScoreBreakdown robbed = Win(kNoPointsHand, "4p", WinSource::RobbedKong);

        EXPECT_EQ(LineDoubles(ScoreSeat(loose), ScoreKind::LooseTile), kOneDouble);
        EXPECT_EQ(LineDoubles(ScoreSeat(last), ScoreKind::LastTile), kOneDouble);
        EXPECT_EQ(LineDoubles(robbed, ScoreKind::RobbedKong), kOneDouble);
        // Nothing of it for an ordinary win.
        const ScoreBreakdown plain = Win(kNoPointsHand, "4p");
        EXPECT_EQ(CountLines(plain, ScoreKind::LooseTile) + CountLines(plain, ScoreKind::LastTile) + CountLines(plain, ScoreKind::RobbedKong), 0);
    }

    TEST(ScoreWinnerTest, ObviouslyLastNeedsThreeVisibleCopies) {
        ScoreInput input = MakeWinnerInput(kNoPointsHand, "4p", WinSource::Discard);
        input.visibleWinningTileCopies = kVisibleCopies;
        EXPECT_EQ(LineDoubles(ScoreSeat(input), ScoreKind::ObviouslyLast), kOneDouble);

        input.visibleWinningTileCopies = kVisibleCopies - 1;
        EXPECT_EQ(CountLines(ScoreSeat(input), ScoreKind::ObviouslyLast), 0);
    }

    TEST(ScoreWinnerTest, TheCallingHandDoublesInTheFirstHandOnly) {
        ScoreInput input = MakeWinnerInput(kNoPointsHand, "4p");
        EXPECT_EQ(CountLines(ScoreSeat(input), ScoreKind::CallingFirstHand), 0);

        input.hand.callingDeclared = true;
        input.hand.callingAtDiscardCount = 0;
        EXPECT_EQ(LineDoubles(ScoreSeat(input), ScoreKind::CallingFirstHand), kOneDouble);

        input.hand.callingAtDiscardCount = 1;
        EXPECT_EQ(CountLines(ScoreSeat(input), ScoreKind::CallingFirstHand), 0);
    }

    // ----- no points -----

    TEST(ScoreWinnerTest, NoPointsIsBrokenByAWallTileAFlowerAndAScoringPair) {
        EXPECT_EQ(CountLines(Win(kNoPointsHand, "4p"), ScoreKind::NoPoints), 0);

        ScoreInput flower = MakeWinnerInput(kNoPointsHand, "4p", WinSource::Discard);
        flower.hand.bonus = ParseTiles("F3").value();
        EXPECT_EQ(CountLines(ScoreSeat(flower), ScoreKind::NoPoints), 0);

        const ScoreBreakdown dragonPair = Win(WithChows("2p 3p 4p Rd Rd"), "4p", WinSource::Discard);
        EXPECT_EQ(CountLines(dragonPair, ScoreKind::NoPoints), 0);
        EXPECT_EQ(dragonPair.points, kWinning + kScoringPair);
    }

    TEST(ScoreWinnerTest, NoPointsIsBrokenByAPungAndByTheOnlyWinningTile) {
        EXPECT_EQ(CountLines(Win(WithChows("2p 2p 2p Ww Ww"), "Ww", WinSource::Discard), ScoreKind::NoPoints), 0);
        EXPECT_EQ(CountLines(Win(kNoPointsHand, "Ww", WinSource::Discard), ScoreKind::NoPoints), 0);
    }

    // ----- the readings that the winner's lines decide -----

    TEST(ScoreWinnerTest, ThePungsAreReadWhenTheyAreWorthMore) {
        // 1p 1p 1p 2p 2p 2p 3p 3p 3p can be three chows; as pungs the hand is only pungs.
        const ScoreBreakdown breakdown = Win("1p 1p 1p 2p 2p 2p 3p 3p 3p 5s 5s 5s 9m 9m", "9m", WinSource::Discard);

        EXPECT_EQ(CountLines(breakdown, ScoreKind::Pung), 4);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::OnlyPungs), kOneDouble);
    }

    TEST(ScoreWinnerTest, TheChowsAreReadWhenNoPointsIsWorthMore) {
        // 2p 2p 2p 3p 3p 3p 4p 4p 4p as pungs: 20 + 3 x 4 = 32 points, none of the doubles; as three chows 2 3 4p: the 20, no points x2.
        const ScoreBreakdown breakdown = Win("2p 2p 2p 3p 3p 3p 4p 4p 4p 5s 6s 7s 8m 8m", "5s", WinSource::Discard);

        EXPECT_EQ(CountLines(breakdown, ScoreKind::Pung), 0);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::NoPoints), kOneDouble);
        EXPECT_EQ(breakdown.points, kWinning);
        EXPECT_EQ(breakdown.score, kNoPointsScore);
    }

    TEST(ScoreWinnerTest, ASeatThatDoesNotWinHasNoWinnerLines) {
        // The same tiles as the winner of the hand above: no 20, no no-points double.
        const ScoreBreakdown breakdown = ScoreSeat(MakeInput(kNoPointsHand));

        EXPECT_FALSE(breakdown.won);
        EXPECT_EQ(breakdown.points, 0);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::Win), 0);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::NoPoints), 0);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::FullyConcealed), 0);
    }

    // ----- the worked hand -----

    TEST(ScoreWinnerTest, TheWorkedHand) {
        // South wins a self-drawn 4p in the East round: an open pung of the red dragon, 2p 3p 4p, a pung of 6s, 7m 8m 9m, the pair of
        // South's wind; the calling hand was declared with the first discard.
        ScoreInput input = MakeWinnerInput("2p 3p 4p 6s 6s 6s 7m 8m 9m Sw Sw", "4p", WinSource::SelfDraw, {OpenPung(T("Rd"))}, Wind::South, Wind::East);
        input.hand.callingDeclared = true;
        input.hand.callingAtDiscardCount = 0;
        const ScoreBreakdown breakdown = ScoreSeat(input);

        EXPECT_TRUE(breakdown.won);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Win), kWinning);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::WinTileFromWall), kWallTile);
        // Before the 4p the hand waits on 1p and 4p.
        EXPECT_EQ(CountLines(breakdown, ScoreKind::OnlyWinningTile), 0);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pung), kHonourPungOpen + kSimplePungConcealed);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::Pung), 2);
        EXPECT_EQ(LinePoints(breakdown, ScoreKind::Pair), kScoringPair);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::DragonSet), kOneDouble);
        EXPECT_EQ(LineDoubles(breakdown, ScoreKind::CallingFirstHand), kOneDouble);
        for (const ScoreKind absent : {ScoreKind::OnlyPungs, ScoreKind::NoPoints, ScoreKind::TerminalsOrHonoursEverywhere, ScoreKind::FullyConcealed, ScoreKind::LooseTile, ScoreKind::LastTile,
                                       ScoreKind::RobbedKong, ScoreKind::SeveralKongs, ScoreKind::ObviouslyLast}) {
            EXPECT_EQ(CountLines(breakdown, absent), 0) << Name(absent);
        }
        EXPECT_EQ(breakdown.points, kWorkedPoints);
        EXPECT_EQ(breakdown.doubles, kTwoDoubles);
        EXPECT_EQ(breakdown.raw, kWorkedRaw);
        EXPECT_EQ(breakdown.score, kWorkedScore);
        EXPECT_FALSE(breakdown.limitHand.has_value());
    }

}  // namespace BECore::Mahjong::Tests
