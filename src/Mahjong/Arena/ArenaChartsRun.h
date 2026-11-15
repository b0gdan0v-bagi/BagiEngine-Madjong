#pragma once

#include <Mahjong/Arena/ArenaChartsDiscards.h>
#include <Mahjong/Arena/ArenaChartsWindows.h>
#include <Mahjong/Arena/ArenaRun.h>
#include <Mahjong/Arena/ArenaStats.h>

namespace BECore::Mahjong::Arena {

    /// The worlds per window of a run that does not say; the study's own number is in the protocol of `docs/charts-claims.md`.
    inline constexpr int kDefaultChartWorlds = 100;
    /// The seeds of a run that does not say; a window costs `worlds` rollouts, so the arena's default of 250 seeds would run for hours.
    inline constexpr int kDefaultChartSeeds = 20;

    /// The seeds `first .. first + count - 1`, one hand each with four Normal seats, every claim window (or measured own turn) measured with
    /// `worlds` worlds.
    struct ChartsConfig {
        uint32_t first = kDefaultFirstSeed;
        int count = kDefaultChartSeeds;
        int worlds = kDefaultChartWorlds;
        /// The threads in all, 1 to `kMaxJobs`: `min(jobs, count)` threads each take whole seeds and play the worlds of their windows
        /// themselves. The result does not depend on it.
        int jobs = kDefaultJobs;
    };

    /// Empty when `config` can be run, else the reason (a front end asks first to tell a bad argument from a refused action).
    [[nodiscard]] String ValidateChartsConfig(const ChartsConfig& config);

    /// What the windows of a run came to; integers, so the merge of the seeds does not depend on the order.
    struct ChartsCounters {
        int64_t hands = 0;
        /// Windows that list at least one claim, a mahjong claim included.
        int64_t claimWindows = 0;
        /// Of those, the windows left out because they list a mahjong claim.
        int64_t excludedForWin = 0;
        /// Windows that were to be measured but could not be analysed (no world); they have no rows.
        int64_t dropped = 0;
        /// The rows of the measured windows: one per claim.
        int64_t rows = 0;

        void Add(const ChartsCounters& other) noexcept;
    };

    /// The rows of one seed, in the order the hand played its windows; a seed without a window has none.
    struct SeedRows {
        uint32_t seed = 0;
        Vector<ClaimRow> rows;
    };

    struct ChartsResult {
        uint32_t first = 0;
        int count = 0;
        int worlds = 0;
        ChartsCounters counters;
        /// One per seed, in seed order.
        Vector<SeedRows> seeds;
    };

    /// How the hand of a seed is played and its windows collected; `CollectClaimWindows` with four Normal seats for the program. It is
    /// called from several threads, so it must not write anything shared. An error text names the seed, as those of `PlayArenaHand` do.
    using ChartsHandSource = std::function<std::expected<ClaimWindows, String>(uint32_t seed)>;  //-NoStd: stored per run and called from several threads, as ArenaHandPlayer

    /**
     * @brief Plays the seeds and measures every claim window.
     *
     * Deterministic: the same config gives the same result at any `jobs` (a seed is played by one thread into a slot of its own, the
     * slots are merged in seed order). A refused action or a window that cannot be measured is an error that names the seed; the error
     * is that of the lowest seed that failed, whatever the number of threads. A window with no world to sample is not an error: it is
     * counted in `ChartsCounters::dropped`.
     */
    [[nodiscard]] std::expected<ChartsResult, String> RunCharts(const ChartsConfig& config);

    /// The seam for a test that makes a whole hand fail on chosen seeds; the error rules are those of `RunCharts(config)`.
    [[nodiscard]] std::expected<ChartsResult, String> RunCharts(const ChartsConfig& config, const ChartsHandSource& source);

    /// The rows a cell holds: a field left empty takes every value.
    struct ClaimFilter {
        Optional<ClaimChartKind> kind;
        Optional<TileGroup> tile;
        Optional<ShantenBefore> shantenBefore;
        Optional<ShantenChange> shantenChange;
        Optional<WallStage> stage;
        Optional<bool> east;
        Optional<OpenSets> openSets;
    };

    [[nodiscard]] bool Matches(const ClaimFilter& filter, const ClaimClass& cls) noexcept;

