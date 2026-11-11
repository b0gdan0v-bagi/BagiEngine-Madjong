#include "ArenaHand.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong::Arena {

    namespace {

        eastl::string Where(uint32_t seed, Seat seat) {
            return Format("seed {} seat {}", seed, static_cast<int>(seat));
        }

        /// The action a computer seat of `profile` chooses on `view` (what `ComputerPlayer::Choose` does with the profile of its level),
        /// at once; with `worlds` a search decision is handed back by the chooser and its worlds are played through the loop (the same
        /// draws and the same answer).
        HandAction ChooseComputer(const TableView& view, MahjongRng& rng, const LevelProfile& profile, const WorldLoop* worlds) {
            SearchHandoff handoff;
            SearchHandoff* const asked = worlds != nullptr ? &handoff : nullptr;
            HandAction action = HandAction::MakePass();
            switch (view.phase) {
                case Phase::Claim:
                case Phase::RobKong:
                    action = ChooseClaimAction(view, rng, profile, asked);
                    break;
                case Phase::Discard:
                    action = ChooseTurnAction(view, rng, profile, asked);
                    break;
                case Phase::Finished:
                    break;
            }
            if (handoff.run.has_value()) {
                handoff.run->RunWorlds(*worlds);
                return handoff.run->Result();
            }
            return action;
        }

        /// Fills the outcome of a finished hand into `record` (which already holds the decisions and the times).
        void FillResult(const HandModel& hand, const HandSettlement& settlement, HandRecord& record) {
            record.kind = settlement.kind;
            record.east = settlement.east;
            if (hand.Result().has_value()) {
                record.source = hand.Result()->source;
                record.from = hand.Result()->from;
            }
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
        }

    }  // namespace

    std::expected<HandRecord, eastl::string> PlayArenaHand(uint32_t seed, const ArenaPlayers& players, bool timing, const WorldLoop* worlds, const HandObserver* observer) {
        const MahjongSettings settings;
        MahjongRng wallRng(seed);
        const Seat east = static_cast<Seat>(seed % kSeatCount);
        HandModel hand;
        if (!hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value()) {
            return std::unexpected(Format("seed {}: the hand did not begin", seed));
        }

        eastl::array<MahjongRng, kSeatCount> seatRngs;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            seatRngs[seat].Seed(SeatSeed(seed, seat));
        }
        MahjongRng randomRng(seed * kRandomSeatSeedFactor + kRandomSeatSeedOffset);

        HandRecord record;
        int steps = 0;
        while (hand.GetPhase() != Phase::Finished) {
            if (++steps > kArenaStepLimit) {
                return std::unexpected(Format("seed {}: the hand does not end", seed));
            }
            const Seat actor = ComputerTable::NextActor(hand);
            if (!IsSeat(actor)) {
                return std::unexpected(Format("seed {}: nobody is to act", seed));
            }
            if (observer != nullptr) {
                (*observer)(hand, actor);
            }
            const ArenaPlayer& player = players[actor];
            const auto started = timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
            HandAction action;
            if (player.random) {
                const eastl::vector<HandAction> legal = hand.LegalActions(actor);
                if (legal.empty()) {
                    return std::unexpected(Format("{}: the random seat has no legal action", Where(seed, actor)));
                }
                action = ChooseSeekWin(legal, randomRng);
            } else {
                const TableView view = MakeTableView(hand, actor);
                if (view.legal.empty()) {
                    return std::unexpected(Format("{}: the computer seat has no legal action", Where(seed, actor)));
                }
                action = ChooseComputer(view, seatRngs[actor], player.profile, worlds);
            }
            if (timing) {
                record.nanoseconds[actor] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count();
            }
            ++record.decisions[actor];
            const HandStatus applied = hand.Apply(actor, action);
            if (!applied.has_value()) {
                return std::unexpected(Format("{}: the model refused the action ({})", Where(seed, actor), EnumUtils<HandError>::ToString(applied.error())));
            }
        }

        FillResult(hand, SettleHand(hand), record);
        return record;
    }

}  // namespace BECore::Mahjong::Arena
