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
                return ChooseClaimAction(view, _rng, _profile);
            case Phase::Discard:
                return ChooseTurnAction(view, _rng, _profile);
            case Phase::Finished:
                break;
        }
        return HandAction::MakePass();
    }

    PlayerDecision ComputerPlayer::Begin(const TableView& view) {
        ASSERT(!view.legal.empty());
        if (view.legal.empty()) {
            return {HandAction::MakePass(), std::nullopt};
        }
        SearchHandoff handoff;
        HandAction action = HandAction::MakePass();
        switch (view.phase) {
            case Phase::Claim:
            case Phase::RobKong:
                action = ChooseClaimAction(view, _rng, _profile, &handoff);
                break;
            case Phase::Discard:
                action = ChooseTurnAction(view, _rng, _profile, &handoff);
                break;
            case Phase::Finished:
                break;
        }
        if (handoff.run) {
            return {std::nullopt, std::move(handoff.run)};
        }
        return {action, std::nullopt};
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

    ComputerTable::ComputerTable(uint32_t seed, ComputerLevel level) : ComputerTable(seed, UniformLevels(level)) {}

    ComputerTable::ComputerTable(uint32_t seed, const SeatLevels& levels)
        : _players{ComputerPlayer(SeatSeed(seed, 0), levels[0]), ComputerPlayer(SeatSeed(seed, 1), levels[1]), ComputerPlayer(SeatSeed(seed, 2), levels[2]),
                   ComputerPlayer(SeatSeed(seed, 3), levels[3])} {}

    ComputerTable::ComputerTable(uint32_t seed, const LevelProfile& profile)
        : _players{ComputerPlayer(SeatSeed(seed, 0), profile), ComputerPlayer(SeatSeed(seed, 1), profile), ComputerPlayer(SeatSeed(seed, 2), profile), ComputerPlayer(SeatSeed(seed, 3), profile)} {}

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

    std::expected<ComputerTable::BegunStep, HandError> ComputerTable::BeginStep(const HandModel& model) {
        const Seat actor = NextActor(model);
        if (actor == kNoSeat) {
            return std::unexpected(HandError::WrongPhase);
        }
        const TableView view = MakeTableView(model, actor);
        if (view.legal.empty()) {
            return std::unexpected(HandError::NotYourTurn);
        }
        return BegunStep{actor, _players[actor].Begin(view)};
    }

    StepResult ComputerTable::FinishStep(HandModel& model, Seat actor, const HandAction& action) {
        if (const HandStatus applied = model.Apply(actor, action); !applied) {
            return std::unexpected(applied.error());
        }
        return action;
    }

    StepResult ComputerTable::Step(HandModel& model) {
        std::expected<BegunStep, HandError> begun = BeginStep(model);
        if (!begun) {
            return std::unexpected(begun.error());
        }
        PlayerDecision& decision = begun->decision;
        if (decision.run) {
            while (!decision.run->Done()) {
                decision.run->Step();
            }
            return FinishStep(model, begun->actor, decision.run->Result());
        }
        return FinishStep(model, begun->actor, *decision.action);
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
