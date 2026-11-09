#include <Mahjong/Arena/ArenaReport.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * The statistics and the report of the arena (`src/Mahjong/Arena/ArenaStats.h`, `ArenaReport.h`): `MeanCi` (mean, sample standard
 * deviation, half-width `1.96 * sd / sqrt(n)`, no interval for fewer than two seeds), the per-seed series (a seed's four rotations
 * divided by four), the paired difference of two focus players on the same seeds and its `significant` rule, and the text of the report
 * for a made-up `ArenaResult` (header, both tables, the paired rows, the score lines that occur, the timing table only with timing).
 * The numbers are worked out by hand; the one run of the model is the mirror identity (Normal against three Normal: points 0 for every
 * seed, an interval of 0). Pure logic: no engine services; the file takes about 1 s in Debug.
 */
namespace BECore::Mahjong::Arena {

    namespace {

        /// The tolerance of a number worked out by hand to nine digits.
        constexpr double kTolerance = 1e-9;
        constexpr uint32_t kFirstSeed = 100;
        constexpr int kMirrorSeeds = 6;
        constexpr uint32_t kMirrorFirstSeed = 4000;
        constexpr const char* kNormal = "normal";
        constexpr const char* kEasy = "easy";
        constexpr const char* kRandom = "random";

        ArenaPlayer Player(std::string_view spec) {
            const std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(spec);
            EXPECT_TRUE(player.has_value()) << std::string(spec.data(), spec.size());
            return player.value_or(ArenaPlayer{});
        }

        SeedRow Row(uint32_t seed, int points, int wins, int drawn, int declared, int winScore) {
            SeedRow row;
            row.seed = seed;
            row.pointsOfFocus = points;
            row.winsOfFocus = wins;
            row.drawn = drawn;
            row.declared = declared;
            row.winScore = winScore;
            return row;
        }

        size_t Index(ScoreKind kind) {
            return static_cast<size_t>(kind);
        }

        /// Normal over four seeds, worked out by hand: points a hand 100, 50, 0, -50 (mean 25, sd 64.5497), wins % 50, 25, 0, 25 (mean 25,
        /// sd 20.4124), 4 wins of 16 hands, 1 drawn hand, 8 declarations, winning scores 80 + 40 + 30 = 150.
        FocusResult NormalFocus() {
            FocusResult focus;
            focus.player = Player(kNormal);
            focus.rows = {Row(kFirstSeed, 400, 2, 0, 3, 80), Row(kFirstSeed + 1, 200, 1, 0, 2, 40), Row(kFirstSeed + 2, 0, 0, 1, 1, 0), Row(kFirstSeed + 3, -200, 1, 0, 2, 30)};
            focus.focus.hands = 16;
            focus.focus.wins = 4;
            focus.focus.points = 400;
            focus.focus.drawn = 1;
            focus.focus.declared = 8;
            focus.focus.winScore = 150;
            focus.focus.winsByKind[Index(ScoreKind::Win)] = 4;
            focus.focus.winsByKind[Index(ScoreKind::WinTileFromWall)] = 1;
            focus.focus.decisions = 400;
            focus.focus.nanoseconds = 800000;
            // The three other seats: 11 of the 15 won hands, 3 seat-hands of the drawn one.
            focus.others.hands = 48;
            focus.others.wins = 11;
            focus.others.points = -400;
            focus.others.drawn = 3;
            focus.others.declared = 24;
            focus.others.winScore = 330;
            focus.others.winsByKind[Index(ScoreKind::Win)] = 11;
            focus.others.winsByKind[Index(ScoreKind::OnlyPungs)] = 2;
            focus.others.decisions = 1200;
            focus.others.nanoseconds = 600000;
            focus.winsBySeat = {1, 1, 1, 1};
            return focus;
        }

        /// Easy on the same seeds, with no win at all: the wins % differ from Normal's by -50, -25, 0, -25 (mean -25, sd 20.4124, half
        /// 20.0043, significant), the points a hand by -150, -75, -25, 25 (mean -56.25, half 73.16, not significant).
        FocusResult EasyFocus() {
            FocusResult focus;
            focus.player = Player(kEasy);
            focus.rows = {Row(kFirstSeed, -200, 0, 0, 1, 0), Row(kFirstSeed + 1, -100, 0, 0, 1, 0), Row(kFirstSeed + 2, -100, 0, 1, 0, 0), Row(kFirstSeed + 3, -100, 0, 0, 1, 0)};
            focus.focus.hands = 16;
            focus.focus.points = -500;
            focus.focus.drawn = 1;
            focus.focus.declared = 3;
            focus.others.hands = 48;
            focus.others.wins = 15;
            focus.others.points = 500;
            focus.others.drawn = 3;
            focus.others.winScore = 600;
            focus.others.winsByKind[Index(ScoreKind::Win)] = 15;
            focus.focus.decisions = 500;
            focus.focus.nanoseconds = 1000000;
            focus.others.decisions = 1500;
            focus.others.nanoseconds = 1500000;
            return focus;
        }