    /// A group of rows and the mean of their gains over the seeds that have one (`MeanCi` of the per-seed means), for both kinds.
    struct CellStats {
        int64_t rows = 0;
        /// The seeds with at least one row: the `n` of the interval.
        int64_t seeds = 0;
        MeanInterval gain;
        /// Fewer than `kFewRows` rows: shown, marked, never `significant`.
        bool few = false;
        /// The interval excludes 0, and the group is not `few`.
        bool significant = false;
    };

    /// All the rows of a filter, and the same rows split by whether Normal takes the claim. A claim Normal takes that loses points, or
    /// one it passes that gains them, is a class where its rule is wrong.
    struct Cell : CellStats {
        CellStats takes;
        CellStats passes;
    };

    /// The cell of `filter` over every row of `result`. The unit of the interval is the seed: the mean of the gains of a seed's matching
    /// rows, then `MeanCi` over the seeds that have one.
    [[nodiscard]] Cell Summarise(const ChartsResult& result, const ClaimFilter& filter);

    /// What the own turns of a run came to; integers, so the merge of the seeds does not depend on the order.
    /// `ownTurns = otherStep + noValue + sameAsNormal + dropped + rows`.
    struct DiscardCounters {
        int64_t hands = 0;
        int64_t ownTurns = 0;
        /// Turns Normal answers with a win, a declaration, a kong or a kept wait (steps 1 to 4): not charted.
        int64_t otherStep = 0;
        /// Turns for which `ValueDiscards` rates no discard.
        int64_t noValue = 0;
        /// Turns whose valued tile is one of Normal's own ties.
        int64_t sameAsNormal = 0;
        /// Measured turns that could not be analysed (no world); they have no row.
        int64_t dropped = 0;
        /// The rows: one per measured turn.
        int64_t rows = 0;

        void Add(const DiscardCounters& other) noexcept;
    };

    /// The rows of one seed, in the order the hand played its turns; a seed without a measured turn has none.
    struct SeedDiscardRows {
        uint32_t seed = 0;
        Vector<DiscardRow> rows;
    };

    struct DiscardChartsResult {
        uint32_t first = 0;
        int count = 0;
        int worlds = 0;
        DiscardCounters counters;
        /// One per seed, in seed order.
        Vector<SeedDiscardRows> seeds;
    };

    /// How the hand of a seed is played and its own turns collected; `CollectDiscardTurns` with four Normal seats for the program. Called
    /// from several threads, so it must not write anything shared.
    using DiscardHandSource = std::function<std::expected<DiscardTurns, String>(uint32_t seed)>;  //-NoStd: stored per run and called from several threads, as ChartsHandSource

    /**
     * @brief Plays the seeds and measures every own turn the discard charts chart.
     *
     * The same engine as `RunCharts` (same bytes at any `jobs`, the error of the lowest failed seed). An error text names the seed, and
     * the turn when a turn failed. A turn with no world to sample is not an error: it is counted in `DiscardCounters::dropped`.
     */
    [[nodiscard]] std::expected<DiscardChartsResult, String> RunDiscardCharts(const ChartsConfig& config);

    /// The seam for a test that makes a whole hand fail on chosen seeds; the error rules are those of `RunDiscardCharts(config)`.
    [[nodiscard]] std::expected<DiscardChartsResult, String> RunDiscardCharts(const ChartsConfig& config, const DiscardHandSource& source);

    /// The rows a cell holds: a field left empty takes every value.
    struct DiscardFilter {
        Optional<ValueHeld> valueHeld;
        Optional<DiscardShanten> shanten;
        Optional<ValueCost> cost;
        Optional<WallStage> stage;
        Optional<OpenSets> openSets;
        Optional<bool> east;
    };

    [[nodiscard]] bool Matches(const DiscardFilter& filter, const DiscardClass& cls) noexcept;

    /// Normal always takes its own discard, so a discard cell has no `takes` / `passes` split.
    using DiscardCell = CellStats;

    /// The cell of `filter` over every row of `result`. The unit of the interval is the seed: the mean of the gains of a seed's matching
    /// rows, then `MeanCi` over the seeds that have one.
    [[nodiscard]] DiscardCell SummariseDiscards(const DiscardChartsResult& result, const DiscardFilter& filter);

}  // namespace BECore::Mahjong::Arena
