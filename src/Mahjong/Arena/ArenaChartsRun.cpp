#include "ArenaChartsRun.h"

#include <thread>

namespace BECore::Mahjong::Arena {

    namespace {

        /// Everything one seed of a claim run gives; a thread writes only the slot of the seed it took.
        struct SeedSlot {
            SeedRows rows;
            ChartsCounters counters;
        };

        /// The same for a discard run.
        struct DiscardSlot {
            SeedDiscardRows rows;
            DiscardCounters counters;
        };

        /**
         * The engine of both kinds: `min(jobs, count)` threads take the seeds `first ..` in order and call `play(index, seed)`, which
         * fills the slot `index` of its own and returns an error text (empty: done). Returns the error of the lowest seed that failed,
         * whatever the number of threads, or an empty text.
         */
        template <typename Play>
        String PlaySeeds(const ChartsConfig& config, const Play& play) {
            Vector<String> errors(static_cast<size_t>(config.count));
            std::atomic<int> nextSeed{0};
            // The lowest seed that failed so far: seeds are taken in order, so every lower seed is already taken and will finish, and a
            // thread that takes a higher one has nothing to add.
            std::atomic<int> lowestFailed{config.count};
            const auto work = [&] {
                for (;;) {
                    const int index = nextSeed.fetch_add(1);
                    if (index >= config.count || index > lowestFailed.load()) {
                        return;
                    }
                    String& error = errors[static_cast<size_t>(index)];
                    error = play(index, config.first + static_cast<uint32_t>(index));
                    if (!error.empty()) {
                        int seen = lowestFailed.load();
                        while (index < seen && !lowestFailed.compare_exchange_weak(seen, index)) {
                        }
                    }
                }
            };
            const int threadCount = std::min(config.jobs, config.count);
            Vector<std::thread> threads;  //-NoStd: the arena's seed threads are plain std::thread, as in RunArena
            for (int thread = 1; thread < threadCount; ++thread) {
                threads.emplace_back(work);
            }
            work();
            for (std::thread& thread : threads) {  //-NoStd: as above
                thread.join();
            }
            for (const String& error : errors) {
                if (!error.empty()) {
                    return error;
                }
            }
            return {};
        }

        template <typename T>
        bool Admits(const Optional<T>& wanted, T value) noexcept {
            return !wanted.has_value() || *wanted == value;
        }

        /// Plays the windows of one seed into `slot`; the error text, empty when done.
        String PlayClaimSeed(const ChartsConfig& config, const ChartsHandSource& source, uint32_t seed, SeedSlot& slot) {
            slot.rows.seed = seed;
            const std::expected<ClaimWindows, String> collected = source(seed);
            if (!collected.has_value()) {
                return collected.error();
            }
            slot.counters.hands = collected->hands;
            slot.counters.claimWindows = collected->claimWindows;
            slot.counters.excludedForWin = collected->excludedForWin;
            const WorldLoop loop = SerialWorldLoop;
            for (const ClaimWindow& window : collected->windows) {
                std::expected<WindowRows, String> measured = MeasureWindow(window.view, seed, window.index, config.worlds, loop);
                if (!measured.has_value()) {
                    return Format("seed {} window {}: {}", seed, window.index, measured.error().c_str());
                }
                if (measured->dropped) {
                    ++slot.counters.dropped;
                    continue;
                }
                slot.counters.rows += static_cast<int64_t>(measured->rows.size());
                for (ClaimRow& row : measured->rows) {
                    slot.rows.rows.push_back(eastl::move(row));
                }
            }
            return {};
        }