        ArenaResult MadeUp(bool timing, bool withEasy) {
            ArenaResult result;
            result.first = kFirstSeed;
            result.count = 4;
            result.others = Player(kRandom);
            result.timing = timing;
            result.focus.push_back(NormalFocus());
            if (withEasy) {
                result.focus.push_back(EasyFocus());
            }
            return result;
        }

        bool Contains(const eastl::string& text, const char* part) {
            return text.find(part) != eastl::string::npos;
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

    }  // namespace

    TEST(ArenaStatsTest, MeanCiOfASmallSeries) {
        // 2 4 4 4 5 5 7 9: the mean is 5, the squares of the deviations sum to 32, the sample variance is 32 / 7.
        const MeanInterval interval = MeanCi({2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0});
        EXPECT_EQ(interval.n, 8u);
        EXPECT_TRUE(interval.hasInterval);
        EXPECT_NEAR(interval.mean, 5.0, kTolerance);
        EXPECT_NEAR(interval.sd, std::sqrt(32.0 / 7.0), kTolerance);
        EXPECT_NEAR(interval.half, kIntervalZ * std::sqrt(32.0 / 7.0) / std::sqrt(8.0), kTolerance);
        EXPECT_NEAR(interval.half, 1.48162, 1e-5);
    }

    TEST(ArenaStatsTest, AConstantSeriesHasNoWidth) {
        const MeanInterval interval = MeanCi({3.5, 3.5, 3.5});
        EXPECT_TRUE(interval.hasInterval);
        EXPECT_EQ(interval.mean, 3.5);
        EXPECT_EQ(interval.sd, 0.0);
        EXPECT_EQ(interval.half, 0.0);
    }

    TEST(ArenaStatsTest, FewerThanTwoSeedsGiveNoInterval) {
        const MeanInterval one = MeanCi({7.0});
        EXPECT_EQ(one.n, 1u);
        EXPECT_FALSE(one.hasInterval);
        EXPECT_EQ(one.mean, 7.0);
        EXPECT_EQ(one.half, 0.0);
        const MeanInterval none = MeanCi({});
        EXPECT_EQ(none.n, 0u);
        EXPECT_FALSE(none.hasInterval);
        EXPECT_EQ(none.mean, 0.0);
        EXPECT_FALSE(IsSignificant(one));
        EXPECT_FALSE(IsSignificant(none));
    }

    TEST(ArenaStatsTest, ThePerSeedSeriesAreTheFourRotationsDividedByFour) {
        const FocusResult normal = NormalFocus();
        const eastl::vector<double> points = SeedSeries(normal, SeedMetric::PointsPerHand);
        ASSERT_EQ(points.size(), 4u);
        EXPECT_EQ(points[0], 100.0);
        EXPECT_EQ(points[1], 50.0);
        EXPECT_EQ(points[2], 0.0);
        EXPECT_EQ(points[3], -50.0);
        const eastl::vector<double> wins = SeedSeries(normal, SeedMetric::WinPercent);
        EXPECT_EQ(wins[0], 50.0);
        EXPECT_EQ(wins[1], 25.0);
        EXPECT_EQ(wins[2], 0.0);
        const eastl::vector<double> drawn = SeedSeries(normal, SeedMetric::DrawnPercent);
        EXPECT_EQ(drawn[2], 25.0);
        EXPECT_EQ(drawn[0], 0.0);
        const eastl::vector<double> declared = SeedSeries(normal, SeedMetric::DeclaredPercent);
        EXPECT_EQ(declared[0], 75.0);
        EXPECT_EQ(declared[2], 25.0);

        // The mean of a series is the total over the hands: 400 points over 16 hands, 4 wins of 16.
        EXPECT_NEAR(MetricInterval(normal, SeedMetric::PointsPerHand).mean, 25.0, kTolerance);
        EXPECT_NEAR(MetricInterval(normal, SeedMetric::WinPercent).mean, 25.0, kTolerance);
        // Worked out by hand: the deviations of the wins % are 25, 0, -25, 0, so the variance is 1250 / 3.
        EXPECT_NEAR(MetricInterval(normal, SeedMetric::WinPercent).sd, std::sqrt(1250.0 / 3.0), kTolerance);
    }

