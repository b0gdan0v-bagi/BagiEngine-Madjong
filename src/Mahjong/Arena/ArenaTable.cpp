#include "ArenaTable.h"

#include <Mahjong/Arena/ArenaStats.h>
#include <thread>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr size_t kEastIndex = 0;
        constexpr size_t kOtherIndex = 1;
        constexpr int kPrecision = 2;
        constexpr int kPermillePrecision = 1;
        constexpr double kPermille = 1000.0;
        constexpr int kLabelWidth = 22;
        constexpr int kNumberWidth = 9;
        constexpr const char* kColumnGap = "  ";

        size_t EastIndexOf(bool isEast) noexcept {
            return isEast ? kEastIndex : kOtherIndex;
        }

        int DeclaredOpponents(const HandModel& hand, Seat seat) {
            int declared = 0;
            for (Seat other = 0; other < kSeatCount; ++other) {
                declared += other != seat && hand.GetHand(other).callingDeclared ? 1 : 0;
            }
            return declared;
        }

        void AddOwnTurnRounds(TableStats& stats, const eastl::vector<int>& declared, bool otherWon) {
            const int turns = static_cast<int>(declared.size());
            for (int turn = 1; turn <= turns; ++turn) {
                const size_t bucket = static_cast<size_t>(declared[static_cast<size_t>(turn - 1)]);
                // The round of the other seat's win, counted from this turn: the hand ends after the seat's last turn.
                const int winRound = turns - turn + 1;
                for (int rounds = 1; rounds <= kTableStatsRounds; ++rounds) {
                    const bool known = otherWon || turn + rounds <= turns;
                    if (!known) {
                        continue;
                    }
                    RoundCount& count = stats.rounds[bucket][static_cast<size_t>(rounds - 1)];
                    ++count.observed;
                    count.won += otherWon && winRound <= rounds ? 1 : 0;
                }
            }
        }

        void MergeSums(SampleSums& total, const SampleSums& part) {
            total.n += part.n;
            total.sum += part.sum;
            total.sumSquares += part.sumSquares;
        }

        double Mean(const SampleSums& sums) {
            return sums.n > 0 ? static_cast<double>(sums.sum) / static_cast<double>(sums.n) : 0.0;
        }

        /// The half-width of the 95 percent interval of the mean of `sums` (0 with fewer than two samples).
        double Half(const SampleSums& sums) {
            if (sums.n < 2) {
                return 0.0;
            }
            const double n = static_cast<double>(sums.n);
            const double mean = Mean(sums);
            const double variance = (static_cast<double>(sums.sumSquares) - n * mean * mean) / (n - 1.0);
            return kIntervalZ * std::sqrt(std::max(variance, 0.0) / n);
        }

        void AddRight(eastl::string& text, const eastl::string& cell, int width) {
            text.append(Format("{}{:>{}}", kColumnGap, cell, width));
        }

        void EndLine(eastl::string& text, size_t lineStart) {
            size_t end = text.size();
            while (end > lineStart && text[end - 1] == ' ') {
                --end;
            }
            text.resize(end);
            text.push_back('\n');
        }

        eastl::string Fixed(double value, int precision = kPrecision) {
            return Format("{:.{}f}", value, precision);
        }

        double PercentOf(int64_t part, int64_t whole) {
            return whole > 0 ? kPercent * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
        }

        void AddRoundsTable(eastl::string& text, const TableStats& stats) {
            text.append("\nanother seat wins in the following round, by the opponents that have declared (d)\n");
            text.append("  each row: own turns observed, per mille that another seat wins in the next round, survival % after 1..4 rounds; the row below is (1-h)^i\n");
            size_t start = text.size();
            text.append(Format("{:<{}}", "d", kLabelWidth));
            AddRight(text, "turns", kNumberWidth);
            AddRight(text, "h per mille", kNumberWidth + 2);
            for (int rounds = 1; rounds <= kTableStatsRounds; ++rounds) {
                AddRight(text, Format("after {}", rounds), kNumberWidth);
            }
            EndLine(text, start);
            for (size_t declared = 0; declared < kDeclaredCases; ++declared) {
                const auto& row = stats.rounds[declared];
                const double chance = row[0].observed > 0 ? static_cast<double>(row[0].won) / static_cast<double>(row[0].observed) : 0.0;
                start = text.size();
                text.append(Format("{:<{}}", declared, kLabelWidth));
                AddRight(text, Format("{}", row[0].observed), kNumberWidth);
                AddRight(text, Fixed(chance * kPermille, kPermillePrecision), kNumberWidth + 2);
                for (const RoundCount& count : row) {
                    AddRight(text, Fixed(PercentOf(count.observed - count.won, count.observed)), kNumberWidth);
                }
                EndLine(text, start);
                start = text.size();
                text.append(Format("{:<{}}", "  (1-h)^i", kLabelWidth));
                AddRight(text, "", kNumberWidth);
                AddRight(text, "", kNumberWidth + 2);
                double alive = 1.0;
                for (int rounds = 1; rounds <= kTableStatsRounds; ++rounds) {
                    alive *= 1.0 - chance;
                    AddRight(text, Fixed(kPercent * alive), kNumberWidth);
                }
                EndLine(text, start);
            }
        }

        void AddScoreRow(eastl::string& text, std::string_view label, const SampleSums& sums) {
            const size_t start = text.size();
            text.append(Format("{:<{}}", label, kLabelWidth));
            AddRight(text, Format("{}", sums.n), kNumberWidth);
            AddRight(text, Format("{} +- {}", Fixed(Mean(sums)), Fixed(Half(sums))), kNumberWidth + 8);
            EndLine(text, start);
        }

    }  // namespace

    void SampleSums::Add(int value) noexcept {
        ++n;
        sum += value;
        sumSquares += static_cast<int64_t>(value) * value;
    }

    HandObserver MakeOwnTurnObserver(OwnTurns& turns) {
        for (eastl::vector<int>& seat : turns) {
            seat.clear();
        }
        return [&turns](const HandModel& hand, Seat actor) {
            if (hand.GetPhase() == Phase::Discard) {
                turns[actor].push_back(DeclaredOpponents(hand, actor));
            }
        };
    }

    void AddHand(TableStats& stats, const HandRecord& record, const OwnTurns& turns) {
        ++stats.hands;
        const bool won = IsSeat(record.winner);
        if (!won) {
            ++stats.drawn;
        } else {
            ++stats.wonBySource[static_cast<size_t>(record.source)];
            stats.winnerScore[EastIndexOf(record.winner == record.east)].Add(record.scores[record.winner]);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != record.winner) {
                    stats.standingScore[EastIndexOf(seat == record.east)].Add(record.scores[seat]);
                }
            }
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            AddOwnTurnRounds(stats, turns[seat], won && seat != record.winner);
        }
    }

    std::expected<TableStats, eastl::string> RunTableStats(const TableStatsConfig& config) {
        ArenaConfig valid;
        valid.first = config.first;
        valid.count = config.count;
        valid.focus.push_back(config.player);
        valid.others = config.player;
        valid.jobs = config.jobs;
        const eastl::string problem = ValidateArenaConfig(valid);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        const size_t seedCount = static_cast<size_t>(config.count);
        // One slot per seed: a thread writes only the slots of the seeds it took; the slots are summed in seed order.
        eastl::vector<TableStats> slots(seedCount);
        eastl::vector<eastl::string> errors(seedCount);
        std::atomic<int> nextSeed{0};
        std::atomic<int> lowestFailed{config.count};
        ArenaPlayers players;
        players.fill(config.player);
        const auto work = [&] {
            for (;;) {
                const int index = nextSeed.fetch_add(1);
                if (index >= config.count || index > lowestFailed.load()) {
                    return;
                }
                OwnTurns turns;
                const HandObserver observer = MakeOwnTurnObserver(turns);
                const uint32_t seed = config.first + static_cast<uint32_t>(index);
                const std::expected<HandRecord, eastl::string> record = PlayArenaHand(seed, players, false, nullptr, &observer);
                if (record.has_value()) {
                    AddHand(slots[static_cast<size_t>(index)], *record, turns);
                    continue;
                }
                errors[static_cast<size_t>(index)] = record.error();
                int seen = lowestFailed.load();
                while (index < seen && !lowestFailed.compare_exchange_weak(seen, index)) {
                }
            }
        };
        const int threadCount = std::min(config.jobs, config.count);
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

        TableStats total;
        total.first = config.first;
        total.count = config.count;
        total.player = config.player.name;
        for (const TableStats& slot : slots) {
            total.hands += slot.hands;
            total.drawn += slot.drawn;
            for (size_t source = 0; source < kWinSourceCount; ++source) {
                total.wonBySource[source] += slot.wonBySource[source];
            }
            for (size_t declared = 0; declared < kDeclaredCases; ++declared) {
                for (size_t rounds = 0; rounds < static_cast<size_t>(kTableStatsRounds); ++rounds) {
                    total.rounds[declared][rounds].observed += slot.rounds[declared][rounds].observed;
                    total.rounds[declared][rounds].won += slot.rounds[declared][rounds].won;
                }
            }
            for (size_t east = 0; east < kEastCases; ++east) {
                MergeSums(total.winnerScore[east], slot.winnerScore[east]);
                MergeSums(total.standingScore[east], slot.standingScore[east]);
            }
        }
        return total;
    }

    eastl::string FormatTableStats(const TableStats& stats) {
        eastl::string text;
        const int64_t last = static_cast<int64_t>(stats.first) + stats.count - 1;
        text.append(Format("Table statistics: four {} seats, seeds {} .. {} ({} hands)\n", stats.player, stats.first, last, stats.hands));
        const int64_t won = stats.hands - stats.drawn;
        text.append(Format("hands won {}, drawn {} ({} %)\n", won, stats.drawn, Fixed(PercentOf(stats.drawn, stats.hands))));
        for (size_t source = 0; source < kWinSourceCount; ++source) {
            text.append(Format("  won on {}: {} ({} % of the won hands)\n", EnumUtils<WinSource>::ToString(static_cast<WinSource>(source)), stats.wonBySource[source],
                               Fixed(PercentOf(stats.wonBySource[source], won))));
        }
        AddRoundsTable(text, stats);
        text.append("\nmean score (rounded score of ScoreSeat), 95 percent interval over the hands\n");
        size_t start = text.size();
        text.append(Format("{:<{}}", "", kLabelWidth));
        AddRight(text, "samples", kNumberWidth);
        AddRight(text, "mean", kNumberWidth + 8);
        EndLine(text, start);
        AddScoreRow(text, "winner, East", stats.winnerScore[kEastIndex]);
        AddScoreRow(text, "winner, not East", stats.winnerScore[kOtherIndex]);
        AddScoreRow(text, "standing, East", stats.standingScore[kEastIndex]);
        AddScoreRow(text, "standing, not East", stats.standingScore[kOtherIndex]);
        text.append("standing = a seat that did not win, in a won hand\n");
        return text;
    }

}  // namespace BECore::Mahjong::Arena
