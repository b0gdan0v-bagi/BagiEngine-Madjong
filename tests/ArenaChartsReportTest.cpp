#include <Mahjong/Arena/ArenaChartsCli.h>
#include <gtest/gtest.h>

/// The report and the markdown over a hand-made result (no hand is played), and the program's arguments; two tiny real runs of a
/// couple of seconds in Debug.
namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kSignificantSeeds = 40;
        constexpr uint32_t kFirstSeed = 100;
        constexpr int kWorlds = 25;
        constexpr uint32_t kRealFirstSeed = 11;
        constexpr int kRealSeeds = 2;
        constexpr int kRealWorlds = 4;
        constexpr int kManyJobs = 4;

        /// The cell "pung, 1 shanten, middle": 40 seeds of one row each, the gains 2.0 and 1.0 in turn (mean 1.50, half 0.16), all taken
        /// by Normal. The cell "chow, 0 shanten, early": three seeds of one row each, gains 1, 2 and 3, none taken (a thin cell).
        ChartsResult MadeResult() {
            ChartsResult result;
            result.first = kFirstSeed;
            result.count = kSignificantSeeds;
            result.worlds = kWorlds;
            result.counters.hands = kSignificantSeeds;
            result.counters.claimWindows = 70;
            result.counters.excludedForWin = 3;
            result.counters.dropped = 2;
            result.counters.rows = kSignificantSeeds + 3;
            ClaimClass significant;
            significant.kind = ClaimChartKind::Pung;
            significant.tile = TileGroup::ValueHonour;
            significant.shantenBefore = ShantenBefore::One;
            significant.stage = WallStage::Middle;
            significant.east = true;
            significant.openSets = OpenSets::One;
            ClaimClass thin;
            for (int s = 0; s < kSignificantSeeds; ++s) {
                SeedRows seed;
                seed.seed = kFirstSeed + static_cast<uint32_t>(s);
                ClaimRow row;
                row.cls = significant;
                row.gain = (s % 2 == 0) ? 2.0 : 1.0;
                row.normalTakes = true;
                seed.rows.push_back(row);
                if (s < 3) {
                    ClaimRow small;
                    small.cls = thin;
                    small.gain = static_cast<double>(s + 1);
                    seed.rows.push_back(small);
                }
                result.seeds.push_back(eastl::move(seed));
            }
            return result;
        }

        bool Contains(const String& text, StringView part) {
            return StringView(text).find(part) != StringView::npos;
        }

        using Args = Vector<StringView>;

        std::expected<ChartsArgs, String> Parse(const Args& args) {
            return ParseChartsArgs(Span<const StringView>(args.data(), args.size()));
        }

        int RunProgram(const Args& args, ArenaCliOutput& output) {
            return RunChartsCli(Span<const StringView>(args.data(), args.size()), output);
        }

        String ErrorOf(const Args& args) {
            const std::expected<ChartsArgs, String> parsed = Parse(args);
            EXPECT_FALSE(parsed.has_value());
            return parsed.has_value() ? String() : parsed.error();
        }

    }  // namespace

    TEST(ArenaChartsReportTest, TheTextReportHasTheHeaderAndTheCellLines) {
        const String text = FormatChartsReport(MadeResult());
        const char* const header =
            "Claim charts: seeds 100 .. 139 (40 seeds), 25 worlds per window\n"
            "hands 40, windows with a claim 70, left out for a listed win 3, not analysable 2, rows 43\n"
            "gain = mean points of the claim minus points of passing, in the same worlds; +- is 1.96 standard errors over the seeds (a 95 percent interval), a seed is one sample; "
            "a cell of fewer than 30 rows is few: shown, never significant\n"
            "caveat: against three Normal bots, hidden tiles uniform, the points of this hand only; about one significant cell in 20 is chance, so read only what holds in both "
            "ranges (docs/charts-claims.md)\n"
            "\n"
            "Main table: kind x shanten before x stage of the wall\n"
            "cell                           rows   seeds        gain +- 95 %\n";
        EXPECT_EQ(text.substr(0, StringView(header).size()), String(header)) << text.c_str();
        // A significant cell with its sub-rows: Normal takes every row of it, so the passes line has no numbers.
        EXPECT_TRUE(Contains(text, "pung, 1 shanten, middle          40      40       +1.50 +- 0.16  significant\n"
                                   "  Normal takes                   40      40       +1.50 +- 0.16  significant\n"
                                   "  Normal passes                   0       0                      few\n"))
            << text.c_str();
        // A thin cell is marked few, whatever its interval.
        EXPECT_TRUE(Contains(text, "chow, 0 shanten, early            3       3       +2.00 +- 1.13  few\n"
                                   "  Normal takes                    0       0                      few\n"
                                   "  Normal passes                   3       3       +2.00 +- 1.13  few\n"))
            << text.c_str();
        EXPECT_TRUE(Contains(text, "By East\n")) << text.c_str();
        EXPECT_TRUE(Contains(text, "east                             40      40       +1.50 +- 0.16  significant\n")) << text.c_str();
    }

    TEST(ArenaChartsReportTest, TheMarkdownHasTheSameCellsAsTables) {
        const String text = FormatChartsMarkdown(MadeResult());
        EXPECT_TRUE(Contains(text, "Claim charts: seeds 100 .. 139 (40 seeds), 25 worlds per window.\n\nhands 40, windows with a claim 70, left out for a listed win 3, not analysable 2, rows 43.\n"))
            << text.c_str();
        EXPECT_TRUE(Contains(text, "\n#### Main table: kind x shanten before x stage of the wall\n\n| class | claims | rows | seeds | gain +- 95 % | |\n|---|---|---:|---:|---:|---|\n"))
            << text.c_str();
        EXPECT_TRUE(Contains(text, "| pung, 1 shanten, middle | all | 40 | 40 | +1.50 +- 0.16 | significant |\n"
                                   "|  | Normal takes | 40 | 40 | +1.50 +- 0.16 | significant |\n"
                                   "|  | Normal passes | 0 | 0 |  | few |\n"))
            << text.c_str();
        EXPECT_TRUE(Contains(text, "| chow, 0 shanten, early | all | 3 | 3 | +2.00 +- 1.13 | few |\n")) << text.c_str();
    }

    TEST(ArenaChartsReportTest, NoNumberIsNanAndAnEmptyResultPrintsBlanks) {
        for (const ChartsResult& result : {MadeResult(), ChartsResult{}}) {
            for (const String& text : {FormatChartsReport(result), FormatChartsMarkdown(result)}) {
                EXPECT_FALSE(Contains(text, "nan")) << text.c_str();
                EXPECT_FALSE(Contains(text, "inf")) << text.c_str();
            }
        }
        const String empty = FormatChartsReport(ChartsResult{});
        EXPECT_TRUE(Contains(empty, "chow, 0 shanten, early            0       0                      few\n")) << empty.c_str();
        EXPECT_EQ(FormatChartsReport(MadeResult()), FormatChartsReport(MadeResult()));
    }

    TEST(ArenaChartsReportTest, BadArgumentsAreAnErrorThatNamesTheOption) {
        EXPECT_NE(ErrorOf({"--seeds", "0"}).find("--seeds"), String::npos);
        EXPECT_NE(ErrorOf({"--worlds", "0"}).find("--worlds"), String::npos);
        EXPECT_NE(ErrorOf({"--jobs", "300"}).find("--jobs"), String::npos);
        EXPECT_NE(ErrorOf({"--first", "-1"}).find("--first"), String::npos);
        EXPECT_NE(ErrorOf({"--seeds", "many"}).find("--seeds"), String::npos);
        EXPECT_NE(ErrorOf({"--seeds"}).find("--seeds"), String::npos);
        EXPECT_NE(ErrorOf({"--turbo"}).find("--turbo"), String::npos);
        // Seeds that run past the largest seed are the range's fault, not one option's.
        EXPECT_FALSE(ErrorOf({"--first", "4294967295", "--seeds", "2"}).empty());
    }

    TEST(ArenaChartsReportTest, TheProgramRefusesBadArgumentsWithTheUsageAndExitTwo) {
        ArenaCliOutput output;
        EXPECT_EQ(RunProgram({"--worlds", "0"}, output), kExitBadArguments);
        EXPECT_TRUE(output.out.empty());
        EXPECT_TRUE(Contains(output.err, "--worlds"));
        EXPECT_TRUE(Contains(output.err, "usage: BagiMahjongCharts"));
    }

    TEST(ArenaChartsReportTest, HelpWinsAndExitsZero) {
        ArenaCliOutput output;
        EXPECT_EQ(RunProgram({"--help", "--seeds", "0"}, output), kExitOk);
        EXPECT_TRUE(output.err.empty());
        EXPECT_TRUE(Contains(output.out, "usage: BagiMahjongCharts"));
        for (const StringView option : {"--first", "--seeds", "--worlds", "--jobs", "--markdown"}) {
            EXPECT_TRUE(Contains(output.out, option)) << option.data();
        }
    }

    TEST(ArenaChartsReportTest, ArgumentsFillTheConfigAndJobsZeroIsTheHardware) {
        const std::expected<ChartsArgs, String> parsed = Parse({"--first", "7", "--seeds", "3", "--worlds", "9", "--jobs", "0", "--markdown"});
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ(parsed->config.first, 7U);
        EXPECT_EQ(parsed->config.count, 3);
        EXPECT_EQ(parsed->config.worlds, 9);
        EXPECT_GE(parsed->config.jobs, 1);
        EXPECT_LE(parsed->config.jobs, kMaxJobs);
        EXPECT_TRUE(parsed->markdown);
        const std::expected<ChartsArgs, String> defaults = Parse({});
        ASSERT_TRUE(defaults.has_value());
        EXPECT_EQ(defaults->config.worlds, kDefaultChartWorlds);
        EXPECT_EQ(defaults->config.count, kDefaultChartSeeds);
        EXPECT_FALSE(defaults->markdown);
    }

    TEST(ArenaChartsReportTest, ATinyRealRunGivesTheSameBytesAtOneAndSeveralJobs) {
        const String first = Format("{}", kRealFirstSeed);
        const String seeds = Format("{}", kRealSeeds);
        const String worlds = Format("{}", kRealWorlds);
        const String many = Format("{}", kManyJobs);
        ArenaCliOutput one;
        ArenaCliOutput several;
        ASSERT_EQ(RunProgram({"--first", first, "--seeds", seeds, "--worlds", worlds, "--jobs", "1"}, one), kExitOk) << one.err.c_str();
        ASSERT_EQ(RunProgram({"--first", first, "--seeds", seeds, "--worlds", worlds, "--jobs", many}, several), kExitOk) << several.err.c_str();
        EXPECT_FALSE(one.out.empty());
        EXPECT_EQ(one.out, several.out);
        EXPECT_TRUE(Contains(one.out, "Main table: kind x shanten before x stage of the wall"));
        ArenaCliOutput markdown;
        ASSERT_EQ(RunProgram({"--first", first, "--seeds", seeds, "--worlds", worlds, "--jobs", many, "--markdown"}, markdown), kExitOk) << markdown.err.c_str();
        EXPECT_TRUE(Contains(markdown.out, "| class | claims | rows | seeds | gain +- 95 % | |"));
    }

}  // namespace BECore::Mahjong::Arena
