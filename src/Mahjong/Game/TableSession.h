#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/Match.h>

namespace BECore::Mahjong {

    /// Who plays a seat: a person (a front end asks for its actions) or a computer player (the session plays it).
    CORE_ENUM(SeatKind, uint8_t, Person, Computer)

    using SeatSetup = Array<SeatKind, kSeatCount>;

    /// The name of each seat's player, indexed by seat; empty where a table has none (a local game names no one).
    using SeatNames = Array<String, kSeatCount>;

    inline constexpr SeatSetup kDefaultSeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

    /// The level of the lowest computer seat (`kDefaultLevel` when none): what a client that knows one level for the table is told.
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

    /// The longest `nickname` of a log, in bytes: the length of a player's name in a room (`TextEntry` of kind `Name`).
    inline constexpr size_t kMaxMatchLogNicknameBytes = 24;

    /// Where and by which build a match was played, plain data of the `Start` event (kept here, like `SeatSetup`, because the session
    /// stores it and `MatchLog.h` includes this header). The game fills it; the rules know no platform: a session only copies what it
    /// was given (`TableSession::SetLogOrigin`).
    struct MatchLogOrigin {
        MatchPlatform platform = MatchPlatform::Unknown;
        String build;        // the build id (a short git hash), empty when not known
        String gameVersion;  // the version for testers, empty when not known
        String nickname;     // what a tester typed to be found by (only after a consent), empty: none

        bool operator==(const MatchLogOrigin&) const = default;
    };

    /// Where the table stands for a front end: some seat has to act, the hand is over and waits for `NextHand`, or the match is over.
    CORE_ENUM(SessionPhase, uint8_t, Playing, HandOver, MatchOver)

    /// The most steps `RunComputers` takes in one call; more is a defect.
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
        void Run() {
            while (!Done()) {
                Step();
            }
        }
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

    /// What a front end needs to key the hand an accepted action of a person was made in (`IPersonMoveObserver`): the session's
    /// `StartCount()`, the match's `HandsPlayed()` and the number of its settled hands (a replayed drawn hand has the same
    /// `HandsPlayed()` and one more settlement), and how many people sit at the table.
    struct MoveContext {
        uint64_t startCount = 0;
        int handsPlayed = 0;
        size_t historySize = 0;
        int personCount = 0;
    };

    /// Told of every accepted action of a person (`TableSession::SetMoveObserver`), with the view of the actor from before the action.
    class IPersonMoveObserver {
    public:
        virtual ~IPersonMoveObserver() = default;
        virtual void OnPersonMove(const TableView& before, const HandAction& action, const MoveContext& context) = 0;
    };

