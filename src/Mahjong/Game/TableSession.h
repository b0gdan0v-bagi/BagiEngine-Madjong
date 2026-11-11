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

    /// Where a match was played; `Unknown` is what a log of an older format says (the origin was not recorded).
    CORE_ENUM(MatchPlatform, uint8_t, Unknown, Desktop, Web)

    /// Where and by which build a match was played, plain data of the `Start` event (kept here, like `SeatSetup`, because the session
    /// stores it and `MatchLog.h` includes this header). The game fills it; the rules know no platform: a session only copies what it
    /// was given (`TableSession::SetLogOrigin`).
    struct MatchLogOrigin {
        MatchPlatform platform = MatchPlatform::Unknown;
        eastl::string build;        // the build id (a short git hash), empty when not known
        eastl::string gameVersion;  // the version for testers, empty when not known

        bool operator==(const MatchLogOrigin&) const = default;
    };

    /// Where the table stands for a front end: some seat has to act, the hand is over and waits for `NextHand`, or the match is over.
    CORE_ENUM(SessionPhase, uint8_t, Playing, HandOver, MatchOver)

    /// The most steps `RunComputers` takes in one call; a hand of four computer players takes a few hundred, so more is a defect.
    inline constexpr int kMaxComputerSteps = 2000;

    class IMatchLogSink;
    struct MatchLogEvent;
    struct ReplayError;

    /**
     * @brief One step of a computer actor, begun and not yet applied (`TableSession::BeginComputerStep`): a value type that holds
     *        copies only and no pointer to the session, so it can be run on another thread and dropped with nothing changed.
     *
     * The decision is ready (an action: `Ready()`) or a search to play (`Step()` plays one rollout, `Run()` all of them). The
     * `Version()` and `StartCount()` are those of the session when the job was begun; `FinishComputerStep` applies it only while
     * both are unchanged. `Action()` is valid when `Done()`.
     */
    class ComputerStepJob {
    public:
        /// The seat that acts.
        [[nodiscard]] Seat Actor() const noexcept {
            return _actor;
        }
        /// The decision needs no search: `Done()` from the start.
        [[nodiscard]] bool Ready() const noexcept {
            return _decision.action.has_value();
        }
        [[nodiscard]] bool Done() const noexcept {
            return Ready() || (_decision.run && _decision.run->Done());
        }
        /// One rollout of the search; nothing for a ready or a done job.
        void Step() {
            if (_decision.run && !_decision.run->Done()) {
                _decision.run->Step();
            }
        }
        /// `Step` until `Done()`.
        void Run() {
            while (!Done()) {
                Step();
            }
        }
        /// The action chosen; valid when `Done()`.
        [[nodiscard]] HandAction Action() const {
            return Ready() ? *_decision.action : _decision.run->Result();
        }
        /// `TableSession::Version()` / `StartCount()` when the job was begun.
        [[nodiscard]] uint64_t Version() const noexcept {
            return _version;
        }
        [[nodiscard]] uint64_t StartCount() const noexcept {
            return _starts;
        }

    private:
        friend class TableSession;

        Seat _actor = kNoSeat;
        PlayerDecision _decision;
        uint64_t _version = 0;
        uint64_t _starts = 0;
    };

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
     * - A computer's step in two halves (a search seat may take long, so a front end can compute it elsewhere): `BeginComputerStep`
     *   makes a `ComputerStepJob` (the player's rng draws here, on the caller's thread) and `FinishComputerStep` applies a done job
     *   as `StepComputer` does, but only while `Version()` and `StartCount()` are those of the job: any change in between (a start, a
     *   resume, another step, a person's action, a settled hand, a `Reveal` that moved the viewer) makes it stale and nothing changes.
     *   `StepComputer` is both halves with the job run to its end, so its answers do not depend on who runs the job.
     * - `Version` grows on every change a front end must see (a start, a step, an accepted action, a settled hand, a `Reveal` that
     *   changes the viewer) and never goes back, so a front end redraws or publishes only when it moved. A refused call changes nothing.
     * - Hot seat: with exactly one person that seat is the viewer always (no cover); with none the viewer is `kNoSeat` (a spectator
     *   sees no concealed hand until the hand is over); with two or more the viewer is the person who acted last, and it becomes
     *   `kNoSeat` as soon as another person has to act (a start, a step, an action, a settled hand), so the screen is covered
     *   (`HandoffPending`) until that person calls `Reveal`. The same person acting again keeps the viewer. In `HandOver` and
     *   `MatchOver` every hand is shown: front ends read `GetPhase()` then and not the viewer.
     * - The match log (`Game/MatchLog.h`): with a sink set (`SetLogSink`), every accepted change is reported to it as an event, in
     *   order (the `Start` carries the origin set by `SetLogOrigin`): `StartMatch` (Start, Hand), `StepComputer` / `PerformPerson` (Action), `NextHand` (Settlement, then Hand or End). A
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
        /// `StartMatch` where every computer seat plays by `profile` (a profile that is no level, e.g. a search seat; the seam of the
        /// tests). `Levels()` and the log's `Start` event report `kDefaultLevel`, and a saved match does not keep the profile: a match
        /// resumed from a save plays by its levels.
        MatchStatus StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, const LevelProfile& profile);

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

        /// Where the matches begun from now on say they were played: stamped into every `Start` event. A resumed match echoes the `Start`
        /// of its log, so a save keeps the origin of the build that began the match. Default: `Unknown`, empty, empty.
        void SetLogOrigin(const MatchLogOrigin& origin) {
            _origin = origin;
        }
        [[nodiscard]] const MatchLogOrigin& LogOrigin() const noexcept {
            return _origin;
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
        /// The first half of `StepComputer`: the job of the actor's step, the player's rng drawn, the model and the version unchanged.
        /// `nullopt` (nothing drawn) unless `Playing` with a computer as the actor.
        [[nodiscard]] std::optional<ComputerStepJob> BeginComputerStep();
        /// The second half: `job` (done) is applied, logged and counted as `StepComputer` does, and its `StepResult` returned; `nullopt`
        /// when the job is stale (`Version()` or `StartCount()` is not the job's), with nothing changed or logged.
        [[nodiscard]] std::optional<StepResult> FinishComputerStep(const ComputerStepJob& job);
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
        /// The hand of `GetMatch()` was replaced from outside the session (a test that lays a model): `Version()` grows, so every view
        /// a front end keeps by the version is made again. The rules never call it.
        void MarkChanged() noexcept {
            ++_version;
        }
        /// The number of matches begun through `StartMatch` (a refused start is not counted): a front end tells a new match from the old one
        /// by it, whatever the new match has played since.
        [[nodiscard]] uint64_t StartCount() const noexcept {
            return _starts;
        }

    private:
        /// `StartMatch` with the table of `levels`, or of `profile` when it is not null.
        MatchStatus StartWith(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, const SeatLevels& levels, const LevelProfile* profile);
        /// The job of the actor's step, or why there is none (the errors of `StepComputer`).
        [[nodiscard]] std::expected<ComputerStepJob, HandError> MakeComputerJob();
        /// Applies the done `job`, then logs, covers and bumps the version.
        StepResult ApplyComputerJob(const ComputerStepJob& job);
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
        MatchLogOrigin _origin;
    };

}  // namespace BECore::Mahjong
