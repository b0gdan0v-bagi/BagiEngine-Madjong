#include <Mahjong/Arena/ArenaChartsReport.h>
#include <Mahjong/Arena/ArenaChartsRun.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>

/// A few seconds in Debug: a handful of whole hands of four Normal seats and a few worlds per measured window.
namespace BECore::Mahjong::Arena {

    namespace {

        using namespace Tests;

        constexpr uint32_t kFirstSeed = 11;
        constexpr int kSeeds = 4;
        constexpr int kHalf = 2;
        constexpr int kWorlds = 6;
        constexpr int kManyJobs = 8;
        constexpr int kSomeJobs = 3;
        constexpr int kTooManyJobs = 300;
        constexpr uint32_t kFailingSeed = 13;
        constexpr uint32_t kLaterFailingSeed = 15;
        constexpr double kBigGain = 2.0;
        constexpr double kSmallGain = 1.0;
        constexpr uint32_t kGoldenFirst = 1;
        constexpr int kGoldenSeeds = 3;
        constexpr int kGoldenWorlds = 20;
        constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
        constexpr uint64_t kFnvPrime = 1099511628211ULL;
        constexpr uint64_t kGoldenReportHash = 11082305448135879985ULL;
        constexpr uint64_t kGoldenMarkdownHash = 8734939666423079044ULL;

        /// FNV-1a, 64 bit, over the bytes of `text`.
        uint64_t Fnv1a64(const String& text) {
            uint64_t hash = kFnvOffset;
            for (const char c : text) {
                hash ^= static_cast<uint8_t>(c);
                hash *= kFnvPrime;
            }
            return hash;
        }

        ChartsConfig ConfigOf(uint32_t first, int count, int jobs) {
            ChartsConfig config;
            config.first = first;
            config.count = count;
            config.worlds = kWorlds;
            config.jobs = jobs;
            return config;
        }

        ChartsResult RunOk(const ChartsConfig& config) {
            const std::expected<ChartsResult, String> result = RunCharts(config);
            EXPECT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().c_str());
            return result.value_or(ChartsResult{});
        }

        /// The run of the whole range at a few jobs, made once for the cases that only read it.
        const ChartsResult& Whole() {
            static const ChartsResult whole = RunOk(ConfigOf(kFirstSeed, kSeeds, kSomeJobs));
            return whole;
        }