        /// Plays the measured turns of one seed into `slot`; the error text (it names the seed and the turn), empty when done.
        String PlayDiscardSeed(const ChartsConfig& config, const DiscardHandSource& source, uint32_t seed, DiscardSlot& slot) {
            slot.rows.seed = seed;
            const std::expected<DiscardTurns, String> collected = source(seed);
            if (!collected.has_value()) {
                return collected.error();
            }
            slot.counters.hands = collected->hands;
            slot.counters.ownTurns = collected->ownTurns;
            slot.counters.otherStep = collected->otherStep;
            slot.counters.noValue = collected->noValue;
            slot.counters.sameAsNormal = collected->sameAsNormal;
            const WorldLoop loop = SerialWorldLoop;
            for (const DiscardTurn& turn : collected->turns) {
                std::expected<DiscardMeasure, String> measured = MeasureDiscardTurn(turn.view, turn.pair, seed, turn.index, config.worlds, loop);
                if (!measured.has_value()) {
                    return measured.error();
                }
                if (measured->dropped) {
                    ++slot.counters.dropped;
                    continue;
                }
                ++slot.counters.rows;
                slot.rows.rows.push_back(eastl::move(measured->row));
            }
            return {};
        }

        /// The rows of one group (all, taken or passed) over the seeds, in seed order.
        class GroupMeans {
        public:
            void AddRow(double gain) noexcept {
                _sum += gain;
                ++_rowsOfSeed;
            }

            /// Closes the seed: its mean joins the series when the seed had a row.
            void EndSeed() {
                if (_rowsOfSeed > 0) {
                    _means.push_back(_sum / static_cast<double>(_rowsOfSeed));
                    _rows += _rowsOfSeed;
                }
                _sum = 0.0;
                _rowsOfSeed = 0;
            }

            [[nodiscard]] CellStats Finish() const {
                CellStats stats;
                stats.rows = _rows;
                stats.seeds = static_cast<int64_t>(_means.size());
                stats.gain = MeanCi(_means);
                stats.few = _rows < kFewRows;
                stats.significant = !stats.few && IsSignificant(stats.gain);
                return stats;
            }

        private:
            Vector<double> _means;
            double _sum = 0.0;
            int64_t _rowsOfSeed = 0;
            int64_t _rows = 0;
        };

    }  // namespace

    void ChartsCounters::Add(const ChartsCounters& other) noexcept {
        hands += other.hands;
        claimWindows += other.claimWindows;
        excludedForWin += other.excludedForWin;
        dropped += other.dropped;
        rows += other.rows;
    }

    String ValidateChartsConfig(const ChartsConfig& config) {
        const std::expected<ArenaPlayer, String> normal = ParseArenaPlayer("normal");
        if (!normal.has_value()) {
            return normal.error();
        }
        ArenaConfig seeds;
        seeds.first = config.first;
        seeds.count = config.count;
        seeds.jobs = config.jobs;
        seeds.focus.push_back(*normal);
        seeds.others = *normal;
        const String problem = ValidateArenaConfig(seeds);
        if (!problem.empty()) {
            return problem;
        }
        if (config.worlds < 1) {
            return Format("the number of worlds is {}, it must be at least 1", config.worlds);
        }
        return {};
    }

    std::expected<ChartsResult, String> RunCharts(const ChartsConfig& config) {
        const std::expected<ArenaPlayer, String> normal = ParseArenaPlayer("normal");
        if (!normal.has_value()) {
            return std::unexpected(normal.error());
        }
        ArenaPlayers players;
        players.fill(*normal);
        return RunCharts(config, [&players](uint32_t seed) { return CollectClaimWindows(seed, players); });
    }

