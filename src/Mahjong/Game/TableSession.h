#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/Match.h>

namespace BECore::Mahjong {

    /// Who plays a seat: a person (a front end asks for its actions) or a computer player (the session plays it).
    CORE_ENUM(SeatKind, uint8_t, Person, Computer)

    /// The kind of each seat, indexed by seat.
    using SeatSetup = eastl::array<SeatKind, kSeatCount>;

    /// Seat 0 is a person, the others are computers.
    inline constexpr SeatSetup kDefaultSeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

    /// The level of the lowest seat that `seats` makes a computer, `kDefaultLevel` when no seat is one: what a client that knows one level
    /// for the whole table is told.
    [[nodiscard]] constexpr ComputerLevel FirstComputerLevel(const SeatSetup& seats, const SeatLevels& levels) noexcept {
        for (size_t seat = 0; seat < seats.size(); ++seat) {
            if (seats[seat] == SeatKind::Computer) {
                return levels[seat];
            }
        }
        return kDefaultLevel;
    }

    /// Where the table stands for a front end: some seat has to act, the hand is over and waits for `NextHand`, or the match is over.
    CORE_ENUM(SessionPhase, uint8_t, Playing, HandOver, MatchOver)

    /// The most steps `RunComputers` takes in one call; a hand of four computer players takes a few hundred, so more is a defect.
    inline constexpr int kMaxComputerSteps = 2000;

    class IMatchLogSink;
    struct MatchLogEvent;
    struct ReplayError;

    /**
     * @brief The one object every front end drives: a `Match` with the seats set up as people and computers. Plain C++, no scene.
     *
     * The control API, the table component and the HUD never drive `Match`, `HandModel` and `ComputerTable` by themselves, so the
     * rules of "who acts next" and "when does the state change" exist once. The session keeps a reference to the match, which belongs
     * to its owner (`MahjongGame`) and must outlive it.
     *
     * - `StartMatch` begins a match and the computer players of the same seed, each seat at its level (`SeatLevels`: one number for
     *   the whole table is `UniformLevels`). The levels of seats a person plays are kept too and play nothing.
     * - `Actor` is the seat that has to act (the first pending seat in a claim window; the result does not depend on the order).
     *   `StepComputer` plays one action of a computer actor, `RunComputers` plays until a person has to act or the hand is over,
     *   `PerformPerson` applies the action of a person actor, `NextHand` settles a finished hand and deals on.
     * - `Version` grows on every change a front end must see (a start, a step, an accepted action, a settled hand, a `Reveal` that
     *   changes the viewer) and never goes back, so a front end redraws or publishes only when it moved. A refused call changes nothing.
     * - Hot seat: with exactly one person that seat is the viewer always (no cover); with none the viewer is `kNoSeat` (a spectator
     *   sees no concealed hand until the hand is over); with two or more the viewer is the person who acted last, and it becomes
     *   `kNoSeat` as soon as another person has to act (a start, a step, an action, a settled hand), so the screen is covered
     *   (`HandoffPending`) until that person calls `Reveal`. The same person acting again keeps the viewer. In `HandOver` and
     *   `MatchOver` every hand is shown: front ends read `GetPhase()` then and not the viewer.
     * - The match log (`Game/MatchLog.h`): with a sink set (`SetLogSink`), every accepted change is reported to it as an event, in
     *   order: `StartMatch` (Start, Hand), `StepComputer` / `PerformPerson` (Action), `NextHand` (Settlement, then Hand or End). A
     *   refused call, `Reveal` and a start with bad settings report nothing. A match started behind the session's back (through the
     *   non-const `Match::Start`) is not seen and so not logged.
     * - `ResumeMatch` puts the session where a recorded log left it (a saved match), see there.
     */
    class TableSession {
    public:
        /// The match belongs to the caller; the session keeps a reference.
        explicit TableSession(Match& match) : _match(match) {}

        TableSession(const TableSession&) = delete;
        TableSession& operator=(const TableSession&) = delete;