        bool SameRows(const ChartsResult& a, const ChartsResult& b) {
            if (a.seeds.size() != b.seeds.size()) {
                return false;
            }
            for (size_t s = 0; s < a.seeds.size(); ++s) {
                const Vector<ClaimRow>& x = a.seeds[s].rows;
                const Vector<ClaimRow>& y = b.seeds[s].rows;
                if (a.seeds[s].seed != b.seeds[s].seed || x.size() != y.size()) {
                    return false;
                }
                for (size_t i = 0; i < x.size(); ++i) {
                    if (!(x[i].cls == y[i].cls) || !(x[i].claim == y[i].claim) || x[i].gain != y[i].gain || x[i].normalTakes != y[i].normalTakes) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SameCounters(const ChartsCounters& a, const ChartsCounters& b) {
            return a.hands == b.hands && a.claimWindows == b.claimWindows && a.excludedForWin == b.excludedForWin && a.dropped == b.dropped && a.rows == b.rows;
        }

        bool SameCell(const CellStats& a, const CellStats& b) {
            return a.rows == b.rows && a.seeds == b.seeds && a.few == b.few && a.significant == b.significant && a.gain.n == b.gain.n && a.gain.mean == b.gain.mean && a.gain.half == b.gain.half;
        }

        bool SameSummary(const ChartsResult& a, const ChartsResult& b, const ClaimFilter& filter) {
            const Cell x = Summarise(a, filter);
            const Cell y = Summarise(b, filter);
            return SameCell(x, y) && SameCell(x.takes, y.takes) && SameCell(x.passes, y.passes);
        }

        ClaimFilter KindFilter(ClaimChartKind kind) {
            ClaimFilter filter;
            filter.kind = kind;
            return filter;
        }

        /// A result of hand-made rows: `perSeed` rows in each of `seeds` seeds, the gain of the i-th seed alternating around `kSmallGain`.
        ChartsResult MadeResult(int seeds, int perSeed) {
            ChartsResult result;
            for (int s = 0; s < seeds; ++s) {
                SeedRows seed;
                seed.seed = static_cast<uint32_t>(s);
                for (int i = 0; i < perSeed; ++i) {
                    ClaimRow row;
                    row.gain = (s % 2 == 0) ? kBigGain : kSmallGain;
                    row.normalTakes = i == 0;
                    seed.rows.push_back(row);
                }
                result.seeds.push_back(eastl::move(seed));
            }
            return result;
        }

        DiscardChartsResult RunDiscardOk(const ChartsConfig& config) {
            const std::expected<DiscardChartsResult, String> result = RunDiscardCharts(config);
            EXPECT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().c_str());
            return result.value_or(DiscardChartsResult{});
        }

        const DiscardChartsResult& WholeDiscards() {
            static const DiscardChartsResult whole = RunDiscardOk(ConfigOf(kFirstSeed, kSeeds, kSomeJobs));
            return whole;
        }

        bool SameDiscardCounters(const DiscardCounters& a, const DiscardCounters& b) {
            return a.hands == b.hands && a.ownTurns == b.ownTurns && a.otherStep == b.otherStep && a.noValue == b.noValue && a.sameAsNormal == b.sameAsNormal && a.dropped == b.dropped &&
                   a.rows == b.rows;
        }

        bool SameDiscardRows(const DiscardChartsResult& a, const DiscardChartsResult& b) {
            if (a.seeds.size() != b.seeds.size()) {
                return false;
            }
            for (size_t s = 0; s < a.seeds.size(); ++s) {
                const Vector<DiscardRow>& x = a.seeds[s].rows;
                const Vector<DiscardRow>& y = b.seeds[s].rows;
                if (a.seeds[s].seed != b.seeds[s].seed || x.size() != y.size()) {
                    return false;
                }
                for (size_t i = 0; i < x.size(); ++i) {
                    if (!(x[i].cls == y[i].cls) || !(x[i].pair == y[i].pair) || x[i].gain != y[i].gain) {
                        return false;
                    }
                }
            }
            return true;
        }

        DiscardFilter ValueHeldFilter(ValueHeld held) {
            DiscardFilter filter;
            filter.valueHeld = held;
            return filter;
        }

        /// A result of hand-made rows: `perSeed` rows in each of `seeds` seeds, the gain alternating by seed as in `MadeResult`.
        DiscardChartsResult MadeDiscardResult(int seeds, int perSeed) {
            DiscardChartsResult result;
            for (int s = 0; s < seeds; ++s) {
                SeedDiscardRows seed;
                seed.seed = static_cast<uint32_t>(s);
                for (int i = 0; i < perSeed; ++i) {
                    DiscardRow row;
                    row.gain = (s % 2 == 0) ? kBigGain : kSmallGain;
                    seed.rows.push_back(row);
                }
                result.seeds.push_back(eastl::move(seed));
            }
            return result;
        }

        /// East holds 13 tiles and draws the first of the live wall: an own discard turn whose valued discard differs from Normal's.
        TableView OwnTurnWithAPair() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 2p 4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Ew";
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            spec.live = "Wd Rd Wd 1p";
            spec.loose = "9m 9s";
            const HandModel model = MakeModel(spec);
            return MakeTableView(model, kSeat0);
        }

        /// Seat 1 holds a pair of 5p and 4p 6p, so East's 5p is a chow or a pung for it; every tile is already visible when `spent`.
        TableView ChowAndPungWindow(bool spent) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "4p 5p 5p 6p 1s 2s 7s 8s 3m 4m 8m 9m Ww";
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            spec.live = "5p Rd Gd Wd";
            spec.loose = "9m 9s";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            TableView view = MakeTableView(model, kSeat1);
            if (spent) {
                view.visible.fill(kCopiesPerTile);
            }
            return view;
        }

    }  // namespace

    /// The bytes of the claim study's report are fixed: the engine of seeds is shared with the discard charts and must not move them.
    TEST(ArenaChartsRunTest, TheClaimReportKeepsItsBytes) {
        ChartsConfig config;
        config.first = kGoldenFirst;
        config.count = kGoldenSeeds;
        config.worlds = kGoldenWorlds;
        config.jobs = 1;
        const ChartsResult result = RunOk(config);
        ASSERT_GT(result.counters.rows, 0);
        const String report = FormatChartsReport(result);
        const String markdown = FormatChartsMarkdown(result);
        EXPECT_EQ(Fnv1a64(report), kGoldenReportHash);
        EXPECT_EQ(Fnv1a64(markdown), kGoldenMarkdownHash);
    }

    TEST(ArenaChartsRunTest, TheSameResultAtAnyNumberOfJobs) {
        const ChartsResult one = RunOk(ConfigOf(kFirstSeed, kSeeds, 1));
        ASSERT_GT(one.counters.rows, 0);
        const ClaimFilter everything;
        for (const int jobs : {kSomeJobs, kManyJobs}) {
            const ChartsResult other = RunOk(ConfigOf(kFirstSeed, kSeeds, jobs));
            EXPECT_TRUE(SameCounters(one.counters, other.counters)) << "jobs " << jobs;
            EXPECT_TRUE(SameRows(one, other)) << "jobs " << jobs;
            EXPECT_TRUE(SameSummary(one, other, everything)) << "jobs " << jobs;
            EXPECT_TRUE(SameSummary(one, other, KindFilter(ClaimChartKind::Pung))) << "jobs " << jobs;
        }
    }

    TEST(ArenaChartsRunTest, TwoHalvesOfARangeAddUpToTheWholeRange) {
        const ChartsResult& whole = Whole();
        const ChartsResult head = RunOk(ConfigOf(kFirstSeed, kHalf, kSomeJobs));
        const ChartsResult tail = RunOk(ConfigOf(kFirstSeed + kHalf, kSeeds - kHalf, kSomeJobs));
        ChartsCounters sum = head.counters;
        sum.Add(tail.counters);
        EXPECT_TRUE(SameCounters(whole.counters, sum));
        EXPECT_EQ(whole.counters.hands, kSeeds);
        ASSERT_EQ(whole.seeds.size(), static_cast<size_t>(kSeeds));
        int64_t rows = 0;
        for (const SeedRows& seed : whole.seeds) {
            rows += static_cast<int64_t>(seed.rows.size());
        }
        EXPECT_EQ(rows, whole.counters.rows);
        EXPECT_GE(whole.counters.claimWindows, whole.counters.excludedForWin);
        for (size_t i = 0; i < whole.seeds.size(); ++i) {
            const SeedRows& part = i < static_cast<size_t>(kHalf) ? head.seeds[i] : tail.seeds[i - static_cast<size_t>(kHalf)];
            EXPECT_EQ(whole.seeds[i].seed, kFirstSeed + static_cast<uint32_t>(i));
            EXPECT_EQ(part.seed, whole.seeds[i].seed);
            EXPECT_EQ(part.rows.size(), whole.seeds[i].rows.size());
        }
    }

    TEST(ArenaChartsRunTest, AnInvalidConfigIsAnErrorThatNamesTheNumber) {
        ChartsConfig config = ConfigOf(kFirstSeed, kSeeds, 1);
        config.count = 0;
        EXPECT_FALSE(RunCharts(config).has_value());
        EXPECT_FALSE(ValidateChartsConfig(config).empty());
        config = ConfigOf(kFirstSeed, kSeeds, 1);
        config.worlds = 0;
        const std::expected<ChartsResult, String> noWorlds = RunCharts(config);
        ASSERT_FALSE(noWorlds.has_value());
        EXPECT_NE(noWorlds.error().find("worlds"), String::npos);
        for (const int jobs : {0, kTooManyJobs}) {
            const std::expected<ChartsResult, String> badJobs = RunCharts(ConfigOf(kFirstSeed, kSeeds, jobs));
            ASSERT_FALSE(badJobs.has_value()) << "jobs " << jobs;
            EXPECT_NE(badJobs.error().find("threads"), String::npos);
        }
        EXPECT_TRUE(ValidateChartsConfig(ConfigOf(kFirstSeed, kSeeds, 1)).empty());
    }

    TEST(ArenaChartsRunTest, AHandThatFailsIsAnErrorOfTheLowestSeedWhateverTheJobs) {
        const ChartsHandSource source = [](uint32_t seed) -> std::expected<ClaimWindows, String> {
            if (seed == kFailingSeed || seed == kLaterFailingSeed) {
                return std::unexpected(Format("seed {}: the model refused the action", seed));
            }
            return ClaimWindows{};
        };
        for (const int jobs : {1, kSomeJobs, kManyJobs}) {
            const std::expected<ChartsResult, String> result = RunCharts(ConfigOf(kFirstSeed, kSeeds, jobs), source);
            ASSERT_FALSE(result.has_value()) << "jobs " << jobs;
            EXPECT_NE(result.error().find(Format("seed {}:", kFailingSeed)), String::npos) << result.error().c_str();
            EXPECT_EQ(result.error().find(Format("seed {}:", kLaterFailingSeed)), String::npos) << result.error().c_str();
        }
    }

    TEST(ArenaChartsRunTest, AWindowThatCannotBeMeasuredIsAnErrorNamingTheSeedAndTheWindow) {
        const ChartsHandSource source = [](uint32_t seed) -> std::expected<ClaimWindows, String> {
            ClaimWindows windows;
            if (seed == kFailingSeed) {
                windows.windows.push_back({0, TableView{}});
            }
            return windows;
        };
        const std::expected<ChartsResult, String> result = RunCharts(ConfigOf(kFirstSeed, kSeeds, kSomeJobs), source);
        ASSERT_FALSE(result.has_value());
        EXPECT_NE(result.error().find(Format("seed {} window 0", kFailingSeed)), String::npos) << result.error().c_str();
    }

    TEST(ArenaChartsRunTest, AWindowThatCannotBeAnalysedIsCountedAsDroppedAndMeasuredOnesAsRows) {
        const ChartsHandSource source = [](uint32_t seed) -> std::expected<ClaimWindows, String> {
            ClaimWindows windows;
            windows.claimWindows = 2;
            windows.windows.push_back({0, ChowAndPungWindow(false)});
            windows.windows.push_back({1, ChowAndPungWindow(true)});
            (void)seed;
            return windows;
        };
        const ChartsResult result = [&] {
            const std::expected<ChartsResult, String> run = RunCharts(ConfigOf(kFirstSeed, kHalf, kSomeJobs), source);
            EXPECT_TRUE(run.has_value()) << (run.has_value() ? "" : run.error().c_str());
            return run.value_or(ChartsResult{});
        }();
        EXPECT_EQ(result.counters.hands, kHalf);
        EXPECT_EQ(result.counters.claimWindows, 2 * kHalf);
        EXPECT_EQ(result.counters.dropped, kHalf);
        EXPECT_GT(result.counters.rows, 0);
        for (const SeedRows& seed : result.seeds) {
            EXPECT_EQ(static_cast<int64_t>(seed.rows.size()) * kHalf, result.counters.rows);
        }
    }

    TEST(ArenaChartsRunTest, TheIntervalOfACellIsMeanCiOfThePerSeedMeans) {
        const ChartsResult& result = Whole();
        for (const ClaimChartKind kind : {ClaimChartKind::Chow, ClaimChartKind::Pung}) {
            Vector<double> all;
            Vector<double> taken;
            Vector<double> passed;
            int64_t rows = 0;
            for (const SeedRows& seed : result.seeds) {
                double sum = 0.0;
                double takenSum = 0.0;
                double passedSum = 0.0;
                int count = 0;
                int takenCount = 0;
                int passedCount = 0;
                for (const ClaimRow& row : seed.rows) {
                    if (row.cls.kind != kind) {
                        continue;
                    }
                    sum += row.gain;
                    ++count;
                    (row.normalTakes ? takenSum : passedSum) += row.gain;
                    ++(row.normalTakes ? takenCount : passedCount);
                }
                rows += count;
                if (count > 0) {
                    all.push_back(sum / count);
                }
                if (takenCount > 0) {
                    taken.push_back(takenSum / takenCount);
                }
                if (passedCount > 0) {
                    passed.push_back(passedSum / passedCount);
                }
            }
            const Cell cell = Summarise(result, KindFilter(kind));
            const MeanInterval expected = MeanCi(all);
            EXPECT_EQ(cell.rows, rows);
            EXPECT_EQ(cell.seeds, static_cast<int64_t>(all.size()));
            EXPECT_EQ(cell.gain.n, expected.n);
            EXPECT_EQ(cell.gain.mean, expected.mean);
            EXPECT_EQ(cell.gain.half, expected.half);
            EXPECT_EQ(cell.takes.gain.mean, MeanCi(taken).mean);
            EXPECT_EQ(cell.takes.gain.half, MeanCi(taken).half);
            EXPECT_EQ(cell.passes.gain.mean, MeanCi(passed).mean);
            EXPECT_EQ(cell.passes.seeds, static_cast<int64_t>(passed.size()));
            EXPECT_EQ(cell.takes.rows + cell.passes.rows, cell.rows);
        }
    }

    TEST(ArenaChartsRunTest, ACellUnderTheFewRowsLimitIsFewAndNeverSignificant) {
        // Eight seeds of two rows: 16 rows, the seeds' means 2 and 1 alternate, so the interval excludes 0, and still it is thin.
        const ChartsResult thin = MadeResult(8, 2);
        const Cell thinCell = Summarise(thin, ClaimFilter{});
        EXPECT_EQ(thinCell.rows, 16);
        EXPECT_LT(thinCell.rows, kFewRows);
        EXPECT_TRUE(thinCell.few);
        EXPECT_TRUE(IsSignificant(thinCell.gain));
        EXPECT_FALSE(thinCell.significant);

        const ChartsResult thick = MadeResult(20, 2);
        const Cell thickCell = Summarise(thick, ClaimFilter{});
        EXPECT_EQ(thickCell.rows, 40);
        EXPECT_FALSE(thickCell.few);
        EXPECT_TRUE(thickCell.significant);
        // A row in each seed is taken by Normal: 20 rows are under the limit, the 20 others are too.
        EXPECT_TRUE(thickCell.takes.few);
        EXPECT_FALSE(thickCell.takes.significant);
        EXPECT_TRUE(thickCell.passes.few);
    }

    TEST(ArenaChartsRunTest, AnEmptyCellHasNoNumbers) {
        const ChartsResult& result = Whole();
        // A chow of an honour tile does not exist.
        ClaimFilter none = KindFilter(ClaimChartKind::Chow);
        none.tile = TileGroup::OtherHonour;
        const Cell cell = Summarise(result, none);
        EXPECT_EQ(cell.rows, 0);
        EXPECT_EQ(cell.seeds, 0);
        EXPECT_EQ(cell.gain.n, 0U);
        EXPECT_FALSE(cell.gain.hasInterval);
        EXPECT_TRUE(cell.few);
        EXPECT_FALSE(cell.significant);
        const Cell nothing = Summarise(ChartsResult{}, ClaimFilter{});
        EXPECT_EQ(nothing.rows, 0);
        EXPECT_TRUE(nothing.few);
        EXPECT_FALSE(nothing.significant);
    }

    TEST(ArenaChartsRunTest, TheOneWayTablesAddUpTheCellsOfTheMainTable) {
        const ChartsResult& result = Whole();
        int64_t total = 0;
        Array<int64_t, EnumUtils<ClaimChartKind>::Count()> byKind{};
        Array<int64_t, EnumUtils<ShantenBefore>::Count()> byShanten{};
        Array<int64_t, EnumUtils<WallStage>::Count()> byStage{};
        for (const ClaimChartKind kind : EnumUtils<ClaimChartKind>::Values()) {
            for (const ShantenBefore shanten : EnumUtils<ShantenBefore>::Values()) {
                for (const WallStage stage : EnumUtils<WallStage>::Values()) {
                    ClaimFilter filter;
                    filter.kind = kind;
                    filter.shantenBefore = shanten;
                    filter.stage = stage;
                    const Cell cell = Summarise(result, filter);
                    EXPECT_EQ(cell.takes.rows + cell.passes.rows, cell.rows);
                    total += cell.rows;
                    byKind[static_cast<size_t>(kind)] += cell.rows;
                    byShanten[static_cast<size_t>(shanten)] += cell.rows;
                    byStage[static_cast<size_t>(stage)] += cell.rows;
                }
            }
        }
        EXPECT_EQ(total, result.counters.rows);
        EXPECT_EQ(Summarise(result, ClaimFilter{}).rows, result.counters.rows);
        for (const ClaimChartKind kind : EnumUtils<ClaimChartKind>::Values()) {
            EXPECT_EQ(Summarise(result, KindFilter(kind)).rows, byKind[static_cast<size_t>(kind)]);
        }
        for (const ShantenBefore shanten : EnumUtils<ShantenBefore>::Values()) {
            ClaimFilter filter;
            filter.shantenBefore = shanten;
            EXPECT_EQ(Summarise(result, filter).rows, byShanten[static_cast<size_t>(shanten)]);
        }
        for (const WallStage stage : EnumUtils<WallStage>::Values()) {
            ClaimFilter filter;
            filter.stage = stage;
            EXPECT_EQ(Summarise(result, filter).rows, byStage[static_cast<size_t>(stage)]);
        }
        int64_t eastRows = 0;
        for (const bool east : {false, true}) {
            ClaimFilter filter;
            filter.east = east;
            eastRows += Summarise(result, filter).rows;
        }
        EXPECT_EQ(eastRows, result.counters.rows);
    }

    TEST(ArenaChartsRunTest, TheDiscardSummaryIsTheSameAtAnyNumberOfJobs) {
        const DiscardChartsResult one = RunDiscardOk(ConfigOf(kFirstSeed, kSeeds, 1));
        ASSERT_GT(one.counters.rows, 0);
        for (const int jobs : {kSomeJobs, kManyJobs}) {
            const DiscardChartsResult other = RunDiscardOk(ConfigOf(kFirstSeed, kSeeds, jobs));
            EXPECT_TRUE(SameDiscardCounters(one.counters, other.counters)) << "jobs " << jobs;
            EXPECT_TRUE(SameDiscardRows(one, other)) << "jobs " << jobs;
            EXPECT_TRUE(SameCell(SummariseDiscards(one, DiscardFilter{}), SummariseDiscards(other, DiscardFilter{}))) << "jobs " << jobs;
            for (const ValueHeld held : EnumUtils<ValueHeld>::Values()) {
                EXPECT_TRUE(SameCell(SummariseDiscards(one, ValueHeldFilter(held)), SummariseDiscards(other, ValueHeldFilter(held)))) << "jobs " << jobs;
            }
        }
    }

    TEST(ArenaChartsRunTest, TwoHalvesOfADiscardRangeHaveTheCountersOfTheWholeAdded) {
        const DiscardChartsResult& whole = WholeDiscards();
        const DiscardChartsResult head = RunDiscardOk(ConfigOf(kFirstSeed, kHalf, kSomeJobs));
        const DiscardChartsResult tail = RunDiscardOk(ConfigOf(kFirstSeed + kHalf, kSeeds - kHalf, kSomeJobs));
        DiscardCounters sum = head.counters;
        sum.Add(tail.counters);
        EXPECT_TRUE(SameDiscardCounters(whole.counters, sum));
        for (size_t i = 0; i < whole.seeds.size(); ++i) {
            const SeedDiscardRows& part = i < static_cast<size_t>(kHalf) ? head.seeds[i] : tail.seeds[i - static_cast<size_t>(kHalf)];
            EXPECT_EQ(whole.seeds[i].seed, kFirstSeed + static_cast<uint32_t>(i));
            EXPECT_EQ(part.seed, whole.seeds[i].seed);
            EXPECT_EQ(part.rows.size(), whole.seeds[i].rows.size());
        }
    }

    TEST(ArenaChartsRunTest, TheCountersOfADiscardResultAreTheSumOfTheSeedsAndAddUp) {
        const DiscardChartsResult& result = WholeDiscards();
        ASSERT_EQ(result.seeds.size(), static_cast<size_t>(kSeeds));
        EXPECT_EQ(result.counters.hands, kSeeds);
        int64_t rows = 0;
        for (const SeedDiscardRows& seed : result.seeds) {
            rows += static_cast<int64_t>(seed.rows.size());
        }
        EXPECT_EQ(rows, result.counters.rows);
        const DiscardCounters& c = result.counters;
        EXPECT_GT(c.ownTurns, 0);
        EXPECT_EQ(c.ownTurns, c.otherStep + c.noValue + c.sameAsNormal + c.dropped + c.rows);
    }

    TEST(ArenaChartsRunTest, ADiscardHandThatFailsIsAnErrorOfTheLowestSeedWhateverTheJobs) {
        const DiscardHandSource source = [](uint32_t seed) -> std::expected<DiscardTurns, String> {
            if (seed == kFailingSeed || seed == kLaterFailingSeed) {
                return std::unexpected(Format("seed {}: the model refused the action", seed));
            }
            return DiscardTurns{};
        };
        for (const int jobs : {1, kSomeJobs, kManyJobs}) {
            const std::expected<DiscardChartsResult, String> result = RunDiscardCharts(ConfigOf(kFirstSeed, kSeeds, jobs), source);
            ASSERT_FALSE(result.has_value()) << "jobs " << jobs;
            EXPECT_NE(result.error().find(Format("seed {}:", kFailingSeed)), String::npos) << result.error().c_str();
            EXPECT_EQ(result.error().find(Format("seed {}:", kLaterFailingSeed)), String::npos) << result.error().c_str();
        }
    }

    TEST(ArenaChartsRunTest, ATurnThatCannotBeMeasuredIsAnErrorNamingTheSeedAndTheTurn) {
        const DiscardHandSource source = [](uint32_t seed) -> std::expected<DiscardTurns, String> {
            DiscardTurns turns;
            if (seed == kFailingSeed) {
                turns.turns.push_back({0, TableView{}, DiscardPair{}});
            }
            return turns;
        };
        const std::expected<DiscardChartsResult, String> result = RunDiscardCharts(ConfigOf(kFirstSeed, kSeeds, kSomeJobs), source);
        ASSERT_FALSE(result.has_value());
        EXPECT_NE(result.error().find(Format("seed {} turn 0", kFailingSeed)), String::npos) << result.error().c_str();
    }

    TEST(ArenaChartsRunTest, ATurnThatCannotBeAnalysedIsCountedAsDroppedAndAMeasuredOneAsARow) {
        const TableView open = OwnTurnWithAPair();
        const DiscardSelection selection = SelectDiscardPair(open);
        ASSERT_EQ(selection.outcome, DiscardOutcome::Measured);
        TableView spent = open;
        spent.visible.fill(kCopiesPerTile);
        const DiscardHandSource source = [&](uint32_t) -> std::expected<DiscardTurns, String> {
            DiscardTurns turns;
            turns.ownTurns = 2;
            turns.turns.push_back({0, open, selection.pair});
            turns.turns.push_back({1, spent, selection.pair});
            return turns;
        };
        const std::expected<DiscardChartsResult, String> run = RunDiscardCharts(ConfigOf(kFirstSeed, kHalf, kSomeJobs), source);
        ASSERT_TRUE(run.has_value()) << (run.has_value() ? "" : run.error().c_str());
        EXPECT_EQ(run->counters.hands, kHalf);
        EXPECT_EQ(run->counters.ownTurns, 2 * kHalf);
        EXPECT_EQ(run->counters.dropped, kHalf);
        EXPECT_EQ(run->counters.rows, kHalf);
        for (const SeedDiscardRows& seed : run->seeds) {
            EXPECT_EQ(seed.rows.size(), 1U);
        }
    }

    TEST(ArenaChartsRunTest, TheIntervalOfADiscardCellIsMeanCiOfThePerSeedMeans) {
        const DiscardChartsResult& result = WholeDiscards();
        for (const ValueHeld held : EnumUtils<ValueHeld>::Values()) {
            Vector<double> means;
            int64_t rows = 0;
            for (const SeedDiscardRows& seed : result.seeds) {
                double sum = 0.0;
                int count = 0;
                for (const DiscardRow& row : seed.rows) {
                    if (row.cls.valueHeld == held) {
                        sum += row.gain;
                        ++count;
                    }
                }
                rows += count;
                if (count > 0) {
                    means.push_back(sum / count);
                }
            }
            const DiscardCell cell = SummariseDiscards(result, ValueHeldFilter(held));
            const MeanInterval expected = MeanCi(means);
            EXPECT_EQ(cell.rows, rows);
            EXPECT_EQ(cell.seeds, static_cast<int64_t>(means.size()));
            EXPECT_EQ(cell.gain.n, expected.n);
            EXPECT_EQ(cell.gain.mean, expected.mean);
            EXPECT_EQ(cell.gain.half, expected.half);
        }
    }

    TEST(ArenaChartsRunTest, ADiscardCellUnderTheFewRowsLimitIsFewAndNeverSignificant) {
        const DiscardChartsResult thin = MadeDiscardResult(8, 2);
        const DiscardCell thinCell = SummariseDiscards(thin, DiscardFilter{});
        EXPECT_EQ(thinCell.rows, 16);
        EXPECT_TRUE(thinCell.few);
        EXPECT_TRUE(IsSignificant(thinCell.gain));
        EXPECT_FALSE(thinCell.significant);

        const DiscardCell thickCell = SummariseDiscards(MadeDiscardResult(20, 2), DiscardFilter{});
        EXPECT_EQ(thickCell.rows, 40);
        EXPECT_FALSE(thickCell.few);
        EXPECT_TRUE(thickCell.significant);

        const DiscardCell empty = SummariseDiscards(DiscardChartsResult{}, DiscardFilter{});
        EXPECT_EQ(empty.rows, 0);
        EXPECT_EQ(empty.seeds, 0);
        EXPECT_FALSE(empty.gain.hasInterval);
        EXPECT_TRUE(empty.few);
        EXPECT_FALSE(empty.significant);
    }

    TEST(ArenaChartsRunTest, TheOneWayDiscardTablesAddUpTheCellsOfTheMainTable) {
        const DiscardChartsResult& result = WholeDiscards();
        int64_t total = 0;
        Array<int64_t, EnumUtils<ValueHeld>::Count()> byHeld{};
        Array<int64_t, EnumUtils<DiscardShanten>::Count()> byShanten{};
        Array<int64_t, EnumUtils<ValueCost>::Count()> byCost{};
        for (const ValueHeld held : EnumUtils<ValueHeld>::Values()) {
            for (const DiscardShanten shanten : EnumUtils<DiscardShanten>::Values()) {
                for (const ValueCost cost : EnumUtils<ValueCost>::Values()) {
                    DiscardFilter filter;
                    filter.valueHeld = held;
                    filter.shanten = shanten;
                    filter.cost = cost;
                    const int64_t rows = SummariseDiscards(result, filter).rows;
                    total += rows;
                    byHeld[static_cast<size_t>(held)] += rows;
                    byShanten[static_cast<size_t>(shanten)] += rows;
                    byCost[static_cast<size_t>(cost)] += rows;
                }
            }
        }
        EXPECT_EQ(total, result.counters.rows);
        EXPECT_EQ(SummariseDiscards(result, DiscardFilter{}).rows, result.counters.rows);
        for (const ValueHeld held : EnumUtils<ValueHeld>::Values()) {
            EXPECT_EQ(SummariseDiscards(result, ValueHeldFilter(held)).rows, byHeld[static_cast<size_t>(held)]);
        }
        for (const DiscardShanten shanten : EnumUtils<DiscardShanten>::Values()) {
            DiscardFilter filter;
            filter.shanten = shanten;
            EXPECT_EQ(SummariseDiscards(result, filter).rows, byShanten[static_cast<size_t>(shanten)]);
        }
        for (const ValueCost cost : EnumUtils<ValueCost>::Values()) {
            DiscardFilter filter;
            filter.cost = cost;
            EXPECT_EQ(SummariseDiscards(result, filter).rows, byCost[static_cast<size_t>(cost)]);
        }
        int64_t byStage = 0;
        for (const WallStage stage : EnumUtils<WallStage>::Values()) {
            DiscardFilter filter;
            filter.stage = stage;
            byStage += SummariseDiscards(result, filter).rows;
        }
        EXPECT_EQ(byStage, result.counters.rows);
        int64_t bySets = 0;
        for (const OpenSets sets : EnumUtils<OpenSets>::Values()) {
            DiscardFilter filter;
            filter.openSets = sets;
            bySets += SummariseDiscards(result, filter).rows;
        }
        EXPECT_EQ(bySets, result.counters.rows);
        int64_t byEast = 0;
        for (const bool east : {false, true}) {
            DiscardFilter filter;
            filter.east = east;
            byEast += SummariseDiscards(result, filter).rows;
        }
        EXPECT_EQ(byEast, result.counters.rows);
    }

}  // namespace BECore::Mahjong::Arena
