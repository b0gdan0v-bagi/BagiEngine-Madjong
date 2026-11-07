#include "ScoreTestSupport.h"

#include <Mahjong/Game/HandShape.h>

/**
 * The eleven limit hands of `ScoreSeat` (and the shape-independent two: the last tile as 1 dot and the loose tile as 5 dots), each on a
 * hand that has it and on near misses that score normally; the limit setting; a seat that does not win. Pure logic: no engine
 * services. Hands are written with the winning tile in them; the expected numbers are in `ScoreTestSupport.h`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kOddLimit = 305;
        constexpr int kThirteenOrphanKinds = 13;

        constexpr const char* kFourConcealedPungs = "1p 1p 1p 3p 3p 3p 5p 5p 5p 8p 8p 8p 9p 9p";
        constexpr const char* kHonours = "Ew Ew Ew Sw Sw Sw Rd Rd Rd Wd Wd Wd Gd Gd";
        constexpr const char* kTerminals = "1p 1p 1p 9p 9p 9p 1s 1s 1s 9m 9m 9m 9s 9s";
        constexpr const char* kGreenPungs = "2s 2s 2s 4s 4s 4s 6s 6s 6s 8s 8s 8s Gd Gd";
        constexpr const char* kSevenPairsOfHonours = "Ew Ew Sw Sw Ww Ww Nw Nw Rd Rd Wd Wd Gd Gd";
        constexpr const char* kSevenPairsOfDots = "1p 1p 2p 2p 3p 3p 4p 4p 5p 5p 6p 6p 7p 7p";
        constexpr const char* kBigThreeDragons = "Rd Rd Rd Wd Wd Wd Gd Gd Gd 5p 5p 5p 8p 8p";
        constexpr const char* kBigFourWinds = "Ew Ew Ew Sw Sw Sw Ww Ww Ww Nw Nw Nw 5p 5p";
        constexpr const char* kOrphans = "1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd";
        // Three chows, a pung of West and a pair of simples: no limit hand unless the winning tile makes it.
        constexpr const char* kPlainWithOneDot = "1p 2p 3p 7m 8m 9m 5s 6s 7s Ww Ww Ww 4m 4m";
        constexpr const char* kPlainWithFiveDots = "3p 4p 5p 7m 8m 9m 5s 6s 7s Ww Ww Ww 4m 4m";

        ScoreBreakdown Win(const std::string& concealed, const char* winningTile, WinSource source = WinSource::SelfDraw, std::initializer_list<Meld> melds = {}) {
            return ScoreSeat(MakeWinnerInput(concealed, winningTile, source, melds));
        }

        ScoreInput LastTileInput(const std::string& concealed, const char* winningTile) {
            ScoreInput input = MakeWinnerInput(concealed, winningTile);
            input.result.lastLiveTile = true;
            return input;
        }

        ScoreInput LooseTileInput(const std::string& concealed, const char* winningTile) {
            ScoreInput input = MakeWinnerInput(concealed, winningTile);
            input.result.loose = true;
            return input;
        }

        /// A limit hand: its name, the limit as the score, and the one line of it.
        void ExpectLimitHand(const ScoreBreakdown& breakdown, ScoreKind kind) {
            ASSERT_TRUE(breakdown.limitHand.has_value());
            EXPECT_EQ(*breakdown.limitHand, kind);
            EXPECT_TRUE(breakdown.won);
            EXPECT_EQ(breakdown.score, kLimit);
            EXPECT_EQ(breakdown.raw, kLimit);
            EXPECT_EQ(breakdown.points, 0);
            EXPECT_EQ(breakdown.doubles, 0);
            ASSERT_EQ(breakdown.lines.size(), 1u);
            EXPECT_EQ(breakdown.lines[0].kind, kind);
        }

        /// A hand that scores by its points: no limit hand, the lines of a normal score.
        void ExpectNoLimitHand(const ScoreBreakdown& breakdown) {
            EXPECT_FALSE(breakdown.limitHand.has_value());
            EXPECT_GT(breakdown.points, 0);
            EXPECT_GT(breakdown.lines.size(), 1u);
        }

    }  // namespace

    // ----- the hands of sets -----

    TEST(ScoreLimitTest, ConcealedPungsOfOneSuit) {
        ExpectLimitHand(Win(kFourConcealedPungs, "9p"), ScoreKind::ConcealedPungsOneSuit);
        // Declared kongs are concealed; the winning tile completes the pair.
        ExpectLimitHand(Win("1p 1p 1p 3p 3p 3p 9p 9p", "9p", WinSource::SelfDraw, {ConcealedKong(T("5p")), ConcealedKong(T("8p"))}), ScoreKind::ConcealedPungsOneSuit);
        // A win on a discard that completes only the pair opens no set.
        ExpectLimitHand(Win(kFourConcealedPungs, "9p", WinSource::Discard), ScoreKind::ConcealedPungsOneSuit);
    }

    TEST(ScoreLimitTest, ConcealedPungsOfOneSuitNearMisses) {
        ExpectNoLimitHand(Win("1p 2p 3p 4p 4p 4p 5p 5p 5p 8p 8p 8p 9p 9p", "9p"));
        ExpectNoLimitHand(Win("1p 1p 1p 3p 3p 3p 8p 8p 8p 9p 9p", "9p", WinSource::SelfDraw, {OpenPung(T("5p"))}));
        ExpectNoLimitHand(Win("1p 1p 1p 3p 3p 3p 5p 5p 5p 8p 8p 8p Rd Rd", "Rd"));
        // The winning discard completes the pung of 8p: a claimed set.
        ExpectNoLimitHand(Win(kFourConcealedPungs, "8p", WinSource::Discard));
        // The same pung completed by a self-draw is concealed.
        ExpectLimitHand(Win(kFourConcealedPungs, "8p"), ScoreKind::ConcealedPungsOneSuit);
    }

    TEST(ScoreLimitTest, HonoursOnly) {
        ExpectLimitHand(Win(kHonours, "Gd"), ScoreKind::HonoursOnly);
        ExpectLimitHand(Win("Rd Rd Rd Wd Wd Wd Gd Gd", "Gd", WinSource::SelfDraw, {OpenPung(T("Ew")), OpenPung(T("Sw"))}), ScoreKind::HonoursOnly);
    }

    TEST(ScoreLimitTest, HonoursOnlyNearMisses) {
        ExpectNoLimitHand(Win("Ew Ew Ew Sw Sw Sw Rd Rd Rd Wd Wd Wd 5p 5p", "5p"));
        ExpectNoLimitHand(Win("Ew Ew Ew Sw Sw Sw Rd Rd Rd Wd Wd Wd 4p 5p", "5p", WinSource::SelfDraw, {OpenChow(T("3p"))}));
    }

    TEST(ScoreLimitTest, TerminalsOnly) {
        ExpectLimitHand(Win(kTerminals, "9s"), ScoreKind::TerminalsOnly);
        ExpectLimitHand(Win("1p 1p 1p 9p 9p 9p 1s 1s 1s 9s 9s", "9s", WinSource::SelfDraw, {OpenPung(T("9m"))}), ScoreKind::TerminalsOnly);
    }

    TEST(ScoreLimitTest, TerminalsOnlyNearMisses) {
        ExpectNoLimitHand(Win("1p 1p 1p 9p 9p 9p 1s 1s 1s 9m 9m 9m 2s 2s", "2s"));
        // A chow 1 2 3 holds a 2; so does a chow meld.
        ExpectNoLimitHand(Win("1p 1p 1p 9p 9p 9p 1s 1s 1s 9s 9s", "9s", WinSource::SelfDraw, {OpenChow(T("1m"))}));
        ExpectNoLimitHand(Win("1p 2p 3p 9p 9p 9p 1s 1s 1s 9m 9m 9m 9s 9s", "9s"));
    }

    TEST(ScoreLimitTest, AllGreenInPungsAndAPair) {
        ExpectLimitHand(Win(kGreenPungs, "Gd"), ScoreKind::AllGreen);
        // All bamboo as well: the hand is two limit hands and is named by the first of them in the order of `ScoreKind`.
        ExpectLimitHand(Win("2s 2s 2s 4s 4s 4s 6s 6s 6s 8s 8s 8s 3s 3s", "3s"), ScoreKind::ConcealedPungsOneSuit);
        // 2 2 2 3 3 3 4 4 4 also reads as three chows 2 3 4; the pung reading is a limit hand.
        ExpectLimitHand(Win("2s 2s 2s 3s 3s 3s 4s 4s 4s 8s 8s 8s Gd Gd", "Gd"), ScoreKind::AllGreen);
        ExpectLimitHand(Win("2s 2s 2s 4s 4s 4s 8s 8s", "8s", WinSource::SelfDraw, {OpenPung(T("6s")), ConcealedKong(T("Gd"))}), ScoreKind::AllGreen);
    }

    TEST(ScoreLimitTest, AllGreenNearMisses) {
        // The only reading has a chow 2 3 4 of bamboo.
        ExpectNoLimitHand(Win("2s 3s 4s 6s 6s 6s 8s 8s 8s Gd Gd Gd 4s 4s", "4s"));
        // A pung of 5 of bamboo; the red dragon.
        ExpectNoLimitHand(Win("2s 2s 2s 4s 4s 4s 5s 5s 5s 8s 8s 8s Gd Gd", "Gd"));
        ExpectNoLimitHand(Win("2s 2s 2s 4s 4s 4s 6s 6s 6s 8s 8s 8s Rd Rd", "Rd"));
        // A chow meld is no pung.
        ExpectNoLimitHand(Win("4s 4s 4s 6s 6s 6s 8s 8s 8s Gd Gd", "Gd", WinSource::SelfDraw, {OpenChow(T("2s"))}));
    }

    TEST(ScoreLimitTest, BigThreeDragons) {
        ExpectLimitHand(Win(kBigThreeDragons, "8p"), ScoreKind::BigThreeDragons);
        ExpectLimitHand(Win("Wd Wd Wd Gd Gd Gd 5p 5p 5p 8p 8p", "8p", WinSource::SelfDraw, {OpenKong(T("Rd"))}), ScoreKind::BigThreeDragons);
    }

    TEST(ScoreLimitTest, BigThreeDragonsNearMisses) {
        // Two sets of dragons and a pair of dragons (the little three dragons).
        ExpectNoLimitHand(Win("Rd Rd Rd Wd Wd Wd Gd Gd 5p 5p 5p 7p 8p 9p", "8p"));
        // The fourth set and the pair are of two suits; the fourth set is a chow.
        ExpectNoLimitHand(Win("Rd Rd Rd Wd Wd Wd Gd Gd Gd 5p 5p 5p 8s 8s", "8s"));
        ExpectNoLimitHand(Win("Rd Rd Rd Wd Wd Wd Gd Gd Gd 5p 6p 7p 8p 8p", "8p"));
    }

    TEST(ScoreLimitTest, BigFourWinds) {
        ExpectLimitHand(Win(kBigFourWinds, "5p"), ScoreKind::BigFourWinds);
        ExpectLimitHand(Win("Ew Ew Ew Sw Sw Sw Ww Ww Ww 5p 5p", "5p", WinSource::SelfDraw, {OpenKong(T("Nw"))}), ScoreKind::BigFourWinds);
    }

    TEST(ScoreLimitTest, BigFourWindsNearMisses) {
        // Three sets of winds and a pair of winds (the little four winds).
        ExpectNoLimitHand(Win("Ew Ew Ew Sw Sw Sw Ww Ww Ww Nw Nw 1p 2p 3p", "1p"));
        // Three sets of winds and a set of dragons.
        ExpectNoLimitHand(Win("Ew Ew Ew Sw Sw Sw Ww Ww Ww Rd Rd Rd 5p 5p", "5p"));
    }

    TEST(ScoreLimitTest, FourKongsOpenAndConcealed) {
        ExpectLimitHand(Win("7p 7p", "7p", WinSource::SelfDraw, {OpenKong(T("2p")), ConcealedKong(T("9s")), OpenKong(T("Ew")), ConcealedKong(T("5m"))}), ScoreKind::FourKongs);
    }

    TEST(ScoreLimitTest, FourKongsNearMiss) {
        ExpectNoLimitHand(Win("7p 7p", "7p", WinSource::SelfDraw, {OpenKong(T("2p")), ConcealedKong(T("9s")), OpenKong(T("Ew")), OpenPung(T("5m"))}));
    }

    // ----- the shapes without sets -----

    TEST(ScoreLimitTest, SevenPairsOfHonoursIsNamedAsSevenPairsNotAsHonoursOnly) {
        ExpectLimitHand(Win(kSevenPairsOfHonours, "Gd"), ScoreKind::SevenPairsHonours);
    }

    TEST(ScoreLimitTest, SevenPairsOfOneSuit) {
        EXPECT_TRUE(IsSevenPairs(CountTiles(ParseTiles(kSevenPairsOfDots).value())));
        ExpectLimitHand(Win(kSevenPairsOfDots, "7p"), ScoreKind::SevenPairsOneSuit);
    }

    TEST(ScoreLimitTest, SevenPairsOfMixedSuitsAreNoWinAndNoLimitHand) {
        const char* mixed = "1p 1p 3p 3p 5p 5p 7p 7p 2s 2s 4s 4s 6s 6s";

        EXPECT_FALSE(IsSevenPairs(CountTiles(ParseTiles(mixed).value())));
        EXPECT_FALSE(Win(mixed, "6s").limitHand.has_value());
    }

    TEST(ScoreLimitTest, SevenPairsOfOneSuitWithAnHonourPairIsNoSevenPairs) {
        // It still wins by sets (1 2 3, 1 2 3, 4 5 6, 4 5 6 and a pair), and scores normally.
        const char* withHonour = "1p 1p 2p 2p 3p 3p 4p 4p 5p 5p 6p 6p Rd Rd";

        EXPECT_FALSE(IsSevenPairs(CountTiles(ParseTiles(withHonour).value())));
        ExpectNoLimitHand(Win(withHonour, "Rd"));
    }

    TEST(ScoreLimitTest, ThirteenOrphansOnEveryOneOfTheThirteenKinds) {
        // The 13-sided wait: every kind of the thirteen completes the hand with its pair.
        const eastl::vector<Tile> kinds = ParseTiles(kOrphans).value();
        ASSERT_EQ(kinds.size(), static_cast<size_t>(kThirteenOrphanKinds));
        for (const Tile kind : kinds) {
            const std::string winning = ToString(kind).data();
            ExpectLimitHand(Win(std::string(kOrphans) + " " + winning, winning.c_str()), ScoreKind::ThirteenOrphans);
        }
    }

    TEST(ScoreLimitTest, ThirteenOrphansNearMisses) {
        // A kind is missing (a second white dragon in its place); a simple in place of a terminal.
        ExpectNoLimitHand(Win("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Wd 1p", "1p"));
        EXPECT_FALSE(Win("2p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd 1s", "1s").limitHand.has_value());
    }

    // ----- the circumstances -----

    TEST(ScoreLimitTest, TheLastTileAsOneDot) {
        ExpectLimitHand(ScoreSeat(LastTileInput(kPlainWithOneDot, "1p")), ScoreKind::LastTileOneDot);
    }

    TEST(ScoreLimitTest, TheLastTileAsOneDotNearMisses) {
        // Another tile of the dots, another suit, the 1 dot without the last tile.
        ExpectNoLimitHand(ScoreSeat(LastTileInput(kPlainWithOneDot, "2p")));
        ExpectNoLimitHand(ScoreSeat(LastTileInput("1s 2s 3s 7m 8m 9m 5p 6p 7p Ww Ww Ww 4m 4m", "1s")));
        ExpectNoLimitHand(Win(kPlainWithOneDot, "1p"));
    }

    TEST(ScoreLimitTest, ALooseTileAsFiveDots) {
        ExpectLimitHand(ScoreSeat(LooseTileInput(kPlainWithFiveDots, "5p")), ScoreKind::LooseTileFiveDots);
    }

    TEST(ScoreLimitTest, ALooseTileAsFiveDotsNearMisses) {
        ExpectNoLimitHand(ScoreSeat(LooseTileInput("3s 4s 5s 7m 8m 9m 5p 6p 7p Ww Ww Ww 4m 4m", "5s")));
        ExpectNoLimitHand(ScoreSeat(LooseTileInput(kPlainWithFiveDots, "3p")));
        ExpectNoLimitHand(Win(kPlainWithFiveDots, "5p"));
        // The last tile is not the loose tile: 5 dots as the last tile is no limit hand.
        ExpectNoLimitHand(ScoreSeat(LastTileInput(kPlainWithFiveDots, "5p")));
    }

    // ----- the limit and who can have a limit hand -----

    TEST(ScoreLimitTest, ALimitHandScoresTheLimitOfTheSetting) {
        ScoreInput input = MakeWinnerInput(kBigFourWinds, "5p");
        input.limit = kLowerLimit;
        const ScoreBreakdown lower = ScoreSeat(input);
        input.limit = kOddLimit;
        const ScoreBreakdown odd = ScoreSeat(input);

        EXPECT_EQ(lower.score, kLowerLimit);
        EXPECT_EQ(lower.raw, kLowerLimit);
        EXPECT_EQ(odd.score, kLowerLimit);
        ASSERT_TRUE(lower.limitHand.has_value());
    }

    TEST(ScoreLimitTest, ALimitHandIsTheSingleLineAndNotTheNormalReadingOverTheLimit) {
        // Won on a discard of 8p the pung of 8p is open: the one suit with pungs scores by points, far over the limit, and is capped.
        const ScoreBreakdown normal = Win(kFourConcealedPungs, "8p", WinSource::Discard);
        const ScoreBreakdown limitHand = Win(kFourConcealedPungs, "9p", WinSource::Discard);

        EXPECT_GT(normal.raw, kLimit);
        EXPECT_EQ(normal.score, kLimit);
        EXPECT_FALSE(normal.limitHand.has_value());
        ExpectLimitHand(limitHand, ScoreKind::ConcealedPungsOneSuit);
    }

    TEST(ScoreLimitTest, ASeatThatDoesNotWinHasNoLimitHand) {
        // Thirteen honours, seven pairs of honours, thirteen orphans and four concealed pungs of one suit, none of them a win.
        for (const char* tiles : {"Ew Ew Ew Sw Sw Sw Ww Ww Ww Rd Rd Rd Gd", kSevenPairsOfHonours, kOrphans, kFourConcealedPungs}) {
            const ScoreBreakdown breakdown = ScoreSeat(MakeInput(tiles));

            EXPECT_FALSE(breakdown.limitHand.has_value()) << tiles;
            EXPECT_FALSE(breakdown.won) << tiles;
        }
    }

}  // namespace BECore::Mahjong::Tests
