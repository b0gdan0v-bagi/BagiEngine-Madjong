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

        SeedRow Row(uint32_t seed, int points, int wins, int drawn, int declared, int winScore, int gaveWin, int lostHands, int standingScore) {
            SeedRow row;
            row.seed = seed;
            row.pointsOfFocus = points;
            row.winsOfFocus = wins;
            row.drawn = drawn;
            row.declared = declared;
            row.winScore = winScore;
            row.gaveWin = gaveWin;
            row.lostHands = lostHands;
            row.standingScore = standingScore;
            return row;
        }

        size_t Index(ScoreKind kind) {
            return static_cast<size_t>(kind);
        }

        /// Normal over four seeds, worked out by hand: points a hand 100, 50, 0, -50 (mean 25, sd 64.5497), wins % 50, 25, 0, 25 (mean 25,
        /// sd 20.4124), 4 wins of 16 hands, 1 drawn hand, 8 declarations, winning scores 80 + 40 + 30 = 150. How the hands end: the lost
        /// hands are the 4 hands of a seed less its wins and drawn ones (2, 3, 3, 3); gave win % 25, 25, 0, 50 (mean 25, sd 20.4124), the
        /// standing score of a seed 40, 30, 50, 20 (mean 35, sd 12.9099, half 12.6517); 3 of the 4 wins are self-draws.
        FocusResult NormalFocus() {
            FocusResult focus;
            focus.player = Player(kNormal);
            focus.rows = {Row(kFirstSeed, 400, 2, 0, 3, 80, 1, 2, 80), Row(kFirstSeed + 1, 200, 1, 0, 2, 40, 1, 3, 90), Row(kFirstSeed + 2, 0, 0, 1, 1, 0, 0, 3, 150),
                          Row(kFirstSeed + 3, -200, 1, 0, 2, 30, 2, 3, 60)};
            focus.focus.hands = 16;
            focus.focus.wins = 4;
            focus.focus.points = 400;
            focus.focus.drawn = 1;
            focus.focus.declared = 8;
            focus.focus.winScore = 150;
            focus.focus.selfDraws = 3;
            focus.focus.lost = 11;
            focus.focus.gaveWin = 4;
            focus.focus.lostPoints = -870;
            focus.focus.standingScore = 380;
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
            // 48 seat-hands less 11 won and 3 drawn are lost: 34 of them, 5 of the 11 wins self-drawn, 3 given by an other seat (the 7
            // wins that were not self-draws were given 4 times by the focus player and 3 times by the others).
            focus.others.selfDraws = 5;
            focus.others.lost = 34;
            focus.others.gaveWin = 3;
            focus.others.lostPoints = -2635;
            focus.others.standingScore = 1411;
            focus.others.winsByKind[Index(ScoreKind::Win)] = 11;
            focus.others.winsByKind[Index(ScoreKind::OnlyPungs)] = 2;
            focus.others.decisions = 1200;
            focus.others.nanoseconds = 600000;
            focus.winsBySeat = {1, 1, 1, 1};
            return focus;
        }

        /// Easy on the same seeds, with no win at all: the wins % differ from Normal's by -50, -25, 0, -25 (mean -25, sd 20.4124, half
        /// 20.0043, significant), the points a hand by -150, -75, -25, 25 (mean -56.25, half 73.16, not significant). Its lost hands are 4,
        /// 4, 3, 4: gave win % 75, 25, 25, 50 (mean 43.75, half 23.4569; against Normal +18.75, half 23.4569, not significant), standing
        /// score of a seed 60, 50, 55, 65 (mean 57.5, half 6.3259; against Normal 20, 20, 5, 45: mean 22.5, sd 16.5831, half 16.2514,
        /// significant).
        FocusResult EasyFocus() {
            FocusResult focus;
            focus.player = Player(kEasy);
            focus.rows = {Row(kFirstSeed, -200, 0, 0, 1, 0, 3, 4, 240), Row(kFirstSeed + 1, -100, 0, 0, 1, 0, 1, 4, 200), Row(kFirstSeed + 2, -100, 0, 1, 0, 0, 1, 3, 165),
                          Row(kFirstSeed + 3, -100, 0, 0, 1, 0, 2, 4, 260)};
            focus.focus.hands = 16;
            focus.focus.points = -500;
            focus.focus.drawn = 1;
            focus.focus.declared = 3;
            focus.focus.lost = 15;
            focus.focus.gaveWin = 7;
            focus.focus.lostPoints = -1180;
            focus.focus.standingScore = 865;
            focus.others.hands = 48;
            focus.others.wins = 15;
            focus.others.points = 500;
            focus.others.drawn = 3;
            focus.others.winScore = 600;
            focus.others.selfDraws = 6;
            focus.others.lost = 30;
            focus.others.gaveWin = 2;
            focus.others.lostPoints = -2301;
            focus.others.standingScore = 1272;
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

    TEST(ArenaStatsTest, TheStyleSeriesAreThePerSeedShareAndStandingScore) {
        const FocusResult normal = NormalFocus();
        // gave win %: 1, 1, 0, 2 of 4 hands.
        const eastl::vector<double> gave = SeedSeries(normal, SeedMetric::GaveWinPercent);
        ASSERT_EQ(gave.size(), 4u);
        EXPECT_EQ(gave[0], 25.0);
        EXPECT_EQ(gave[2], 0.0);
        EXPECT_EQ(gave[3], 50.0);
        EXPECT_NEAR(MetricInterval(normal, SeedMetric::GaveWinPercent).mean, 25.0, kTolerance);
        EXPECT_NEAR(MetricInterval(normal, SeedMetric::GaveWinPercent).sd, std::sqrt(1250.0 / 3.0), kTolerance);
        // standing score: the sum over the lost hands of the seed divided by their number, 80 / 2, 90 / 3, 150 / 3, 60 / 3.
        const eastl::vector<double> standing = SeedSeries(normal, SeedMetric::StandingScore);
        ASSERT_EQ(standing.size(), 4u);
        EXPECT_EQ(standing[0], 40.0);
        EXPECT_EQ(standing[1], 30.0);
        EXPECT_EQ(standing[2], 50.0);
        EXPECT_EQ(standing[3], 20.0);
        const MeanInterval interval = MetricInterval(normal, SeedMetric::StandingScore);
        EXPECT_NEAR(interval.mean, 35.0, kTolerance);
        EXPECT_NEAR(interval.sd, std::sqrt(500.0 / 3.0), kTolerance);
        EXPECT_NEAR(interval.half, 12.6517, 1e-4);
    }

    TEST(ArenaStatsTest, ASeedWithNoLostHandHasNoStandingScore) {
        FocusResult normal = NormalFocus();
        normal.rows[2] = Row(kFirstSeed + 2, 0, 4, 0, 1, 0, 0, 0, 0);
        // The seed is left out of the series, so the series is shorter than the rows; the share of the given wins keeps it (0 %).
        const eastl::vector<double> standing = SeedSeries(normal, SeedMetric::StandingScore);
        ASSERT_EQ(standing.size(), 3u);
        EXPECT_EQ(standing[0], 40.0);
        EXPECT_EQ(standing[1], 30.0);
        EXPECT_EQ(standing[2], 20.0);
        EXPECT_EQ(MetricInterval(normal, SeedMetric::StandingScore).n, 3u);
        EXPECT_EQ(SeedSeries(normal, SeedMetric::GaveWinPercent).size(), 4u);

        // Easy against it: the seeds where both have a value are 0, 1 and 3 (60 - 40, 50 - 30, 65 - 20).
        const FocusResult easy = EasyFocus();
        const MeanInterval difference = PairedDifference(easy, normal, SeedMetric::StandingScore);
        EXPECT_EQ(difference.n, 3u);
        EXPECT_NEAR(difference.mean, 85.0 / 3.0, kTolerance);
        EXPECT_NEAR(difference.sd, std::sqrt(625.0 / 3.0), kTolerance);
        EXPECT_NEAR(difference.half, kIntervalZ * std::sqrt(625.0 / 3.0) / std::sqrt(3.0), kTolerance);
        EXPECT_TRUE(IsSignificant(difference));
        // The other side has none in seed 3: the seeds are 0, 1 and 2 (60 - 40, 50 - 30, 55 - 50).
        FocusResult easyWithout = EasyFocus();
        easyWithout.rows[3] = Row(kFirstSeed + 3, 0, 0, 4, 0, 0, 0, 0, 0);
        const MeanInterval other = PairedDifference(easyWithout, NormalFocus(), SeedMetric::StandingScore);
        EXPECT_EQ(other.n, 3u);
        EXPECT_NEAR(other.mean, 15.0, kTolerance);
        // The seeds that differ still give no answer.
        FocusResult shifted = EasyFocus();
        shifted.rows[1].seed += 1;
        EXPECT_EQ(PairedDifference(shifted, normal, SeedMetric::StandingScore).n, 0u);
    }

    TEST(ArenaStatsTest, ThePairedDifferenceOfTheStyleMetrics) {
        const FocusResult normal = NormalFocus();
        const FocusResult easy = EasyFocus();
        // gave win %: 75 - 25, 25 - 25, 25 - 0, 50 - 50 = 50, 0, 25, 0: mean 18.75, sum of squares 1718.75, half 1.96 * sd / 2.
        const MeanInterval gave = PairedDifference(easy, normal, SeedMetric::GaveWinPercent);
        EXPECT_EQ(gave.n, 4u);
        EXPECT_NEAR(gave.mean, 18.75, kTolerance);
        EXPECT_NEAR(gave.half, kIntervalZ * std::sqrt(1718.75 / 3.0) / 2.0, kTolerance);
        EXPECT_FALSE(IsSignificant(gave));
        // standing score: 20, 20, 5, 45: mean 22.5, sum of squares 825, the interval excludes 0.
        const MeanInterval standing = PairedDifference(easy, normal, SeedMetric::StandingScore);
        EXPECT_EQ(standing.n, 4u);
        EXPECT_NEAR(standing.mean, 22.5, kTolerance);
        EXPECT_NEAR(standing.half, kIntervalZ * std::sqrt(825.0 / 3.0) / 2.0, kTolerance);
        EXPECT_TRUE(IsSignificant(standing));
        EXPECT_NEAR(PairedDifference(normal, easy, SeedMetric::StandingScore).mean, -22.5, kTolerance);
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
        for (const SeedMetric metric : {SeedMetric::GaveWinPercent, SeedMetric::StandingScore}) {
            const MeanInterval same = PairedDifference(run->focus[1], run->focus[0], metric);
            EXPECT_EQ(same.mean, 0.0) << static_cast<int>(metric);
            EXPECT_EQ(same.half, 0.0) << static_cast<int>(metric);
            EXPECT_FALSE(IsSignificant(same)) << static_cast<int>(metric);
        }
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
                                     "how the hands end\n"
                                     "player                        gave win %  self-draw % of wins      standing score  points/lost hand\n"
                                     "normal                    25.00 +- 20.00                75.00      35.00 +- 12.65            -79.09\n"
                                     "  others (per seat)                 6.25                45.45               41.50            -77.50\n"
                                     "easy                      43.75 +- 23.46                 0.00       57.50 +- 6.33            -78.67\n"
                                     "  others (per seat)                 4.17                40.00               42.40            -76.70\n"
                                     "gave win %: the hands another seat won on the seat's discard (or by robbing its kong), of all its hands\n"
                                     "lost hand: a hand another seat won; standing score: the seat's mean score over its lost hands; points/lost hand: its mean change of points over them\n"
                                     "\n"
                                     "paired difference against normal (the same seeds)\n"
                                     "player                           points/hand                         wins %\n"
                                     "easy                         -56.25 +- 73.16                -25.00 +- 20.00  significant\n"
                                     "\n"
                                     "paired difference against normal (the same seeds), how the hands end\n"
                                     "player                        gave win %                   standing score\n"
                                     "easy                     +18.75 +- 23.46                  +22.50 +- 16.25  significant\n"
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
        EXPECT_TRUE(Contains(text, "how the hands end")) << "the table of the hands is there for one player too";
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
