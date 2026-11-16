#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/MahjongGameControl.h>

namespace BECore::Mahjong {

    /// What a table of the rules can hold, which is what a view may claim (`ReadOnlineView` refuses more, `MakeDisplayState` never
    /// allocates more): a hand has at most a winning hand's tiles and four sets, the wall and the pile at most the set without bonus
    /// tiles (a room plays without them), a seat at most all bonus tiles.
    inline constexpr int kViewMaxConcealed = kWinningHandTiles;
    inline constexpr int kViewMaxMelds = kSetsInHand;
    inline constexpr int kViewMaxBonus = kBonusCount;
    inline constexpr int kViewMaxPile = kKindCount * kCopiesPerTile;

    /// One entry of `seats`. The own seat lists its concealed kongs among `melds` (`concealed`), the others only count them.
    struct OnlineSeat {
        Wind wind = Wind::East;
        int score = 0;
        int concealedCount = 0;
        /// What JSON gave as null in a meld is `kNoSeat` and an invalid tile.
        Vector<Meld> melds;
        int concealedKongs = 0;
        Vector<Tile> bonus;
        int discardCount = 0;
        bool calling = false;
    };

    /// The turn of the table, as in `TableView`.
    struct OnlineTurn {
        Phase phase = Phase::Finished;
        Seat current = kNoSeat;
        Tile pendingTile;
        /// The server's flag, false in `hand_over` while `actions` lists `next_hand`: a client reads `OnlineTable::actions`, not this
        /// (`src/Mahjong/Online/CLAUDE.md`).
        bool yourTurn = false;
    };

    /// All four hands are public once the hand is over.
    struct OnlineResultSeat {
        Vector<Tile> concealed;
        Vector<Meld> melds;
        Vector<Tile> bonus;
    };

    /// The breakdown lines are not read here: they stay in the raw view of the client.
    struct OnlineResult {
        ResultKind kind = ResultKind::Drawn;
        Seat winner = kNoSeat;
        Tile winningTile;
        /// Meaningful for a win only.
        WinSource source = WinSource::SelfDraw;
        Seat from = kNoSeat;
        bool loose = false;
        bool lastLiveTile = false;
        Array<OnlineResultSeat, kSeatCount> seats;
        SeatScores scores{};
        SeatScores deltas{};
    };

    /**
     * @brief The view of one seat of an online match as plain data, reusing the value types of `Match`: a client has no hidden state to
     *        play. Only the seat's own hand and the public table are in it (no peeking).
     *
     * A client reads `actions` (the server's list, each entry sent back as it is), not `turn.yourTurn`.
     */
    struct OnlineTable {
        int format = 0;
        /// The room's setting. Additive: a view without the key, or with a value that is not a boolean, says false and is read all the same.
        bool hintsAllowed = false;
        SessionPhase phase = SessionPhase::Playing;
        Seat seat = kNoSeat;
        Wind seatWind = Wind::East;
        Wind roundWind = Wind::East;
        Seat east = kNoSeat;
        /// The own concealed tiles, as sent.
        Vector<Tile> hand;
        OnlineTurn turn;
        Array<OnlineSeat, kSeatCount> seats;
        Vector<DiscardEntry> discards;
        int wall = 0;
        int handsPlayed = 0;
        int handsTotal = 0;
        uint64_t version = 0;
        /// Who sent `next_hand` in the finished hand.
        Array<bool, kSeatCount> ready{};
        Vector<ControlAction> actions;
        /// Present exactly when `phase` is not `Playing`.
        Optional<OnlineResult> result;
    };

    /// `Format`: `format` is missing or not the number this client speaks (reconnecting would not help); `Malformed`: anything else.
    CORE_ENUM(OnlineViewErrorKind, uint8_t, Format, Malformed)

    struct OnlineViewError {
        OnlineViewErrorKind kind = OnlineViewErrorKind::Malformed;
        /// Names the key (`seats[2].melds[0].kind: has to be a string`); for the log, a notice needs the kind only.
        String text;
    };

    /**
     * @brief Reads the `view` and the `actions` of a `RoomClient` (untrusted JSON) into @p out.
     *
     * @p out is touched only on success: the view is read into @p scratch and swapped in, so a bad view leaves the last good table. A
     * client keeps one `scratch` so that both tables keep the capacity of their lists. Unknown keys are ignored (a newer server);
     * `format` has to be `kOnlineViewFormat`.
     */
    [[nodiscard]] std::expected<void, OnlineViewError> ReadOnlineView(const Json& view, const Json& actions, OnlineTable& out, OnlineTable& scratch);

    /// Allocates: for a caller that reads once.
    [[nodiscard]] std::expected<void, OnlineViewError> ReadOnlineView(const Json& view, const Json& actions, OnlineTable& out);

    /// The hand actions among `table.actions` in the server's order, without the `next_hand` ready check: the `legal` list of the viewer's
    /// `TableView`.
    void ListHandActions(const OnlineTable& table, Vector<HandAction>& out);

}  // namespace BECore::Mahjong