    TEST(ArenaStatsTest, ThePairedDifferenceIsTheIntervalOfTheDifferencesOfTheSeeds) {
        const FocusResult normal = NormalFocus();
        const FocusResult easy = EasyFocus();

        // Easy minus Normal in wins %: -50, -25, 0, -25, mean -25, sd sqrt(1250 / 3), half 1.96 * sd / 2: the interval excludes 0.
        const MeanInterval wins = PairedDifference(easy, normal, SeedMetric::WinPercent);
        EXPECT_EQ(wins.n, 4u);
        EXPECT_NEAR(wins.mean, -25.0, kTolerance);
        EXPECT_NEAR(wins.half, kIntervalZ * std::sqrt(1250.0 / 3.0) / 2.0, kTolerance);
        EXPECT_TRUE(IsSignificant(wins));

        // In points a hand the differences are -150, -75, -25, 25: mean -56.25, the interval reaches above 0.
        const MeanInterval points = PairedDifference(easy, normal, SeedMetric::PointsPerHand);
        EXPECT_NEAR(points.mean, -56.25, kTolerance);
        EXPECT_NEAR(points.half, 73.16, 1e-2);
        EXPECT_GT(points.mean + points.half, 0.0);
        EXPECT_FALSE(IsSignificant(points));

        // The sign follows the order of the arguments, the width does not.
        const MeanInterval reversed = PairedDifference(normal, easy, SeedMetric::WinPercent);
        EXPECT_NEAR(reversed.mean, 25.0, kTolerance);
        EXPECT_NEAR(reversed.half, wins.half, kTolerance);
        EXPECT_TRUE(IsSignificant(reversed));

        // A player against itself: every difference is 0, the interval is 0 and excludes nothing.
        const MeanInterval same = PairedDifference(normal, normal, SeedMetric::PointsPerHand);
        EXPECT_EQ(same.mean, 0.0);
        EXPECT_EQ(same.half, 0.0);
        EXPECT_FALSE(IsSignificant(same));
    }

    TEST(ArenaStatsTest, TheSignificantRuleIsTheIntervalExcludingZero) {
        MeanInterval interval;
        interval.n = 10;
        interval.hasInterval = true;
        interval.mean = 2.0;
        interval.half = 1.9;
        EXPECT_TRUE(IsSignificant(interval));
        interval.half = 2.0;
        EXPECT_FALSE(IsSignificant(interval)) << "an interval that touches 0 does not exclude it";
        interval.half = 2.1;
        EXPECT_FALSE(IsSignificant(interval));
        interval.mean = -2.0;
        interval.half = 1.9;
        EXPECT_TRUE(IsSignificant(interval)) << "below 0 counts too";
        interval.hasInterval = false;
        EXPECT_FALSE(IsSignificant(interval)) << "no interval, nothing excluded";
    }

    TEST(ArenaStatsTest, ThePairedDifferenceNeedsTheSameSeeds) {
        const FocusResult normal = NormalFocus();
        FocusResult shifted = EasyFocus();
        shifted.rows[2].seed += 1;
        EXPECT_EQ(PairedDifference(shifted, normal, SeedMetric::WinPercent).n, 0u);
        FocusResult shorter = EasyFocus();
        shorter.rows.pop_back();
        EXPECT_EQ(PairedDifference(shorter, normal, SeedMetric::WinPercent).n, 0u);
        EXPECT_FALSE(PairedDifference(shorter, normal, SeedMetric::WinPercent).hasInterval);
    }

    TEST(ArenaStatsTest, TheMirrorHasPointsOfZeroAndAnIntervalOfZero) {
        // Normal against three Normal on the same walls: the four rotations replay one game, so every seed has points 0.
        ArenaConfig config;
        config.first = kMirrorFirstSeed;
        config.count = kMirrorSeeds;
        config.focus = {Player(kNormal), Player(kNormal)};
        config.others = Player(kNormal);
        const std::expected<ArenaResult, eastl::string> run = RunArena(config);
        ASSERT_TRUE(run.has_value()) << (run.has_value() ? "" : run.error().c_str());
        const FocusResult& focus = run->focus[0];
        const MeanInterval points = MetricInterval(focus, SeedMetric::PointsPerHand);
        EXPECT_EQ(points.n, static_cast<size_t>(kMirrorSeeds));
        EXPECT_TRUE(points.hasInterval);
        EXPECT_EQ(points.mean, 0.0);
        EXPECT_EQ(points.half, 0.0);
        EXPECT_FALSE(IsSignificant(points));
        // The same player twice in the list differs from itself by nothing.
        const MeanInterval difference = PairedDifference(run->focus[1], run->focus[0], SeedMetric::PointsPerHand);
        EXPECT_EQ(difference.mean, 0.0);
        EXPECT_EQ(difference.half, 0.0);
        EXPECT_FALSE(IsSignificant(PairedDifference(run->focus[1], run->focus[0], SeedMetric::WinPercent)));
        // The report of that run says so.
        const eastl::string text = FormatArenaReport(*run);
        EXPECT_TRUE(Contains(text, "+0.00 +- 0.00")) << Std(text);
        EXPECT_FALSE(Contains(text, "significant")) << Std(text);
    }

