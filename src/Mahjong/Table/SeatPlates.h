#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// The seat at the bottom of the table: the viewer, else the person that waits for the cover to go, else the first person, else seat 0.
    [[nodiscard]] Seat BottomSeat(const TableSession& session);

    /// The seat whose turn it is on the table: the one that discards, the discarder (or the kong's owner) while the others decide;
    /// `kNoSeat` unless the session is `Playing`. Not the seat that has to act: that one is the first seat with a claim in a window,
    /// which would tell a person that this computer could claim.
    [[nodiscard]] Seat TurnSeat(const TableSession& session, const HandModel& hand);

    /// What the plate of one seat says. Public data only, so it is the same under the hot-seat cover, for a spectator and at the end of
    /// a hand.
    struct SeatPlateText {
        /// The seat at this screen position.
        Seat seat = 0;
        /// "Seat 2" and, for the person at a table with exactly one person, the word "You" (`SeatLabel` without the wind).
        eastl::string name;
        /// The seat's wind in the hand shown ("South"; the last hand once the match is over).
        eastl::string wind;
        /// The seat's points in `Match::Scores()`.
        int score = 0;
        /// The seat holds East in the hand shown (`Match::East()` while the match goes on; once it is over `Match::East()` is the East of
        /// the hand that would come next, which no plate shows, so the dealer is read from the winds of the hand like the wind word).
        bool dealer = false;
        /// It is this seat's turn (`TurnSeat`).
        bool turn = false;
    };

    /// The four plates by screen position (0 bottom, 1 right, 2 top, 3 left; `SeatAtPosition(BottomSeat(session), position)` is the seat
    /// at each), in the current language of `strings`.
    [[nodiscard]] eastl::array<SeatPlateText, kSeatCount> MakeSeatPlates(const UIStrings& strings, const TableSession& session, const Match& match);

}  // namespace BECore::Mahjong