        /// `Match::Start(settings, seed)` with `seats` and computer players seeded `seed`, seat `i` at `levels[i]`. `BadSettings`, and
        /// nothing changes (the version and the levels too), unless `IsPlayable(settings)`.
        MatchStatus StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, const SeatLevels& levels);
        /// `StartMatch` with `UniformLevels(level)`.
        MatchStatus StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, ComputerLevel level = kDefaultLevel);

        /**
         * @brief Puts the session and its match where `events` (a log from its `Start`, `Hand` and `End` events optional) left them,
         *        with `seats` and computer players at `levels`. A log that does not replay (`ReplayMatchLog`) is refused with its
         *        `ReplayError` and nothing changes (the match, the version, the levels, the seats, the viewer, the sink).
         *
         * The sink gets the whole log as a live match would (`ReplayMatchLog` with the sink as its echo), so a match log file and a
         * recorder are whole. The computer players are made again from the seed of the last `Start` and the random state they had is
         * not restored: play goes on legally and at the levels, not bit for bit as an uninterrupted match would. The seats and the
         * levels are the caller's (the `Start` event's own, for a saved match); they are not compared with the log. The viewer is as
         * after `StartMatch`: with two or more people or none nobody, so the cover is up for a person to act (`HandoffPending`).
         * Include `Game/MatchLog.h` to call it.
         */
        std::expected<void, ReplayError> ResumeMatch(const eastl::vector<MatchLogEvent>& events, const SeatSetup& seats, const SeatLevels& levels);
        /// `ResumeMatch` with `UniformLevels(level)`.
        std::expected<void, ReplayError> ResumeMatch(const eastl::vector<MatchLogEvent>& events, const SeatSetup& seats, ComputerLevel level);

        /// The level of each seat of the match started last (the entries of person seats are kept, they play nothing); all `kDefaultLevel`
        /// before the first start.
        [[nodiscard]] const SeatLevels& Levels() const noexcept {
            return _levels;
        }
        /// `Levels()` of `seat`; `kDefaultLevel` for a number that is no seat.
        [[nodiscard]] ComputerLevel LevelOf(Seat seat) const noexcept {
            return IsSeat(seat) ? _levels[static_cast<size_t>(seat)] : kDefaultLevel;
        }

        /// Where the events of the match go (see the class comment); null (the default) for none. Not owned: the sink outlives its use
        /// by the session. May be changed at any time, also in the middle of a match.
        void SetLogSink(IMatchLogSink* sink) noexcept {
            _sink = sink;
        }
        [[nodiscard]] IMatchLogSink* GetLogSink() const noexcept {
            return _sink;
        }

        /// `MatchOver` before the first start, too.
        [[nodiscard]] SessionPhase GetPhase() const noexcept;
        [[nodiscard]] const SeatSetup& Seats() const noexcept {
            return _seats;
        }
        /// `Computer` for a number that is no seat.
        [[nodiscard]] SeatKind KindOf(Seat seat) const noexcept;
        [[nodiscard]] int PersonCount() const noexcept;

        /// The seat to act while `Playing`, `kNoSeat` otherwise.
        [[nodiscard]] Seat Actor() const;
        /// Whether the actor is a seat of a person.
        [[nodiscard]] bool ActorIsPerson() const;

        /// One action of the actor, chosen by its computer player and applied. `WrongPhase` unless `Playing`, `NotYourTurn` when the
        /// actor is a person.
        StepResult StepComputer();
        /// `StepComputer` until a person has to act or the hand is over: the number of steps. At most `kMaxComputerSteps`; more
        /// (or a refused step) is a defect and asserts.
        int RunComputers();
        /// `action` for the actor, a person. `WrongPhase` unless `Playing`, `NotYourTurn` when the actor is a computer, otherwise the
        /// error of `HandModel::Apply`. An accepted action makes the actor the viewer (it has seen its hand).
        HandStatus PerformPerson(const HandAction& action);
        /// `Match::EndHand`: settles the finished hand and deals the next. `HandNotFinished` and `NotInHand` as there.
        std::expected<HandSettlement, MatchError> NextHand();
        /// The settlement `NextHand` applied last (the last element of `Match::History()`), nullopt before any. The settlement of a
        /// hand that is `HandOver` is `SettleHand(Hand())`.
        [[nodiscard]] std::optional<HandSettlement> LastSettlement() const;

        /// The person whose concealed tiles the screen shows, `kNoSeat` for nobody (everything covered). See the class comment.
        [[nodiscard]] Seat Viewer() const noexcept {
            return _viewer;
        }
        /// `Playing`, two or more people, the actor is a person and is not the viewer: the screen waits for that person's `Reveal`.
        [[nodiscard]] bool HandoffPending() const;
        /// The actor, a person, becomes the viewer (nothing when no person has to act).
        void Reveal();

        /// Grows on every change a front end must see, never goes back; 0 before the first start.
        [[nodiscard]] uint64_t Version() const noexcept {
            return _version;
        }
        /// The number of matches begun through `StartMatch` (a refused start is not counted): a front end tells a new match from the old one
        /// by it, whatever the new match has played since.
        [[nodiscard]] uint64_t StartCount() const noexcept {
            return _starts;
        }

    private:
        /// After every change of the actor: with two or more people the viewer is `kNoSeat` when another person has to act.
        void CoverForNextPerson();
        /// Reports the action the actor `seat` has just done.
        void LogAction(Seat seat, const HandAction& action);
        /// Reports the hand that was just dealt, or the end of the match when the last hand was settled.
        void LogAfterSettlement();

        Match& _match;
        std::optional<ComputerTable> _table;
        SeatSetup _seats = kDefaultSeatSetup;
        SeatLevels _levels = UniformLevels(kDefaultLevel);
        Seat _viewer = kNoSeat;
        uint64_t _version = 0;
        uint64_t _starts = 0;
        IMatchLogSink* _sink = nullptr;
    };

}  // namespace BECore::Mahjong
