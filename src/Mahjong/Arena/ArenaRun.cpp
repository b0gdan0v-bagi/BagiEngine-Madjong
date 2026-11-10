#include "ArenaRun.h"

#include <Mahjong/Arena/ArenaWorlds.h>

namespace BECore::Mahjong::Arena {

    namespace {

        /// Adds the seat `seat` of `record` to `tally` as one seat-hand.
        void AddSeat(ArenaTally& tally, const HandRecord& record, Seat seat) {
            ++tally.hands;
            tally.points += record.deltas[seat];
            tally.declared += record.declared[seat] ? 1 : 0;
            tally.drawn += record.kind == ResultKind::Drawn ? 1 : 0;
            tally.decisions += record.decisions[seat];
            tally.nanoseconds += record.nanoseconds[seat];
            if (record.winner == seat) {
                ++tally.wins;
                tally.winScore += record.scores[seat];
                for (const ScoreKind kind : record.winKinds) {
                    ++tally.winsByKind[static_cast<size_t>(kind)];
                }
            }
        }

        /// What one seed gives one focus player: its row and the tallies of its four hands. Each seed writes only its own slots.
        struct SeedPlay {
            SeedRow row;
            ArenaTally focus;
            ArenaTally others;
            eastl::array<int64_t, kSeatCount> winsBySeat{};
        };

        void AddTally(ArenaTally& total, const ArenaTally& part) {
            total.hands += part.hands;
            total.wins += part.wins;
            total.points += part.points;
            total.drawn += part.drawn;
            total.declared += part.declared;
            total.winScore += part.winScore;
            for (size_t kind = 0; kind < kScoreKindCount; ++kind) {
                total.winsByKind[kind] += part.winsByKind[kind];
            }
            total.decisions += part.decisions;
            total.nanoseconds += part.nanoseconds;
        }

        /// Plays the seed `config.first + index` for every focus player into `plays[f]` (`f` the place in the focus list). An empty
        /// string is done; else the error of the first focus player whose hand failed.
        eastl::string PlaySeed(const ArenaConfig& config, const ArenaHandPlayer& playHand, int index, const WorldLoop& worlds, eastl::span<SeedPlay> plays) {
            ArenaPlayers players;
            players.fill(config.others);
            const uint32_t seed = config.first + static_cast<uint32_t>(index);
            for (size_t f = 0; f < config.focus.size(); ++f) {
                const ArenaPlayer& focus = config.focus[f];
                SeedPlay& play = plays[f];
                play.row.seed = seed;
                for (Seat focusSeat = 0; focusSeat < kSeatCount; ++focusSeat) {
                    players[focusSeat] = focus;
                    const std::expected<HandRecord, eastl::string> record = playHand(seed, players, config.timing, worlds);
                    players[focusSeat] = config.others;
                    if (!record.has_value()) {
                        return Format("focus {}: {}", focus.name.c_str(), record.error().c_str());
                    }
                    AddSeat(play.focus, *record, focusSeat);
                    for (Seat seat = 0; seat < kSeatCount; ++seat) {
                        if (seat != focusSeat) {
                            AddSeat(play.others, *record, seat);
                        }
                    }
                    const bool won = record->winner == focusSeat;
                    play.row.pointsOfFocus += record->deltas[focusSeat];
                    play.row.winsOfFocus += won ? 1 : 0;
                    play.row.drawn += record->kind == ResultKind::Drawn ? 1 : 0;
                    play.row.declared += record->declared[focusSeat] ? 1 : 0;
                    play.row.winScore += won ? record->scores[focusSeat] : 0;
                    play.winsBySeat[focusSeat] += won ? 1 : 0;
                }
            }
            return {};
        }

        /// The threads that play seeds: `jobs`, never more than there are seeds.
        int ThreadsFor(const ArenaConfig& config) {
            return std::min(config.jobs, config.count);
        }

