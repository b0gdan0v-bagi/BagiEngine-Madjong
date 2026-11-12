#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/MahjongGameControl.h>

namespace BECore::Mahjong {

    /// One entry of `seats` as the server sent it. The own seat lists its concealed kongs among `melds` (`concealed`), the others show
    /// their open melds only and count the concealed kongs.
    struct OnlineSeat {
        Wind wind = Wind::East;
        int score = 0;
        int concealedCount = 0;
        /// A meld with `from` and `claimed` that JSON gave as null holds `kNoSeat` and an invalid tile.
        eastl::vector<Meld> melds;
        int concealedKongs = 0;
        eastl::vector<Tile> bonus;
        int discardCount = 0;
        bool calling = false;
    };

    /// What the hand waits for: the turn of the table, the same words as `TableView`.
    struct OnlineTurn {
        Phase phase = Phase::Finished;
        Seat current = kNoSeat;
        Tile pendingTile;
        /// The server's flag. A client reads `OnlineTable::actions`, not this (`Online/CLAUDE.md`).
        bool yourTurn = false;
    };

    /// The tiles of one seat once the hand is over: all four hands are public then.
    struct OnlineResultSeat {
        eastl::vector<Tile> concealed;
        eastl::vector<Meld> melds;
        eastl::vector<Tile> bonus;
    };

    /// How a finished hand ended. The breakdown lines are not read here: they stay in the raw view of the client.
    struct OnlineResult {
        ResultKind kind = ResultKind::Drawn;
        Seat winner = kNoSeat;
        Tile winningTile;
        /// Meaningful for a win only.
        WinSource source = WinSource::SelfDraw;
        Seat from = kNoSeat;
        bool loose = false;
        bool lastLiveTile = false;
        eastl::array<OnlineResultSeat, kSeatCount> seats;
        SeatScores scores{};
        SeatScores deltas{};
    };

    /**
     * @brief The view of one seat of an online match, read into plain data: what a table draws and a client reads.
     *
     * There is no hidden state on a client, so this is not a `Match` or a `HandModel`; it reuses their value types. The tiles are those
     * of `MakeTableView` for `seat` plus the public table. A client reads `actions` (the server's list, in its order, each entry sent
     * back as it is), not `turn.yourTurn`.
     */
    struct OnlineTable {
        int format = 0;
        /// The room's setting: hints and the coach may be used at this table. A view without the key, or with a value that is not a
        /// boolean, says false (an old server, a malformed value) and is read all the same.
        bool hintsAllowed = false;
        SessionPhase phase = SessionPhase::Playing;
        Seat seat = kNoSeat;
        Wind seatWind = Wind::East;
        Wind roundWind = Wind::East;
        Seat east = kNoSeat;
        /// The own concealed tiles, as sent (sorted).
        eastl::vector<Tile> hand;
        OnlineTurn turn;
        eastl::array<OnlineSeat, kSeatCount> seats;
        /// The pile in the order discarded.
        eastl::vector<DiscardEntry> discards;
        /// The live tiles left.
        int wall = 0;
        int handsPlayed = 0;
        int handsTotal = 0;
        uint64_t version = 0;
        /// The seats that sent `next_hand` in the finished hand.
        eastl::array<bool, kSeatCount> ready{};
        eastl::vector<ControlAction> actions;
        /// Present exactly when `phase` is not `Playing`.
        std::optional<OnlineResult> result;
    };

    /// Why a view was not read: `Format` for a `format` that is missing or is not the number this client speaks, `Malformed` for the rest.
    CORE_ENUM(OnlineViewErrorKind, uint8_t, Format, Malformed)

    struct OnlineViewError {
        OnlineViewErrorKind kind = OnlineViewErrorKind::Malformed;
        /// Names the key, as in `seats[2].melds[0].kind: has to be a string`. For a screen's notice the kind is enough; the text is for the log.
        eastl::string text;
    };

    /**
     * @brief Reads the `view` and the `actions` of a `RoomClient` (untrusted JSON, read with the checked helpers) into @p out.
     *
     * @p out is touched only on success: the view is read into @p scratch, which is then swapped with @p out, so a bad view leaves the last
     * good table. Both tables keep the capacity of their lists from one read to the next, which is why a client keeps one `scratch` of its
     * own. Unknown keys are ignored (a newer server); `format` has to be `kOnlineViewFormat`. A seat outside 0..3, a negative count and a
     * tile that is not in the notation are errors.
     */
    [[nodiscard]] std::expected<void, OnlineViewError> ReadOnlineView(const Json& view, const Json& actions, OnlineTable& out, OnlineTable& scratch);

    /// Fills @p out with the hand actions among `table.actions` (the `next_hand` ready check is not one), in the server's order. Keeps the
    /// capacity of @p out. The `legal` list of a `TableView` of the viewer's display hand.
    void ListHandActions(const OnlineTable& table, eastl::vector<HandAction>& out);

    /// The same with a scratch of its own (allocates; for a caller that reads once).
    [[nodiscard]] std::expected<void, OnlineViewError> ReadOnlineView(const Json& view, const Json& actions, OnlineTable& out);

}  // namespace BECore::Mahjong
