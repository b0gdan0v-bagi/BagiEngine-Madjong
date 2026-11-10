#include "TableSession.h"

#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong {

    namespace {

        /// The one person of a setup, kNoSeat unless there is exactly one.
        Seat OnlyPerson(const SeatSetup& seats) noexcept {
            Seat found = kNoSeat;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seats[static_cast<size_t>(seat)] == SeatKind::Person) {
                    if (found != kNoSeat) {
                        return kNoSeat;
                    }
                    found = seat;
                }
            }
            return found;
        }

    }  // namespace

    MatchStatus TableSession::StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, ComputerLevel level) {
        return StartMatch(settings, seed, seats, UniformLevels(level));
    }

    MatchStatus TableSession::StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, const SeatLevels& levels) {
        if (const MatchStatus started = _match.Start(settings, seed); !started) {
            return started;
        }
        _table.emplace(seed, levels);
        _seats = seats;
        _levels = levels;
        _viewer = PersonCount() == 1 ? OnlyPerson(_seats) : kNoSeat;
        ++_version;
        ++_starts;
        if (_sink != nullptr) {
            MatchLogEvent start;
            start.kind = MatchLogKind::Start;
            start.seed = seed;
            start.settings = settings;
            start.seats = seats;
            start.level = FirstComputerLevel(seats, levels);  // one level until the match log keeps one per seat
            _sink->OnEvent(start);
            _sink->OnEvent(MakeHandEvent(_match));
        }
        return {};
    }

    ReplayStatus TableSession::ResumeMatch(const eastl::vector<MatchLogEvent>& events, const SeatSetup& seats, ComputerLevel level) {
        return ResumeMatch(events, seats, UniformLevels(level));
    }

    ReplayStatus TableSession::ResumeMatch(const eastl::vector<MatchLogEvent>& events, const SeatSetup& seats, const SeatLevels& levels) {
        Match scratch;
        if (ReplayStatus checked = ReplayMatchLog(events, scratch); !checked) {
            return checked;
        }
        uint32_t seed = 0;
        for (const MatchLogEvent& event : events) {
            seed = event.kind == MatchLogKind::Start ? event.seed : seed;
        }
        const ReplayStatus replayed = ReplayMatchLog(events, _match, _sink);
        ASSERT(replayed.has_value(), "ResumeMatch: a log that replayed on a scratch match does not replay");
        if (!replayed) {
            return replayed;
        }
        _table.emplace(seed, levels);
        _seats = seats;
        _levels = levels;
        _viewer = PersonCount() == 1 ? OnlyPerson(_seats) : kNoSeat;
        ++_version;
        return {};
    }

    SessionPhase TableSession::GetPhase() const noexcept {
        if (_match.GetPhase() != MatchPhase::InHand) {
            return SessionPhase::MatchOver;
        }
        return _match.Hand().GetPhase() == Phase::Finished ? SessionPhase::HandOver : SessionPhase::Playing;
    }

    SeatKind TableSession::KindOf(Seat seat) const noexcept {
        return IsSeat(seat) ? _seats[static_cast<size_t>(seat)] : SeatKind::Computer;
    }

    int TableSession::PersonCount() const noexcept {
        int count = 0;
        for (const SeatKind kind : _seats) {
            count += kind == SeatKind::Person ? 1 : 0;
        }
        return count;
    }

    Seat TableSession::Actor() const {
        return GetPhase() == SessionPhase::Playing ? ComputerTable::NextActor(_match.Hand()) : kNoSeat;
    }

    bool TableSession::ActorIsPerson() const {
        const Seat actor = Actor();
        return IsSeat(actor) && KindOf(actor) == SeatKind::Person;
    }

    StepResult TableSession::StepComputer() {
        if (GetPhase() != SessionPhase::Playing || !_table) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (ActorIsPerson()) {
            return std::unexpected(HandError::NotYourTurn);
        }
        const Seat actor = Actor();
        const StepResult step = _table->Step(_match.Hand());
        if (step) {
            LogAction(actor, *step);
            CoverForNextPerson();
            ++_version;
        }
        return step;
    }

    int TableSession::RunComputers() {
        int steps = 0;
        while (GetPhase() == SessionPhase::Playing && !ActorIsPerson()) {
            if (steps >= kMaxComputerSteps) {
                ASSERT(false, "RunComputers: the hand does not end");
                break;
            }
            if (const StepResult step = StepComputer(); !step) {
                ASSERT(false, "RunComputers: a computer player's action was refused");
                break;
            }
            ++steps;
        }
        return steps;
    }

    HandStatus TableSession::PerformPerson(const HandAction& action) {
        if (GetPhase() != SessionPhase::Playing) {
            return std::unexpected(HandError::WrongPhase);
        }
        const Seat actor = Actor();
        if (KindOf(actor) != SeatKind::Person) {
            return std::unexpected(HandError::NotYourTurn);
        }
        if (const HandStatus applied = _match.Hand().Apply(actor, action); !applied) {
            return applied;
        }
        if (PersonCount() >= 2) {
            _viewer = actor;
        }
        LogAction(actor, action);
        CoverForNextPerson();
        ++_version;
        return {};
    }

    std::expected<HandSettlement, MatchError> TableSession::NextHand() {
        std::expected<HandSettlement, MatchError> settled = _match.EndHand();
        if (settled) {
            if (_sink != nullptr) {
                MatchLogEvent event;
                event.kind = MatchLogKind::Settlement;
                event.settlement = *settled;
                _sink->OnEvent(event);
                LogAfterSettlement();
            }
            CoverForNextPerson();
            ++_version;
        }
        return settled;
    }

    std::optional<HandSettlement> TableSession::LastSettlement() const {
        const eastl::vector<HandSettlement>& history = _match.History();
        return history.empty() ? std::nullopt : std::optional<HandSettlement>(history.back());
    }

    bool TableSession::HandoffPending() const {
        return PersonCount() >= 2 && ActorIsPerson() && Actor() != _viewer;
    }

    void TableSession::Reveal() {
        if (!ActorIsPerson()) {
            return;
        }
        const Seat actor = Actor();
        if (PersonCount() >= 2 && _viewer != actor) {
            _viewer = actor;
            ++_version;
        }
    }

    void TableSession::LogAction(Seat seat, const HandAction& action) {
        if (_sink == nullptr) {
            return;
        }
        MatchLogEvent event;
        event.kind = MatchLogKind::Action;
        event.seat = seat;
        event.by = KindOf(seat);
        event.action = action;
        _sink->OnEvent(event);
    }

    void TableSession::LogAfterSettlement() {
        if (_sink == nullptr) {
            return;
        }
        if (_match.GetPhase() == MatchPhase::InHand) {
            _sink->OnEvent(MakeHandEvent(_match));
            return;
        }
        _sink->OnEvent(MakeEndEvent(_match));
    }

    void TableSession::CoverForNextPerson() {
        if (HandoffPending()) {
            _viewer = kNoSeat;
        }
    }

}  // namespace BECore::Mahjong