    /**
     * @brief The one object every front end drives: a `Match` with the seats set up as people and computers. Plain C++, no scene.
     *        The contract: `Game/CLAUDE.md` ("Table session").
     *
     * The control API, the table component and the HUD never drive `Match`, `HandModel` and `ComputerTable` by themselves, so "who
     * acts next" and "when does the state change" exist once. The session keeps a reference to the match, which belongs to its owner
     * (`MahjongGame`) and must outlive it.
     *
     * - `StepComputer` is both halves of a computer's step with the job run to its end, so its answers do not depend on who runs the
     *   job. `BeginComputerStep` draws the player's rng on the caller's thread; `FinishComputerStep` applies a done job only while
     *   `Version()` and `StartCount()` are the job's: any change in between makes it stale and nothing changes.
     * - `Version` grows on every change a front end must see and never goes back, so a front end redraws or publishes only when it
     *   moved. A refused call changes nothing.
     * - Hot seat: with exactly one person that seat is the viewer always; with none the viewer is `kNoSeat` (a spectator sees no
     *   concealed hand until the hand is over); with two or more the viewer is the person who acted last, and it becomes `kNoSeat`
     *   as soon as another person has to act, so the screen is covered (`HandoffPending`) until that person calls `Reveal`. In
     *   `HandOver` and `MatchOver` every hand is shown: front ends read `GetPhase()` then and not the viewer.
     * - The match log (`Game/MatchLog.h`): with a sink set (`SetLogSink`), every accepted change is reported to it as an event, in
     *   order: `StartMatch` (Start, Hand), `StepComputer` / `PerformPerson` (Action), `NextHand` (Settlement, then Hand or End). A
     *   refused call, `Reveal` and a start with bad settings report nothing. A match started behind the session's back (through the
     *   non-const `Match::Start`) is not seen and so not logged.
     * - **Remote mode** (`ShowRemote`): the session shows a table that is played elsewhere (an online room) over a match made by
     *   `Match::ShowRemote`. It plays nothing: every step, action, `NextHand` and `Reveal` gives its usual refusal, nothing is logged
     *   and no observer is told. `Actor()` is the actor the caller gave, `GetPhase()` still follows the match and `HandoffPending()` is
     *   false. An accepted `StartMatch` / `ResumeMatch` leaves the mode.
     * - The move observer (`SetMoveObserver`) is told of an accepted `PerformPerson` only, after the version moved and the log line
     *   went out, with `MakeTableView(hand, actor)` made before the action; with none set no view is made. Computer steps, `Reveal`,
     *   `NextHand` and `ResumeMatch` never report, a refused action reports nothing.
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
        MatchStatus StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, ComputerLevel level = kDefaultLevel);
        /// `StartMatch` where every computer seat plays by `profile` (a profile that is no level, e.g. a search seat; the seam of the
        /// tests). `Levels()` and the log's `Start` event report `kDefaultLevel`, and a saved match does not keep the profile: a match
        /// resumed from a save plays by its levels.
        MatchStatus StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, const LevelProfile& profile);
        /// `StartMatch` from a position (`Match::Start(settings, seed, state)`): the first hand is the deal of `state`, which the `Hand`
        /// event the sink gets shows, and the `Start` event carries as its `state`. `BadSettings` or `BadDeal`, and nothing changes (the version and the start count too).
        MatchStatus StartMatch(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, const SeatLevels& levels, const FixedStart& state);

        /**
         * @brief Puts the session and its match where `events` (a log from its `Start`, `Hand` and `End` events optional) left them,
         *        with `seats` and computer players at `levels`. A log that does not replay (`ReplayMatchLog`) is refused with its
         *        `ReplayError` and nothing changes (the match, the version, the levels, the seats, the viewer, the sink).
         *
         * The sink gets the whole log as a live match would (`ReplayMatchLog` with the sink as its echo), so a match log file and a
         * recorder are whole. The computer players are made again from the seed of the last `Start` and the random state they had is
         * not restored: play goes on legally and at the levels, not bit for bit as an uninterrupted match would. `StartCount()` grows: a resumed match is a new table, though its hands played and
         * history size may equal the old one's. The seats and the
         * levels are the caller's (the `Start` event's own, for a saved match); they are not compared with the log. The viewer is as
         * after `StartMatch`: with two or more people or none nobody, so the cover is up for a person to act (`HandoffPending`).
         * Include `Game/MatchLog.h` to call it.
         */
        std::expected<void, ReplayError> ResumeMatch(const Vector<MatchLogEvent>& events, const SeatSetup& seats, const SeatLevels& levels);
        std::expected<void, ReplayError> ResumeMatch(const Vector<MatchLogEvent>& events, const SeatSetup& seats, ComputerLevel level);

        /**
         * @brief Shows a table that is played elsewhere (the session goes into remote mode, see the class comment); the match has been
         *        set by `Match::ShowRemote`. Nothing is checked: the caller gives what a client knows.
         *
         * `seats` and `levels` are what the lobby says (a computer seat shows the level given), `names` the players' names for the
         * plates, `viewer` the seat whose tiles are face up (`kNoSeat` for none), `actor` the seat the table waits for (`kNoSeat`
         * when none, `Actor()` answers it while `Playing`). `Version()` grows; `StartCount()` only when `newTable` (another hand or
         * match, not another view of the same one).
         */
        void ShowRemote(const SeatSetup& seats, const SeatLevels& levels, const SeatNames& names, Seat viewer, Seat actor, bool newTable);
        [[nodiscard]] bool IsRemote() const noexcept {
            return _remote;
        }
        /// The name of the player of `seat` that `ShowRemote` gave; empty for a local game and for a number that is no seat.
        [[nodiscard]] const String& NameOf(Seat seat) const noexcept;

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

