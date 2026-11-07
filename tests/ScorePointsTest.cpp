#include "ScoreTestSupport.h"

/**
 * The points of a hand (`ScoreSeat`): chows, pungs and kongs open and concealed, pairs, bonus tiles. Pure logic: no engine services.
 * The expected numbers are the table of the rules, written out here and not taken from `Score.h`. Hands are seats that do not win
 * unless the case is about a winner (a winner's total still lacks the 20 for winning until the winner's lines are built).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // The table of the rules.
        constexpr int kSimplePungOpen = 2;
        constexpr int kSimplePungConcealed = 4;
        constexpr int kHonourPungOpen = 4;
        constexpr int kHonourPungConcealed = 8;
        constexpr int kSimpleKongOpen = 8;
        constexpr int kSimpleKongConcealed = 16;
        constexpr int kHonourKongOpen = 16;
        constexpr int kHonourKongConcealed = 32;
        constexpr int kScoringPair = 2;
        constexpr int kPairOfOwnAndRoundWind = 4;
        constexpr int kBonusTile = 4;

        // Tiles the cases use. None of them is in `kQuietChows`; the winds are neither the East of the default winds nor, where a
        // case says so, the seat's own.
        constexpr const char* kFivePungDots = "5p 5p 5p";
        constexpr const char* kNineBamboo = "9s 9s 9s";
        constexpr const char* kWestWind = "Ww Ww Ww";
        constexpr const char* kWhiteDragon = "Wd Wd Wd";

        std::string WithQuiet(const char* tiles) {
            return std::string(kQuietChows) + " " + tiles;
        }

        ScoreBreakdown Score(const std::string& tiles, Wind seatWind = Wind::East, Wind roundWind = Wind::East) {
            return ScoreSeat(MakeInput(WithQuiet(tiles.c_str()), {}, "", seatWind, roundWind));
        }

    }  // namespace

    TEST(ScorePointsTest, ChowsScoreNothing) {
        const ScoreBreakdown open = ScoreSeat(MakeInput(kQuietChows, {OpenChow(T("4s")), OpenChow(T("5m"))}));
        const ScoreBreakdown concealed = Score("4s 5s 6s");

        EXPECT_EQ(open.points, 0);
        EXPECT_TRUE(open.lines.empty());
        EXPECT_EQ(concealed.points, 0);
        EXPECT_TRUE(concealed.lines.empty());
        EXPECT_EQ(concealed.score, 0);
    }

    TEST(ScorePointsTest, PairOfDragonsScoresTwo) {
        const ScoreBreakdown breakdown = Score("Rd Rd");

        EXPECT_EQ(breakdown.points, kScoringPair);
        ASSERT_EQ(breakdown.lines.size(), 1u);
        EXPECT_EQ(breakdown.lines[0].kind, ScoreKind::Pair);
        EXPECT_EQ(breakdown.lines[0].tile, T("Rd"));
    }

    TEST(ScorePointsTest, PairOfTheOwnWindScoresTwo) {
        EXPECT_EQ(Score("Sw Sw", Wind::South).points, kScoringPair);
        EXPECT_EQ(Score("Sw Sw", Wind::South).doubles, 0);
    }

    TEST(ScorePointsTest, PairOfTheRoundWindScoresTwo) {
        EXPECT_EQ(Score("Sw Sw", Wind::West, Wind::South).points, kScoringPair);
    }

    TEST(ScorePointsTest, PairOfTheOwnAndTheRoundWindAtOnceScoresFour) {
        EXPECT_EQ(Score("Ew Ew").points, kPairOfOwnAndRoundWind);
        EXPECT_EQ(Score("Sw Sw", Wind::South, Wind::South).points, kPairOfOwnAndRoundWind);
    }

    TEST(ScorePointsTest, PairOfAnotherWindOrOfSimplesScoresNothing) {
        EXPECT_EQ(Score("Ww Ww").points, 0);
        EXPECT_EQ(Score("Ww Ww", Wind::South, Wind::North).points, 0);
        EXPECT_EQ(Score("5p 5p").points, 0);
        EXPECT_EQ(Score("9s 9s").points, 0);
        EXPECT_TRUE(Score("5p 5p").lines.empty());
    }

    TEST(ScorePointsTest, BonusTilesScoreFourEach) {
        const ScoreInput input = MakeInput(kQuietChows, {}, "F3 T3 T4");
        const ScoreBreakdown breakdown = ScoreSeat(input);

        EXPECT_EQ(breakdown.points, 3 * kBonusTile);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::Bonus), 3);
        EXPECT_EQ(breakdown.doubles, 0);
    }

    TEST(ScorePointsTest, PungOfSimplesScoresTwoOpenAndFourConcealed) {
        const ScoreBreakdown open = ScoreSeat(MakeInput(kQuietChows, {OpenPung(T("5p"))}));
        const ScoreBreakdown concealed = Score(kFivePungDots);

        EXPECT_EQ(open.points, kSimplePungOpen);
        EXPECT_EQ(concealed.points, kSimplePungConcealed);
        EXPECT_EQ(LinePoints(concealed, ScoreKind::Pung), kSimplePungConcealed);
        EXPECT_EQ(concealed.lines[0].tile, T("5p"));
    }

    TEST(ScorePointsTest, PungOfTerminalsScoresFourOpenAndEightConcealed) {
        EXPECT_EQ(ScoreSeat(MakeInput(kQuietChows, {OpenPung(T("9s"))})).points, kHonourPungOpen);
        EXPECT_EQ(Score(kNineBamboo).points, kHonourPungConcealed);
        EXPECT_EQ(ScoreSeat(MakeInput(kQuietChows, {OpenPung(T("1m"))})).points, kHonourPungOpen);
    }

    TEST(ScorePointsTest, PungOfWindsAndDragonsScoresLikeTerminals) {
        EXPECT_EQ(ScoreSeat(MakeInput(kQuietChows, {OpenPung(T("Ww"))})).points, kHonourPungOpen);
        EXPECT_EQ(Score(kWestWind).points, kHonourPungConcealed);
        EXPECT_EQ(ScoreSeat(MakeInput(kQuietChows, {OpenPung(T("Wd"))})).points, kHonourPungOpen);
        EXPECT_EQ(Score(kWhiteDragon).points, kHonourPungConcealed);
    }

    TEST(ScorePointsTest, KongOfSimplesScoresEightOpenAndSixteenConcealed) {
        const ScoreBreakdown open = ScoreSeat(MakeInput(kQuietChows, {OpenKong(T("5p"))}));
        const ScoreBreakdown concealed = ScoreSeat(MakeInput(kQuietChows, {ConcealedKong(T("5p"))}));

        EXPECT_EQ(open.points, kSimpleKongOpen);
        EXPECT_EQ(LinePoints(open, ScoreKind::Kong), kSimpleKongOpen);
        EXPECT_EQ(concealed.points, kSimpleKongConcealed);
        EXPECT_EQ(CountLines(concealed, ScoreKind::Pung), 0);
    }

    TEST(ScorePointsTest, KongOfTerminalsAndHonoursScoresSixteenOpenAndThirtyTwoConcealed) {
        for (const char* tile : {"9s", "1m", "Ww", "Wd"}) {
            EXPECT_EQ(ScoreSeat(MakeInput(kQuietChows, {OpenKong(T(tile))})).points, kHonourKongOpen) << tile;
            EXPECT_EQ(ScoreSeat(MakeInput(kQuietChows, {ConcealedKong(T(tile))})).points, kHonourKongConcealed) << tile;
        }
    }

    TEST(ScorePointsTest, FourOfAKindInTheHandThatWasNeverDeclaredIsOneConcealedPung) {
        const ScoreBreakdown simples = Score("5p 5p 5p 5p");
        const ScoreBreakdown terminals = Score("9s 9s 9s 9s");

        EXPECT_EQ(simples.points, kSimplePungConcealed);
        EXPECT_EQ(CountLines(simples, ScoreKind::Pung), 1);
        EXPECT_EQ(CountLines(simples, ScoreKind::Kong), 0);
        EXPECT_EQ(terminals.points, kHonourPungConcealed);
    }

    TEST(ScorePointsTest, SetsAndAPairAddUpInTheOrderOfTheReading) {
        // Melds first (in the order laid), then the concealed pungs, then the pair, then the bonus tiles.
        const ScoreInput input = MakeInput(std::string(kQuietChows) + " 5p 5p 5p Rd Rd", {OpenKong(T("9s")), OpenPung(T("2s"))}, "F3");
        const ScoreBreakdown breakdown = ScoreSeat(input);

        EXPECT_EQ(breakdown.points, kHonourKongOpen + kSimplePungOpen + kSimplePungConcealed + kScoringPair + kBonusTile);
        ASSERT_EQ(breakdown.lines.size(), 5u);
        EXPECT_EQ(breakdown.lines[0].kind, ScoreKind::Kong);
        EXPECT_EQ(breakdown.lines[1].kind, ScoreKind::Pung);
        EXPECT_EQ(breakdown.lines[1].tile, T("2s"));
        EXPECT_EQ(breakdown.lines[2].tile, T("5p"));
        EXPECT_EQ(breakdown.lines[3].kind, ScoreKind::Pair);
        EXPECT_EQ(breakdown.lines[4].kind, ScoreKind::Bonus);
    }

    TEST(ScorePointsTest, ASeatThatDoesNotWinHasNoWinnerLines) {
        const ScoreBreakdown breakdown = Score(kFivePungDots);

        EXPECT_FALSE(breakdown.won);
        EXPECT_FALSE(breakdown.limitHand.has_value());
        EXPECT_EQ(CountLines(breakdown, ScoreKind::Win), 0);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::WinTileFromWall), 0);
        EXPECT_EQ(CountLines(breakdown, ScoreKind::OnlyWinningTile), 0);
    }

}  // namespace BECore::Mahjong::Tests
