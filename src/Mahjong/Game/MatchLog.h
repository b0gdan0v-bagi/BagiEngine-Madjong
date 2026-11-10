#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>

namespace BECore::Mahjong {

    /// What happened at a table, in the order a match is played: it starts, a hand is dealt, a seat acts, a hand is settled, the match ends.
    CORE_ENUM(MatchLogKind, uint8_t, Start, Hand, Action, Settlement, End)

    /**
     * @brief One thing that happened in a match, as plain data. `kind` says which members are meant; the others keep their defaults.
     *
     * - `Start`: `seed`, `settings`, `seats` (who plays what), the `levels` of the seats (one per seat, also the seats a person
     *   plays); the first event of a match.
     * - `Hand`: a hand was dealt: `number` (hands counted so far), `east`, `roundWind`, `winds` and the dealt tiles of every seat
     *   (`concealed`, East's 14 included, and the `bonus` tiles already laid open). Nothing is hidden: the log is the owner's.
     * - `Action`: `seat` did `action`, a `person` or a `computer` (`by`). Only accepted actions are logged.
     * - `Settlement`: the hand was settled (`settlement`, the same `HandSettlement` `Match::History()` keeps); the next `Hand` or `End`
     *   follows it.
     * - `End`: the last hand is settled: the final `scores` and the `standings` (seats best first).
     *
     * `operator==` compares the members of the kind; for a `Settlement` the kind, winner, east, round wind, scores and deltas (the
     * breakdowns are for the reader of the log and are not part of what replays).
     */
    struct MatchLogEvent {
        MatchLogKind kind = MatchLogKind::Start;

        // Start
        uint32_t seed = 0;
        MahjongSettings settings;
        SeatSetup seats = kDefaultSeatSetup;
        SeatLevels levels = UniformLevels(kDefaultLevel);

        // Hand
        int number = 0;
        Seat east = 0;
        Wind roundWind = Wind::East;
        SeatWinds winds{};
        eastl::array<eastl::vector<Tile>, kSeatCount> concealed;
        eastl::array<eastl::vector<Tile>, kSeatCount> bonus;

        // Action
        Seat seat = kNoSeat;
        SeatKind by = SeatKind::Computer;
        HandAction action;

        // Settlement
        HandSettlement settlement;

        // End
        SeatScores scores{};
        eastl::array<Seat, kSeatCount> standings{};

        bool operator==(const MatchLogEvent& other) const;
    };

    /// The `Hand` event of the hand `match` has just dealt: what the session logs, and what a replay compares the log's `Hand` with.
    [[nodiscard]] MatchLogEvent MakeHandEvent(const Match& match);

    /// The `End` event of a match whose last hand was just settled: the final scores and standings.
    [[nodiscard]] MatchLogEvent MakeEndEvent(const Match& match);

    /// Receives the events of a session in order. The session calls it on the thread that drives the table.
    class IMatchLogSink {
    public:
        virtual ~IMatchLogSink() = default;
        virtual void OnEvent(const MatchLogEvent& event) = 0;
    };

    /// A sink that keeps the events, for tests and for a replay.
    class MatchLog final : public IMatchLogSink {
    public:
        void OnEvent(const MatchLogEvent& event) override {
            _events.push_back(event);
        }

        [[nodiscard]] const eastl::vector<MatchLogEvent>& Events() const noexcept {
            return _events;
        }
        void Clear() noexcept {
            _events.clear();
        }

    private:
        eastl::vector<MatchLogEvent> _events;
    };

    /// Why a replay stopped.
    CORE_ENUM(ReplayProblem, uint8_t,
              NoStart,             // the log does not begin with a `Start`, or an event comes when the match cannot take it
              BadSettings,         // the `Start` settings are not playable
              HandMismatch,        // a `Hand` event differs from the hand the match dealt
              ActionRefused,       // `Apply` refused an `Action` event
              SettlementMismatch,  // the hand ended differently (kind, winner, east, round wind, scores or deltas)
              SettlementRefused,   // a `Settlement` event while the hand is not finished (or the match is over)
              EndMismatch          // the final scores or standings, or the match is not over at `End`
    )

    /// The first event of a log that the match disagrees with.
    struct ReplayError {
        size_t index = 0;
        ReplayProblem problem = ReplayProblem::NoStart;
    };

    using ReplayStatus = std::expected<void, ReplayError>;

    /**
     * @brief Plays `events` on `match`: `Match::Start(settings, seed)`, `Hand().Apply(seat, action)` for every `Action`, `EndHand()`
     *        for every `Settlement`. No computer players are needed: the actions are in the log.
     *
     * The `Hand`, `Settlement` and `End` events are checked against what the match makes of the actions before them (the deal, the
     * settlement, the final scores), so a log that was changed, or made by other rules, stops at the first event that disagrees. A
     * second `Start` begins a new match on `match`. The `levels` of a `Start` are not needed (the computers' choices are in the `Action`
     * events), so it is read by nobody here. An empty log is `NoStart` at index 0. A log may lack its `Hand` and `End` events: what
     * is missing is not checked.
     *
     * With an `echo` every event that was applied is passed on in order, exactly as `TableSession` emits them live: the `Start`
     * and then the `Hand` the match dealt, each `Action`, each `Settlement` (the one the match made, with its breakdowns) and then
     * the next `Hand` or the `End`. The `Hand` and `End` events of the log itself are checked but not echoed (the match makes them
     * again), so a log with and a log without them echo the same events. Events reach the echo before a later event is found wrong:
     * validate on a scratch match first when a failure must leave no trace.
     */
    [[nodiscard]] ReplayStatus ReplayMatchLog(const eastl::vector<MatchLogEvent>& events, Match& match, IMatchLogSink* echo = nullptr);

}  // namespace BECore::Mahjong