    TEST(ArenaStatsTest, TheReportOfAMadeUpResult) {
        const eastl::string text = FormatArenaReport(MadeUp(false, true));
        const std::string expected = "Bot arena: seeds 100 .. 103 (4 seeds, 16 hands per focus player)\n"
                                     "focus: normal, easy; others: random\n"
                                     "+- is 1.96 standard errors over the seeds (a 95 percent interval); the 4 rotations of a seed play one wall, so a seed is one sample\n"
                                     "\n"
                                     "player                    hands            wins %           points/hand     drawn %  declared %   avg win score\n"
                                     "normal                       16    25.00 +- 20.00        25.00 +- 63.26        6.25       50.00           37.50\n"
                                     "  others (per seat)          16             22.92                 -8.33        6.25       50.00           30.00\n"
                                     "easy                         16      0.00 +- 0.00       -31.25 +- 12.25        6.25       18.75            0.00\n"
                                     "  others (per seat)          16             31.25                 10.42        6.25        0.00           40.00\n"
                                     "\n"
                                     "paired difference against normal (the same seeds)\n"
                                     "player                           points/hand                         wins %\n"
                                     "easy                         -56.25 +- 73.16                -25.00 +- 20.00  significant\n"
                                     "\n"
                                     "score lines in the won hands, % of the won hands (normal: 4 wins, others: 11 wins)\n"
                                     "line                          normal      others\n"
                                     "Win                           100.00      100.00\n"
                                     "WinTileFromWall                25.00        0.00\n"
                                     "OnlyPungs                       0.00       18.18\n"
                                     "\n"
                                     "score lines in the won hands, % of the won hands (easy: 0 wins, others: 15 wins)\n"
                                     "line                            easy      others\n"
                                     "Win                             0.00      100.00\n";
        EXPECT_EQ(Std(text), expected);
    }

    TEST(ArenaStatsTest, OnlyTheScoreLinesThatOccurHaveARow) {
        const eastl::string text = FormatArenaReport(MadeUp(false, true));
        EXPECT_TRUE(Contains(text, "Win ")) << Std(text);
        EXPECT_TRUE(Contains(text, "WinTileFromWall"));
        EXPECT_TRUE(Contains(text, "OnlyPungs"));
        EXPECT_FALSE(Contains(text, "LittleFourWinds"));
        EXPECT_FALSE(Contains(text, "ThirteenOrphans"));
    }

    TEST(ArenaStatsTest, TheTimingTableIsThereOnlyWithTiming) {
        const eastl::string quiet = FormatArenaReport(MadeUp(false, true));
        EXPECT_FALSE(Contains(quiet, "wall clock"));
        EXPECT_FALSE(Contains(quiet, "per decision"));
        EXPECT_FALSE(Contains(quiet, "decisions"));
        const eastl::string timed = FormatArenaReport(MadeUp(true, true));
        EXPECT_TRUE(Contains(timed, "wall clock, not deterministic")) << Std(timed);
        EXPECT_TRUE(Contains(timed, "us per decision"));
        // 800000 ns over 400 decisions is 2000 ns, 2 us.
        EXPECT_TRUE(Contains(timed, "400")) << Std(timed);
        EXPECT_TRUE(Contains(timed, "2.00"));
    }

    TEST(ArenaStatsTest, TheSameResultGivesTheSameTextTwice) {
        const ArenaResult result = MadeUp(true, true);
        EXPECT_EQ(Std(FormatArenaReport(result)), Std(FormatArenaReport(result)));
    }

    TEST(ArenaStatsTest, OneFocusPlayerHasNoPairedTable) {
        const eastl::string text = FormatArenaReport(MadeUp(false, false));
        EXPECT_FALSE(Contains(text, "paired difference"));
        EXPECT_FALSE(Contains(text, "significant"));
    }

    TEST(ArenaStatsTest, OneSeedSaysItHasNoInterval) {
        ArenaResult result = MadeUp(false, false);
        result.count = 1;
        result.focus[0].rows.resize(1);
        const eastl::string text = FormatArenaReport(result);
        EXPECT_TRUE(Contains(text, "no interval")) << Std(text);
        EXPECT_FALSE(Contains(text, "+-")) << Std(text);
    }

    TEST(ArenaStatsTest, TheKindTablesCanBeLeftOut) {
        const eastl::string with = FormatArenaReport(MadeUp(false, true));
        const eastl::string without = FormatArenaReport(MadeUp(false, true), ArenaReportOptions{.kinds = false});
        EXPECT_TRUE(Contains(with, "score lines"));
        EXPECT_FALSE(Contains(without, "score lines"));
        EXPECT_FALSE(Contains(without, "OnlyPungs"));
    }

}  // namespace BECore::Mahjong::Arena
