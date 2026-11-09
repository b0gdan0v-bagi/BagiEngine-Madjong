#include "ArenaHand.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong::Arena {

    namespace {

        /// A number as text (`eastl::to_string` needs a library the arena does not link).
        eastl::string NumberText(long long value) {
            const std::string text = std::to_string(value);
            return eastl::string(text.data(), text.size());
        }

        eastl::string Where(uint32_t seed, Seat seat) {
            return "seed " + NumberText(seed) + " seat " + NumberText(static_cast<int>(seat));
        }

        /// The action a computer seat of `profile` chooses on `view` (what `ComputerPlayer::Choose` does with the profile of its level).
        HandAction ChooseComputer(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
            switch (view.phase) {
                case Phase::Claim:
                case Phase::RobKong:
                    return ChooseClaimAction(view, rng, profile);
                case Phase::Discard:
                    return ChooseTurnAction(view, rng, profile);
                case Phase::Finished:
                    break;
            }
            return HandAction::MakePass();
        }

        HandRecord RecordOf(const HandModel& hand, const HandSettlement& settlement) {
            HandRecord record;
            record.kind = settlement.kind;
            record.winner = settlement.winner;
            record.scores = settlement.scores;
            record.deltas = settlement.deltas;
            if (IsSeat(settlement.winner)) {
                for (const ScoreLine& line : settlement.breakdowns[settlement.winner].lines) {
                    if (eastl::find(record.winKinds.begin(), record.winKinds.end(), line.kind) == record.winKinds.end()) {
                        record.winKinds.push_back(line.kind);
                    }
                }
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                record.declared[seat] = hand.GetHand(seat).callingDeclared;
            }
            return record;
        }

    }  // namespace

    std::expected<HandRecord, eastl::string> PlayArenaHand(uint32_t seed, const ArenaPlayers& players, bool timing) {
        const MahjongSettings settings;
        MahjongRng wallRng(seed);
        const Seat east = static_cast<Seat>(seed % kSeatCount);
        HandModel hand;
        if (!hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value()) {
            return std::unexpected("seed " + NumberText(seed) + ": the hand did not begin");
        }

        eastl::array<MahjongRng, kSeatCount> seatRngs;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            seatRngs[seat].Seed(SeatSeed(seed, seat));
        }
        MahjongRng randomRng(seed * 13 + 5);

        HandRecord record;
        int steps = 0;
        while (hand.GetPhase() != Phase::Finished) {
            if (++steps > kArenaStepLimit) {
                return std::unexpected("seed " + NumberText(seed) + ": the hand does not end");
            }
            const Seat actor = ComputerTable::NextActor(hand);
            if (!IsSeat(actor)) {
                return std::unexpected("seed " + NumberText(seed) + ": nobody is to act");
            }
            const ArenaPlayer& player = players[actor];
            const auto started = timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
            HandAction action;
            if (player.random) {
                const eastl::vector<HandAction> legal = hand.LegalActions(actor);
                if (legal.empty()) {
                    return std::unexpected(Where(seed, actor) + ": the random seat has no legal action");
                }
                action = ChooseSeekWin(legal, randomRng);
            } else {
                const TableView view = MakeTableView(hand, actor);
                if (view.legal.empty()) {
                    return std::unexpected(Where(seed, actor) + ": the computer seat has no legal action");
                }
                action = ChooseComputer(view, seatRngs[actor], player.profile);
            }
            if (timing) {
                record.nanoseconds[actor] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count();
            }
            ++record.decisions[actor];
            const HandStatus applied = hand.Apply(actor, action);
            if (!applied.has_value()) {
                return std::unexpected(Where(seed, actor) + ": the model refused the action (" + eastl::string(EnumUtils<HandError>::ToString(applied.error()).data()) + ")");
            }
        }

        HandRecord result = RecordOf(hand, SettleHand(hand));
        result.decisions = record.decisions;
        result.nanoseconds = record.nanoseconds;
        return result;
    }

}  // namespace BECore::Mahjong::Arena