    std::expected<ChartsResult, String> RunCharts(const ChartsConfig& config, const ChartsHandSource& source) {
        const String problem = ValidateChartsConfig(config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        const size_t seedCount = static_cast<size_t>(config.count);
        Vector<SeedSlot> slots(seedCount);
        const String error = PlaySeeds(config, [&](int index, uint32_t seed) { return PlayClaimSeed(config, source, seed, slots[static_cast<size_t>(index)]); });
        if (!error.empty()) {
            return std::unexpected(error);
        }

        ChartsResult result;
        result.first = config.first;
        result.count = config.count;
        result.worlds = config.worlds;
        result.seeds.reserve(seedCount);
        for (SeedSlot& slot : slots) {
            result.counters.Add(slot.counters);
            result.seeds.push_back(eastl::move(slot.rows));
        }
        return result;
    }

    void DiscardCounters::Add(const DiscardCounters& other) noexcept {
        hands += other.hands;
        ownTurns += other.ownTurns;
        otherStep += other.otherStep;
        noValue += other.noValue;
        sameAsNormal += other.sameAsNormal;
        dropped += other.dropped;
        rows += other.rows;
    }

    std::expected<DiscardChartsResult, String> RunDiscardCharts(const ChartsConfig& config) {
        const std::expected<ArenaPlayer, String> normal = ParseArenaPlayer("normal");
        if (!normal.has_value()) {
            return std::unexpected(normal.error());
        }
        ArenaPlayers players;
        players.fill(*normal);
        return RunDiscardCharts(config, [&players](uint32_t seed) { return CollectDiscardTurns(seed, players); });
    }

    std::expected<DiscardChartsResult, String> RunDiscardCharts(const ChartsConfig& config, const DiscardHandSource& source) {
        const String problem = ValidateChartsConfig(config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        const size_t seedCount = static_cast<size_t>(config.count);
        Vector<DiscardSlot> slots(seedCount);
        const String error = PlaySeeds(config, [&](int index, uint32_t seed) { return PlayDiscardSeed(config, source, seed, slots[static_cast<size_t>(index)]); });
        if (!error.empty()) {
            return std::unexpected(error);
        }

        DiscardChartsResult result;
        result.first = config.first;
        result.count = config.count;
        result.worlds = config.worlds;
        result.seeds.reserve(seedCount);
        for (DiscardSlot& slot : slots) {
            result.counters.Add(slot.counters);
            result.seeds.push_back(eastl::move(slot.rows));
        }
        return result;
    }

    bool Matches(const ClaimFilter& filter, const ClaimClass& cls) noexcept {
        return Admits(filter.kind, cls.kind) && Admits(filter.tile, cls.tile) && Admits(filter.shantenBefore, cls.shantenBefore) && Admits(filter.shantenChange, cls.shantenChange) &&
               Admits(filter.stage, cls.stage) && Admits(filter.east, cls.east) && Admits(filter.openSets, cls.openSets);
    }

    bool Matches(const DiscardFilter& filter, const DiscardClass& cls) noexcept {
        return Admits(filter.valueHeld, cls.valueHeld) && Admits(filter.shanten, cls.shanten) && Admits(filter.cost, cls.cost) && Admits(filter.stage, cls.stage) &&
               Admits(filter.openSets, cls.openSets) && Admits(filter.east, cls.east);
    }

    DiscardCell SummariseDiscards(const DiscardChartsResult& result, const DiscardFilter& filter) {
        GroupMeans all;
        for (const SeedDiscardRows& seed : result.seeds) {
            for (const DiscardRow& row : seed.rows) {
                if (Matches(filter, row.cls)) {
                    all.AddRow(row.gain);
                }
            }
            all.EndSeed();
        }
        return all.Finish();
    }

    Cell Summarise(const ChartsResult& result, const ClaimFilter& filter) {
        GroupMeans all;
        GroupMeans takes;
        GroupMeans passes;
        for (const SeedRows& seed : result.seeds) {
            for (const ClaimRow& row : seed.rows) {
                if (!Matches(filter, row.cls)) {
                    continue;
                }
                all.AddRow(row.gain);
                (row.normalTakes ? takes : passes).AddRow(row.gain);
            }
            all.EndSeed();
            takes.EndSeed();
            passes.EndSeed();
        }
        Cell cell;
        static_cast<CellStats&>(cell) = all.Finish();
        cell.takes = takes.Finish();
        cell.passes = passes.Finish();
        return cell;
    }

}  // namespace BECore::Mahjong::Arena
