#include "ComputerPlayer.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>

namespace BECore::Mahjong {

    HandAction ComputerPlayer::Choose(const TableView& view) {
        ASSERT(!view.legal.empty());
        if (view.legal.empty()) {
            return HandAction::MakePass();
        }
        switch (view.phase) {
            case Phase::Claim:
            case Phase::RobKong:
                return ChooseClaimAction(view, _rng);
            case Phase::Discard:
                return ChooseTurnAction(view, _rng);
            case Phase::Finished:
                break;
        }
        return HandAction::MakePass();
    }

    std::optional<HandAction> ComputerPlayer::Decide(const HandModel& model, Seat seat) {
        const TableView view = MakeTableView(model, seat);
        if (view.legal.empty()) {
            return std::nullopt;
        }
        return Choose(view);
    }

    uint32_t SeatSeed(uint32_t tableSeed, Seat seat) {
        MahjongRng mixer(tableSeed * static_cast<uint32_t>(kSeatCount) + static_cast<uint32_t>(seat));
        return mixer.Below(std::numeric_limits<uint32_t>::max());
    }

    ComputerTable::ComputerTable(uint32_t seed)
        : _players{ComputerPlayer(SeatSeed(seed, 0)), ComputerPlayer(SeatSeed(seed, 1)), ComputerPlayer(SeatSeed(seed, 2)), ComputerPlayer(SeatSeed(seed, 3))} {}

    Seat ComputerTable::NextActor(const HandModel& model) {
        switch (model.GetPhase()) {
            case Phase::Discard:
                return model.CurrentSeat();
            case Phase::Claim:
            case Phase::RobKong: {
                const eastl::vector<Seat> pending = model.PendingClaimSeats();
                return pending.empty() ? kNoSeat : pending.front();
            }
            case Phase::Finished:
                break;
        }
        return kNoSeat;
    }

    StepResult ComputerTable::Step(HandModel& model) {
        const Seat actor = NextActor(model);
        if (actor == kNoSeat) {
            return std::unexpected(HandError::WrongPhase);
        }
        const std::optional<HandAction> action = _players[actor].Decide(model, actor);
        if (!action) {
            return std::unexpected(HandError::NotYourTurn);
        }
        if (const HandStatus applied = model.Apply(actor, *action); !applied) {
            return std::unexpected(applied.error());
        }
        return *action;
    }

    HandStatus ComputerTable::PlayHand(HandModel& model) {
        while (model.GetPhase() != Phase::Finished) {
            if (const StepResult step = Step(model); !step) {
                return std::unexpected(step.error());
            }
        }
        return {};
    }

    HandStatus ComputerTable::PlayMatch(Match& match) {
        while (match.GetPhase() == MatchPhase::InHand) {
            if (const HandStatus played = PlayHand(match.Hand()); !played) {
                return played;
            }
            if (!match.EndHand()) {
                // The hand is finished and the match is in a hand, so `EndHand` cannot refuse; if it does the match is broken.
                ASSERT(false, "EndHand refused a finished hand of a match that is in a hand");
                return std::unexpected(HandError::WrongPhase);
            }
        }
        return {};
    }

}  // namespace BECore::Mahjong
