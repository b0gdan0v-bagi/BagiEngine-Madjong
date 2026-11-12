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
    };

    [[nodiscard]] TableKey KeyOf(const OnlineTable& table);

    /// Whether @p now is not another view of the table of @p before: the first table, another hand or match, a version that went back,
    /// a played hand after an ended one. Never true inside one hand, so the session's `StartCount()` moves only on a new one.
    [[nodiscard]] bool IsAnotherTable(const TableKey& before, const TableKey& now);

}  // namespace BECore::Mahjong
