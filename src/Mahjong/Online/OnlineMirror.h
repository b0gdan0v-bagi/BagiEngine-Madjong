#pragma once

#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Online/OnlineClient.h>
#include <Mahjong/Online/OnlineTable.h>

namespace BECore::Mahjong {

    /**
     * @brief Everything `ShowOnlineTable` gives to the display copies of `Game/` (`HandModel::ForDisplay`, `Match::ShowRemote`,
     *        `TableSession::ShowRemote`): the table of a view as the arguments of those calls. Plain data, a member of its owner so that
     *        its lists keep their capacity from one view to the next.
     *
     * `hand.moves` is not filled by `MakeDisplayState` (a view has no history): the owner assigns it from a `MoveTracker`.
     */
    struct DisplayState {
        HandState hand;
        /// `handsPerMatch` from the view; the rest is the default (a room never offers flowers).
        MahjongSettings settings;
        /// The match is over: `Match::ShowRemote` gets `MatchPhase::Finished`, the hand is its last.
        bool matchOver = false;
        int handsPlayed = 0;
        /// The dealer of the progress (`Match::East()`): after the last hand the dealer of the hand that would come next, while
        /// `hand.east` is the dealer of the hand shown.
        Seat east = 0;
        SeatScores scores{};
        SeatSetup seats = kDefaultSeatSetup;
        SeatLevels levels = UniformLevels(kDefaultLevel);
        SeatNames names;
        /// The client's own seat: its tiles are the only real concealed ones while a hand is played.
        Seat viewer = kNoSeat;
        Seat actor = kNoSeat;
    };

    /**
     * @brief Turns the table of a view and the lobby into the arguments of the display copies, written over @p out (its lists keep
     *        their capacity).
     *
     * - **Real:** the own concealed tiles and melds (the concealed kongs among them), the open melds, bonus tiles, the pile, the seat
     *   winds (from `east`), the scores, the turn, and, once the hand is over, the four hands of `result` and the result itself.
     * - **Invalid tiles:** the concealed tiles of the other seats (`concealedCount` of them), their concealed kongs (a `Kong` meld,
     *   `concealed`, with an invalid `first`, placed after the open melds), the live wall (`wall` of them), the loose reserve and the
     *   dead wall. The reserve is derived from the tile count (the set minus what lies in hands, melds and pile), so
     *   `GetWall().Remaining()` equals the real model's; the split into three lists is otherwise arbitrary but fixed.
     * - **Claim window:** the own seat is `Waiting` when it has a claim or a pass among `actions`, else `Passed`; the discarder is not
     *   in the window and the other seats are `Passed` (a view does not say what they decided). There is no `LastClaimWindow`.
     * - **Not in the view, left empty:** the drawn tile (`DrawnTile` is invalid for every seat), `callingAtDiscardCount` (set to
     *   `discardCount`, never "the first hand": do not score a display model), the levels (all `kDefaultLevel`) and the win's
     *   breakdown (`SettleHand` of a display model is not the room's settlement; read `result.scores` / `deltas`).
     * - **Seats:** a lobby seat of a person (also one that is away) is a `Person`, the others are computers. The actor is the viewer
     *   when it has actions, else the seat the hand waits for (in a window the first seat after the discarder that is not the viewer).
     */
    void MakeDisplayState(const OnlineTable& table, const OnlineLobby& lobby, DisplayState& out);

    /// The same into a new state (allocates; for a caller that reads once).
    [[nodiscard]] DisplayState MakeDisplayState(const OnlineTable& table, const OnlineLobby& lobby);

    /// Puts @p state into @p match and @p session as display copies (`Match::ShowRemote`, `TableSession::ShowRemote`). @p newTable
    /// moves the session's `StartCount()` (`IsAnotherTable`).
    void ShowOnlineTable(Match& match, TableSession& session, const DisplayState& state, bool newTable);

