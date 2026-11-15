#pragma once

#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Online/OnlineClient.h>
#include <Mahjong/Online/OnlineTable.h>

namespace BECore::Mahjong {

    /**
     * @brief The table of a view as the arguments of the display copies of `Game/` (`HandModel::ForDisplay`, `Match::ShowRemote`,
     *        `TableSession::ShowRemote`). Plain data, a member of its owner so that its lists keep their capacity.
     *
     * `hand.moves` is not filled by `MakeDisplayState` (a view has no history): the owner assigns it from a `MoveTracker`.
     */
    struct DisplayState {
        HandState hand;
        /// `handsPerMatch` from the view; the rest is the default (a room never offers flowers).
        MahjongSettings settings;
        /// `Match::ShowRemote` gets `MatchPhase::Finished`; the hand shown is the last.
        bool matchOver = false;
        int handsPlayed = 0;
        /// `Match::East()`: after the last hand the dealer of the hand that would come next, while `hand.east` is that of the hand shown.
        Seat east = 0;
        SeatScores scores{};
        SeatSetup seats = kDefaultSeatSetup;
        SeatLevels levels = UniformLevels(kDefaultLevel);
        SeatNames names;
        /// The client's own seat: the only one with real concealed tiles while a hand is played.
        Seat viewer = kNoSeat;
        Seat actor = kNoSeat;
    };

    /**
     * @brief Turns the table of a view and the lobby into the arguments of the display copies, written over @p out. A display model
     *        is read, never played or scored (`src/Mahjong/Online/CLAUDE.md`, "The display copy").
     *
     * - **Real:** the own tiles and melds, the open melds, bonus tiles, the pile, winds, scores, the turn, and once the hand is over
     *   the four hands of `result` and the result.
     * - **Invalid tiles:** the concealed tiles of the other seats, their concealed kongs (a `Kong` meld, `concealed`, after the open
     *   melds), the live wall, the loose reserve and the dead wall. The reserve is derived from the tile count, so
     *   `GetWall().Remaining()` equals the real model's.
     * - **Claim window:** the own seat is `Waiting` when it has a claim or a pass among `actions`, else `Passed`; the other seats are
     *   `Passed` (a view does not say what they decided).
     * - **Not in the view, left empty:** the drawn tile, the levels, the win's breakdown (read `result.scores` / `deltas`), and
     *   `callingAtDiscardCount` (set to `discardCount`, never "the first hand").
     * - **Seats:** a lobby seat of a person (also one that is away) is a `Person`, the others are computers. The actor is the viewer
     *   when it has actions, else the seat the hand waits for.
     */
    void MakeDisplayState(const OnlineTable& table, const OnlineLobby& lobby, DisplayState& out);

    /// Allocates: for a caller that reads once.
    [[nodiscard]] DisplayState MakeDisplayState(const OnlineTable& table, const OnlineLobby& lobby);

    /// @p newTable (from `IsAnotherTable`) moves the session's `StartCount()`.
    void ShowOnlineTable(Match& match, TableSession& session, const DisplayState& state, bool newTable);

    /// The melds of @p seat as the display hand holds them: another seat's open melds, then its concealed kongs. The index in this list
    /// is the `meld` of a `HandMove`.
    void DisplayMelds(const OnlineTable& table, Seat seat, Vector<Meld>& out);

    /// What tells one table from another (`IsAnotherTable`).
    struct TableKey {
        bool valid = false;
        int handsPlayed = 0;
        Seat east = kNoSeat;
        Seat seat = kNoSeat;
        uint64_t version = 0;
        SessionPhase phase = SessionPhase::Playing;
        /// The live wall only shrinks inside a hand: a bigger one is another hand (a replayed drawn hand keeps `handsPlayed`).
        int wall = 0;
    };

    [[nodiscard]] TableKey KeyOf(const OnlineTable& table);

    /// Whether @p now is a new hand or match, not another view of the table of @p before: the first table, another hand or match, a
    /// version that went back, a played hand after an ended one. Never true inside one hand.
    [[nodiscard]] bool IsAnotherTable(const TableKey& before, const TableKey& now);

    /**
     * @brief The `Moves()` of a display hand, inferred from the tables of two consecutive views of the same hand (`IsAnotherTable`),
     *        because a client never sees a hand move by move. The owner of the display pair calls `Follow` once per view.
     *
     * New pile entries are discards, the last pile entry gone with a new open meld that claims it is a claim, an open pung that became a
     * kong is an `UpgradeKong`, a new concealed kong a `ConcealedKong`; `meld` is the index in `DisplayMelds` of the new table
     * (`src/Mahjong/Online/CLAUDE.md`, "The display copy").
     *
     * The log starts empty on the first table (a view joined mid-hand flies nothing) and on every new hand; it only grows. **A step
     * that cannot be explained leaves the log as it was**: nothing is invented. Views are not one per move, so several moves of one
     * step come out in a plausible order, the real one only for a single move.
     */
    class MoveTracker {
    public:
        /// The returned log is valid until the next call.
        const Vector<HandMove>& Follow(const OnlineTable& table);
        [[nodiscard]] const Vector<HandMove>& Moves() const noexcept {
            return _moves;
        }
        /// The next `Follow` starts afresh.
        void Reset();

    private:
        struct Snapshot {
            Vector<DiscardEntry> pile;
            Array<Vector<Meld>, kSeatCount> open;
            Array<int, kSeatCount> concealedKongs{};
            Array<bool, kSeatCount> calling{};
        };

        void Take(const OnlineTable& table, Snapshot& into);
        /// False, with nothing appended, when the step from `_before` cannot be explained.
        bool Explain(const OnlineTable& table);

        Vector<HandMove> _moves;
        Snapshot _before;
        Snapshot _now;
        Vector<HandMove> _step;
        Vector<Meld> _melds;
        TableKey _key;
    };

}  // namespace BECore::Mahjong
