#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// The number of the hand inside its round, from 1 (a round is a quarter of the match's hands). It follows `Match::HandsPlayed()`, so
    /// a hand that does not count (a drawn one that is replayed) keeps the number; once the match is over it stays on the last hand's.
    [[nodiscard]] int HandNumberInRound(const Match& match);

    /// What one side of the centre panel says: the seat that sits there and its wind letter, points, dealer mark and light.
    struct CenterPanelSeat {
        /// The seat at this screen position.
        Seat seat = 0;
        /// The seat's wind in the hand shown, one letter ("E"; the last hand once the match is over).
        String wind;
        /// The seat's points in `Match::Scores()`.
        int score = 0;
        /// The seat holds East in the hand shown (read from the winds of the hand like the plates do: after the last hand `Match::East()` is the next one's).
        bool dealer = false;
        /// It is this seat's turn (`TurnSeat`).
        bool turn = false;
    };

    /// What the centre panel inside the wall ring says. Public data only, so it is the same under the hot-seat cover, for a spectator and
    /// at the end of a hand.
    struct CenterPanelText {
        /// The round wind and the number of the hand in it ("East 1").
        String round;
        /// The number of the hand in its round (`HandNumberInRound`).
        int handInRound = 1;
        /// The tiles left in the live wall.
        int wall = 0;
        /// The sentence that names `wall` ("Wall 83").
        String wallText;
        /// By screen position (0 bottom, 1 right, 2 top, 3 left; `SeatAtPosition(BottomSeat(session), position)` is the seat at each).
        Array<CenterPanelSeat, kSeatCount> seats;
    };

    [[nodiscard]] CenterPanelText MakeCenterPanel(const UIStrings& strings, const TableSession& session, const Match& match);

    /// The cells of the panel as shares of its rectangle (the node `CenterPanel`: the top and bottom cells span the width, a side cell lies
    /// between them), so they follow the view fit.
    inline constexpr float kPanelRowShare = 32.0f / 140.0f;
    inline constexpr float kPanelSideShare = 108.0f / 336.0f;

    /// The light of the turn: the bar `width` thick along the whole edge of `panel` that faces the screen `position` (0 bottom, 1 right,
    /// 2 top, 3 left).
    [[nodiscard]] Rect PanelTurnLight(const Rect& panel, int position, float width);

    /// The mark of the dealer: the bar `thickness` thick on the inner boundary of the cell at the screen `position`, so it covers no word
    /// of the cell.
    [[nodiscard]] Rect PanelDealerMark(const Rect& panel, int position, float thickness);

}  // namespace BECore::Mahjong
