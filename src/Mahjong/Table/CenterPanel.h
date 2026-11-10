#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// The number of the hand inside its round, from 1: `East 1` .. `East 4` for a match of 16 hands (`handsPerMatch / 4` hands a wind), `East 1` ..
    /// `East 2` for 8. It follows `Match::HandsPlayed()`, so a hand that does not count (a drawn one that is replayed) keeps the number; once the match is
    /// over it stays on the last hand's number.
    [[nodiscard]] int HandNumberInRound(const Match& match);

    /// What one side of the centre panel says: the seat that sits there and its wind letter, points, dealer mark and light.
    struct CenterPanelSeat {
        /// The seat at this screen position.
        Seat seat = 0;
        /// The seat's wind in the hand shown, one letter ("E"; the last hand once the match is over).
        eastl::string wind;
        /// The seat's points in `Match::Scores()`.
        int score = 0;
        /// The seat holds East in the hand shown (read from the winds of the hand like the plates do: after the last hand `Match::East()` is the next one's).
        bool dealer = false;
        /// It is this seat's turn (`TurnSeat`).
        bool turn = false;
    };

    /// What the centre panel inside the wall ring says. Public data only, so it is the same under the hot-seat cover, for a spectator and at the end
    /// of a hand.
    struct CenterPanelText {
        /// The round wind and the number of the hand in it ("East 1": one key with placeholders).
        eastl::string round;
        /// The number of the hand in its round (`HandNumberInRound`).
        int handInRound = 1;
        /// The tiles left in the live wall.
        int wall = 0;
        /// The sentence that names `wall` ("Wall 83").
        eastl::string wallText;
        /// By screen position (0 bottom, 1 right, 2 top, 3 left; `SeatAtPosition(BottomSeat(session), position)` is the seat at each).
        eastl::array<CenterPanelSeat, kSeatCount> seats;
    };

    /// The panel in the current language of `strings`.
    [[nodiscard]] CenterPanelText MakeCenterPanel(const UIStrings& strings, const TableSession& session, const Match& match);

}  // namespace BECore::Mahjong