        /// The threads each seed thread owns for the worlds of a search decision, itself included: what `jobs` has left over when there
        /// are fewer seeds than threads, so the seed threads together never use more than `jobs`.
        int WorldThreadsFor(const ArenaConfig& config) {
            return std::max(1, config.jobs / ThreadsFor(config));
        }

    }  // namespace

    eastl::string ValidateArenaConfig(const ArenaConfig& config) {
        if (config.focus.empty()) {
            return "no focus player to play";
        }
        if (config.count < 1) {
            return Format("the number of seeds is {}, it must be at least 1", config.count);
        }
        const uint64_t last = static_cast<uint64_t>(config.first) + static_cast<uint64_t>(config.count) - 1;
        if (last > std::numeric_limits<uint32_t>::max()) {
            return Format("the seeds {} .. {} run past the largest seed", config.first, last);
        }
        if (config.jobs < 1 || config.jobs > kMaxJobs) {
            return Format("the number of threads is {}, it must be from 1 to {}", config.jobs, kMaxJobs);
        }
        return {};
    }

    std::expected<ArenaResult, eastl::string> RunArena(const ArenaConfig& config) {
        return RunArena(config, [](uint32_t seed, const ArenaPlayers& players, bool timing, const WorldLoop& worlds) { return PlayArenaHand(seed, players, timing, &worlds); });
    }

    std::expected<ArenaResult, eastl::string> RunArena(const ArenaConfig& config, const ArenaHandPlayer& playHand) {
        const eastl::string problem = ValidateArenaConfig(config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        const size_t focusCount = config.focus.size();
        const size_t seedCount = static_cast<size_t>(config.count);
        // One slot per (seed, focus player) and one error per seed: a thread writes only the slots of the seeds it took.
        eastl::vector<SeedPlay> plays(seedCount * focusCount);
        eastl::vector<eastl::string> errors(seedCount);
        std::atomic<int> nextSeed{0};
        // The lowest seed that failed so far: seeds are taken in order, so every lower seed is already taken and will finish, and a
        // thread that takes a higher one has nothing to add.
        std::atomic<int> lowestFailed{config.count};
        const int worldThreads = WorldThreadsFor(config);
        const auto work = [&] {
            WorldThreads pool(worldThreads);
            const WorldLoop worlds = pool.AsLoop();
            for (;;) {
                const int index = nextSeed.fetch_add(1);
                if (index >= config.count || index > lowestFailed.load()) {
                    return;
                }
                errors[static_cast<size_t>(index)] = PlaySeed(config, playHand, index, worlds, eastl::span<SeedPlay>(plays.data() + static_cast<size_t>(index) * focusCount, focusCount));
                if (!errors[static_cast<size_t>(index)].empty()) {
                    int seen = lowestFailed.load();
                    while (index < seen && !lowestFailed.compare_exchange_weak(seen, index)) {
                    }
                }
            }
        };
        const int threadCount = ThreadsFor(config);
        eastl::vector<std::thread> threads;
        for (int thread = 1; thread < threadCount; ++thread) {
            threads.emplace_back(work);
        }
        work();
        for (std::thread& thread : threads) {
            thread.join();
        }
        for (const eastl::string& error : errors) {
            if (!error.empty()) {
                return std::unexpected(error);
            }
        }

        ArenaResult result;
        result.first = config.first;
        result.count = config.count;
        result.others = config.others;
        result.timing = config.timing;
        for (size_t f = 0; f < focusCount; ++f) {
            FocusResult focus;
            focus.player = config.focus[f];
            focus.rows.reserve(seedCount);
            for (size_t index = 0; index < seedCount; ++index) {
                const SeedPlay& play = plays[index * focusCount + f];
                focus.rows.push_back(play.row);
                AddTally(focus.focus, play.focus);
                AddTally(focus.others, play.others);
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    focus.winsBySeat[seat] += play.winsBySeat[seat];
                }
            }
            result.focus.push_back(eastl::move(focus));
        }
        return result;
    }

}  // namespace BECore::Mahjong::Arena