        /// Who is told of the accepted actions of the people (see the class comment); null (the default) for none. Not owned: it outlives
        /// its use by the session. May be changed at any time.
        void SetMoveObserver(IPersonMoveObserver* observer) noexcept {
            _observer = observer;
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
        [[nodiscard]] bool ActorIsPerson() const;

        /// One action of the actor, chosen by its computer player and applied. `WrongPhase` unless `Playing`, `NotYourTurn` when the
        /// actor is a person.
        StepResult StepComputer();
        /// The first half of `StepComputer`: the job of the actor's step, the player's rng drawn, the model and the version unchanged.
        /// `nullopt` (nothing drawn) unless `Playing` with a computer as the actor.
        [[nodiscard]] Optional<ComputerStepJob> BeginComputerStep();
        /// The second half: `job` (done) is applied, logged and counted as `StepComputer` does, and its `StepResult` returned; `nullopt`
        /// when the job is stale (`Version()` or `StartCount()` is not the job's), with nothing changed or logged.
        [[nodiscard]] Optional<StepResult> FinishComputerStep(const ComputerStepJob& job);
        /// `StepComputer` until a person has to act or the hand is over: the number of steps. At most `kMaxComputerSteps`; more
        /// (or a refused step) is a defect and asserts.
        int RunComputers();
        /// `action` for the actor, a person. `WrongPhase` unless `Playing`, `NotYourTurn` when the actor is a computer, otherwise the
        /// error of `HandModel::Apply`. An accepted action makes the actor the viewer (it has seen its hand).
        HandStatus PerformPerson(const HandAction& action);
        /// `Match::EndHand`, with its errors.
        std::expected<HandSettlement, MatchError> NextHand();
        /// The settlement `NextHand` applied last (the last element of `Match::History()`), nullopt before any. The settlement of a
        /// hand that is `HandOver` is `SettleHand(Hand())`.
        [[nodiscard]] Optional<HandSettlement> LastSettlement() const;

        /// The person whose concealed tiles the screen shows, `kNoSeat` for nobody (everything covered). See the class comment.
        [[nodiscard]] Seat Viewer() const noexcept {
            return _viewer;
        }
        /// `Playing`, two or more people, the actor is a person and is not the viewer: the screen waits for that person's `Reveal`.
        [[nodiscard]] bool HandoffPending() const;
        /// The actor, a person, becomes the viewer.
        void Reveal();

        /// 0 before the first start; see the class comment.
        [[nodiscard]] uint64_t Version() const noexcept {
            return _version;
        }
        /// The hand of `GetMatch()` was replaced from outside the session (a test that lays a model): `Version()` grows, so every view
        /// a front end keeps by the version is made again. The rules never call it.
        void MarkChanged() noexcept {
            ++_version;
        }
        /// The number of matches begun through `StartMatch` or put in place by `ResumeMatch` (a refused call is not counted): a front end
        /// tells a new match from the old one by it, whatever the new match has played since.
        [[nodiscard]] uint64_t StartCount() const noexcept {
            return _starts;
        }
        /// What the observer is told with a move.
        [[nodiscard]] MoveContext Context() const {
            return MoveContext{_starts, _match.HandsPlayed(), _match.History().size(), PersonCount()};
        }

    private:
        /// `StartMatch` with the table of `levels`, or of `profile` when it is not null; from `state` when that is not null.
        MatchStatus StartWith(const MahjongSettings& settings, uint32_t seed, const SeatSetup& seats, const SeatLevels& levels, const LevelProfile* profile, const FixedStart* state);
        /// The job of the actor's step, or why there is none (the errors of `StepComputer`).
        [[nodiscard]] std::expected<ComputerStepJob, HandError> MakeComputerJob();
        /// Applies the done `job`, then logs, covers and bumps the version.
        StepResult ApplyComputerJob(const ComputerStepJob& job);
        /// After every change of the actor: with two or more people the viewer is `kNoSeat` when another person has to act.
        void CoverForNextPerson();
        /// Reports the action the actor `seat` has just done.
        void LogAction(Seat seat, const HandAction& action);
        /// Leaves remote mode (a local match was started or resumed).
        void LeaveRemote();
        /// Reports the hand that was just dealt, or the end of the match when the last hand was settled.
        void LogAfterSettlement();

        Match& _match;
        Optional<ComputerTable> _table;
        SeatSetup _seats = kDefaultSeatSetup;
        SeatLevels _levels = UniformLevels(kDefaultLevel);
        Seat _viewer = kNoSeat;
        uint64_t _version = 0;
        uint64_t _starts = 0;
        IMatchLogSink* _sink = nullptr;
        IPersonMoveObserver* _observer = nullptr;
        MatchLogOrigin _origin;
        bool _remote = false;
        SeatNames _names;
        Seat _remoteActor = kNoSeat;
    };

}  // namespace BECore::Mahjong
