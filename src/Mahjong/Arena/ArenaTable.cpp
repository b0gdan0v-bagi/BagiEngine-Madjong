#include "ArenaTable.h"

#include <Mahjong/Arena/ArenaStats.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandShape.h>
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

        void AddOwnTurnRounds(TableStats& stats, const Vector<int>& declared, bool otherWon) {
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

        /// 1.0 in `HitCount::expectedMicro`.
        constexpr int64_t kMicroPerHit = 1'000'000;

        /// What one seat could win on: its winning kinds and the unseen copies of them against all the unseen copies, from its view.
        struct Exposure {
            Seat seat = kNoSeat;
            Vector<Tile> winning;
            int64_t unseenWinning = 0;
            int64_t unseenTotal = 0;
        };

        /// `view` with the tile `drawn` (invalid for none) counted as unseen again: the seat's view before it drew.
        Optional<Exposure> ExposureOf(const TableView& view, Vector<Tile> winning, Tile drawn) {
            Exposure exposure;
            exposure.seat = view.seat;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                const Tile kind{id};
                const int unseen = UnseenCopies(view, kind) + (kind == drawn ? 1 : 0);
                exposure.unseenTotal += unseen;
                if (eastl::find(winning.begin(), winning.end(), kind) != winning.end()) {
                    exposure.unseenWinning += unseen;
                }
            }
            if (exposure.unseenTotal <= 0) {
                return std::nullopt;
            }
            exposure.winning = eastl::move(winning);
            return exposure;
        }

        void Count(HitCount& count, const Exposure& exposure, Tile tile) {
            ++count.exposures;
            count.expectedMicro += (exposure.unseenWinning * kMicroPerHit + exposure.unseenTotal / 2) / exposure.unseenTotal;
            const bool hit = eastl::find(exposure.winning.begin(), exposure.winning.end(), tile) != exposure.winning.end();
            count.hits += hit ? 1 : 0;
        }

        void MergeHits(HitCount& total, const HitCount& part) {
            total.exposures += part.exposures;
            total.expectedMicro += part.expectedMicro;
            total.hits += part.hits;
        }

        void MergeSums(SampleSums& total, const SampleSums& part) {
            total.n += part.n;
            total.sum += part.sum;
            total.sumSquares += part.sumSquares;
        }

        double Mean(const SampleSums& sums) {
            return sums.n > 0 ? static_cast<double>(sums.sum) / static_cast<double>(sums.n) : 0.0;
        }

        /// 0 with fewer than two samples.
        double Half(const SampleSums& sums) {
            if (sums.n < 2) {
                return 0.0;
            }
            const double n = static_cast<double>(sums.n);
            const double mean = Mean(sums);
            const double variance = (static_cast<double>(sums.sumSquares) - n * mean * mean) / (n - 1.0);
            return kIntervalZ * std::sqrt(std::max(variance, 0.0) / n);
        }

        void AddRight(String& text, const String& cell, int width) {
            text.append(Format("{}{:>{}}", kColumnGap, cell, width));
        }

        void EndLine(String& text, size_t lineStart) {
            size_t end = text.size();
            while (end > lineStart && text[end - 1] == ' ') {
                --end;
            }
            text.resize(end);
            text.push_back('\n');
        }

        String Fixed(double value, int precision = kPrecision) {
            return Format("{:.{}f}", value, precision);
        }

        double PercentOf(int64_t part, int64_t whole) {
            return whole > 0 ? kPercent * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
        }

        void AddRoundsTable(String& text, const TableStats& stats) {
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

        void AddHitRow(String& text, StringView label, const HitCount& count) {
            const size_t start = text.size();
            const double expected = static_cast<double>(count.expectedMicro) / static_cast<double>(kMicroPerHit);
            const double rate = count.expectedMicro > 0 ? kPermille * static_cast<double>(count.hits) / expected : 0.0;
            text.append(Format("{:<{}}", label, kLabelWidth));
            AddRight(text, Format("{}", count.exposures), kNumberWidth);
            AddRight(text, Fixed(expected), kNumberWidth + 2);
            AddRight(text, Format("{}", count.hits), kNumberWidth);
            AddRight(text, Fixed(rate, kPermillePrecision), kNumberWidth + 2);
            EndLine(text, start);
        }

        void AddScoreRow(String& text, StringView label, const SampleSums& sums) {
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
        for (Vector<int>& seat : turns) {
            seat.clear();
        }
        return [&turns](const HandModel& hand, Seat actor) {
            if (hand.GetPhase() == Phase::Discard) {
                turns[actor].push_back(DeclaredOpponents(hand, actor));
            }
        };
    }

    HandObserver MakeHitObserver(HandHits& hits) {
        hits = HandHits{};
        struct State {
            Vector<Exposure> pending;
            size_t scanned = 0;
        };
        return [&hits, state = State{}](const HandModel& hand, Seat actor) mutable {
            // The discard of the last action, if there was one, is the new `Discard` move.
            const Vector<HandMove>& moves = hand.Moves();
            for (; state.scanned < moves.size(); ++state.scanned) {
                const HandMove& move = moves[state.scanned];
                if (move.kind != MoveKind::Discard) {
                    continue;
                }
                for (const Exposure& exposure : state.pending) {
                    if (exposure.seat != move.seat) {
                        Count(hits.discards, exposure, move.tile);
                    }
                }
            }
            state.pending.clear();
            if (!IsSeat(actor) || hand.GetPhase() != Phase::Discard) {
                return;
            }
            const Tile drawn = hand.DrawnTile(actor);
            if (drawn.IsPlayable() && hand.GetHand(actor).callingDeclared) {
                PlayerHand before = hand.GetHand(actor);
                before.RemoveConcealed(drawn);
                if (const Optional<Exposure> own = ExposureOf(MakeTableView(hand, actor), WinningTiles(before), drawn)) {
                    Count(hits.draws, *own, drawn);
                }
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != actor && hand.GetHand(seat).callingDeclared) {
                    if (Optional<Exposure> exposure = ExposureOf(MakeTableView(hand, seat), WinningTiles(hand.GetHand(seat)), Tile{})) {
                        state.pending.push_back(eastl::move(*exposure));
                    }
                }
            }
        };
    }

    void AddHand(TableStats& stats, const HandRecord& record, const OwnTurns& turns, const HandHits& hits) {
        ++stats.hands;
        MergeHits(stats.discardHits, hits.discards);
        MergeHits(stats.drawHits, hits.draws);
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

    std::expected<TableStats, String> RunTableStats(const TableStatsConfig& config) {
        ArenaConfig valid;
        valid.first = config.first;
        valid.count = config.count;
        valid.focus.push_back(config.player);
        valid.others = config.player;
        valid.jobs = config.jobs;
        const String problem = ValidateArenaConfig(valid);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        const size_t seedCount = static_cast<size_t>(config.count);
        // One slot per seed: a thread writes only the slots of the seeds it took; the slots are summed in seed order.
        Vector<TableStats> slots(seedCount);
        Vector<String> errors(seedCount);
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
                HandHits hits;
                const HandObserver turnObserver = MakeOwnTurnObserver(turns);
                const HandObserver hitObserver = MakeHitObserver(hits);
                const HandObserver observer = [&](const HandModel& hand, Seat actor) {
                    turnObserver(hand, actor);
                    hitObserver(hand, actor);
                };
                const uint32_t seed = config.first + static_cast<uint32_t>(index);
                const std::expected<HandRecord, String> record = PlayArenaHand(seed, players, false, nullptr, &observer);
                if (record.has_value()) {
                    AddHand(slots[static_cast<size_t>(index)], *record, turns, hits);
                    continue;
                }
                errors[static_cast<size_t>(index)] = record.error();
                int seen = lowestFailed.load();
                while (index < seen && !lowestFailed.compare_exchange_weak(seen, index)) {
                }
            }
        };
        const int threadCount = std::min(config.jobs, config.count);
        Vector<std::thread> threads;
        for (int thread = 1; thread < threadCount; ++thread) {
            threads.emplace_back(work);
        }
        work();
        for (std::thread& thread : threads) {
            thread.join();
        }
        for (const String& error : errors) {
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
            MergeHits(total.discardHits, slot.discardHits);
            MergeHits(total.drawHits, slot.drawHits);
            for (size_t east = 0; east < kEastCases; ++east) {
                MergeSums(total.winnerScore[east], slot.winnerScore[east]);
                MergeSums(total.standingScore[east], slot.standingScore[east]);
            }
        }
        return total;
    }

    String FormatTableStats(const TableStats& stats) {
        String text;
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
        text.append("\na tile that is a winning kind of a declared, waiting seat, against its unseen winning copies / unseen total (that seat's view)\n");
        size_t hitStart = text.size();
        text.append(Format("{:<{}}", "", kLabelWidth));
        AddRight(text, "exposures", kNumberWidth);
        AddRight(text, "expected hits", kNumberWidth + 2);
        AddRight(text, "hits", kNumberWidth);
        AddRight(text, "hit per mille", kNumberWidth + 2);
        EndLine(text, hitStart);
        AddHitRow(text, "discard of another", stats.discardHits);
        AddHitRow(text, "own draw (check)", stats.drawHits);
        text.append("hit per mille = hits / expected hits; about 1000 means the tiles come as the unseen copies say\n");
        return text;
    }

}  // namespace BECore::Mahjong::Arena