    /// The melds of @p seat as the display hand holds them: the own seat's as the view lists them, another's open melds and then its
    /// concealed kongs with invalid tiles. The index of a meld in this list is the `meld` of a `HandMove` (`MoveTracker`).
    void DisplayMelds(const OnlineTable& table, Seat seat, eastl::vector<Meld>& out);

    /// What tells one table from another: a new hand or match (`handsPlayed`, east), a view that went back in time, the same hand
    /// replayed after a draw (the earlier view was over, this one is played) or another seat of the viewer.
    struct TableKey {
        bool valid = false;
        int handsPlayed = 0;
        Seat east = kNoSeat;
        Seat seat = kNoSeat;
        uint64_t version = 0;
        SessionPhase phase = SessionPhase::Playing;
        /// The live wall only shrinks inside a hand: a bigger one is another hand (a replayed drawn hand has the same `handsPlayed`).
        int wall = 0;
    };

    [[nodiscard]] TableKey KeyOf(const OnlineTable& table);

    /// Whether @p now is not another view of the table of @p before: the first table, another hand or match, a version that went back,
    /// a played hand after an ended one. Never true inside one hand, so the session's `StartCount()` moves only on a new one.
    [[nodiscard]] bool IsAnotherTable(const TableKey& before, const TableKey& now);

    /**
     * @brief The `Moves()` of a display hand: the public log of a hand that a client never saw move by move, inferred from the tables
     *        of two consecutive views. Kept by the owner of the display pair; `Follow` once per view.
     *
     * A view is a state, not a move, so each table is compared with the previous one of the same hand (`IsAnotherTable`):
     * - new entries of the pile are `Discard` moves (`declaredCalling` when the seat's `calling` flag turned on in this step and it made one new
     *   discard: with two the declaring one cannot be told), in the order of the pile;
     * - the last pile entry gone with a new open meld that claims it is a claim (`ClaimChow` / `ClaimPung` / `ClaimKong` by the meld's
     *   kind, `from` its discarder, `tile` the claimed tile) and comes before the discards that follow; with the hand won on a
     *   discard it is no move;
     * - an open pung that became a kong is an `UpgradeKong`, a new concealed kong a `ConcealedKong` (no tile); a kong comes before the
     *   next discard of its seat, or at the end of the step;
     * - `meld` is the index in `DisplayMelds` of the new table.
     *
     * The log starts empty on the first table (no flights for the view a client joined in) and on every new hand; it only grows. A step
     * that cannot be explained (a pile that shrank with no claim, a meld that vanished, two tiles gone, a kong whose place among the discards of the
     * step cannot be told) leaves the log as it was:
     * nothing is invented. Views are not one per move, so moves the client missed are coalesced: several moves of one step come out
     * in the order above, which is the real order for one move and a plausible one for more.
     */
    class MoveTracker {
    public:
        /// Takes in the next table of the hand and returns the log (valid until the next call).
        const eastl::vector<HandMove>& Follow(const OnlineTable& table);
        [[nodiscard]] const eastl::vector<HandMove>& Moves() const noexcept {
            return _moves;
        }
        /// Forgets the table and the log: the next `Follow` starts afresh.
        void Reset();

    private:
        /// What the next step needs of the previous table.
        struct Snapshot {
            eastl::vector<DiscardEntry> pile;
            eastl::array<eastl::vector<Meld>, kSeatCount> open;
            eastl::array<int, kSeatCount> concealedKongs{};
            eastl::array<bool, kSeatCount> calling{};
        };

        void Take(const OnlineTable& table, Snapshot& into);
        /// Appends the moves of the step from `_before` to `table`; false (nothing appended) when the step cannot be explained.
        bool Explain(const OnlineTable& table);

        eastl::vector<HandMove> _moves;
        Snapshot _before;
        Snapshot _now;
        eastl::vector<HandMove> _step;
        eastl::vector<Meld> _melds;
        TableKey _key;
    };

}  // namespace BECore::Mahjong
