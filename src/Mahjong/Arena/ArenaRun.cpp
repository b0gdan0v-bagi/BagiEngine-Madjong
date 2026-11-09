#include "ArenaRun.h"

#include <Mahjong/Game/ScoreHand.h>

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

        eastl::string ValidateConfig(const ArenaConfig& config) {
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
            return {};
        }

        std::expected<FocusResult, eastl::string> PlayFocus(const ArenaConfig& config, const ArenaPlayer& focus) {
            FocusResult result;
            result.player = focus;
            result.rows.reserve(static_cast<size_t>(config.count));
            for (int index = 0; index < config.count; ++index) {
                SeedRow row;
                row.seed = config.first + static_cast<uint32_t>(index);
                for (Seat focusSeat = 0; focusSeat < kSeatCount; ++focusSeat) {
                    ArenaPlayers players;
                    players.fill(config.others);
                    players[focusSeat] = focus;
                    const std::expected<HandRecord, eastl::string> record = PlayArenaHand(row.seed, players, config.timing);
                    if (!record.has_value()) {
                        return std::unexpected(Format("focus {}: {}", focus.name.c_str(), record.error().c_str()));
                    }
                    AddSeat(result.focus, *record, focusSeat);
                    for (Seat seat = 0; seat < kSeatCount; ++seat) {
                        if (seat != focusSeat) {
                            AddSeat(result.others, *record, seat);
                        }
                    }
                    const bool won = record->winner == focusSeat;
                    row.pointsOfFocus += record->deltas[focusSeat];
                    row.winsOfFocus += won ? 1 : 0;
                    row.drawn += record->kind == ResultKind::Drawn ? 1 : 0;
                    row.declared += record->declared[focusSeat] ? 1 : 0;
                    row.winScore += won ? record->scores[focusSeat] : 0;
                    result.winsBySeat[focusSeat] += won ? 1 : 0;
                }
                result.rows.push_back(row);
            }
            return result;
        }

    }  // namespace

    std::expected<ArenaResult, eastl::string> RunArena(const ArenaConfig& config) {
        const eastl::string problem = ValidateConfig(config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        ArenaResult result;
        result.first = config.first;
        result.count = config.count;
        result.others = config.others;
        result.timing = config.timing;
        for (const ArenaPlayer& focus : config.focus) {
            std::expected<FocusResult, eastl::string> played = PlayFocus(config, focus);
            if (!played.has_value()) {
                return std::unexpected(played.error());
            }
            result.focus.push_back(eastl::move(*played));
        }
        return result;
    }

}  // namespace BECore::Mahjong::Arena
