#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /// What everybody sees of a seat. A concealed kong lies face down, so it is only counted, not named.
    struct PublicSeat {
        /// The melds with `!concealed`: chows, pungs, open and upgraded kongs.
        eastl::vector<Meld> openMelds;
        int concealedKongs = 0;
        /// The flowers and seasons laid open; `bonusCount` is their number.
        eastl::vector<Tile> bonus;
        int bonusCount = 0;
        int discardCount = 0;
        bool callingDeclared = false;
    };

    /**
     * @brief Everything a seat may know about the hand: its own tiles and the public table. Nothing else.
     *
     * The computer player (and the control API) never get the `HandModel`: a view is made by `MakeTableView` from the own hand of one
     * seat and from what lies open, so "no peeking at the others' tiles" holds by construction. The view holds neither the live wall
     * nor any other concealed hand; two models that differ only there give equal views.
     *
     * `visible` counts, per kind, the copies the seat can see: its own concealed tiles and melds (the four of a concealed kong
     * too), the open melds of the others, the pile (`Discards()`, the pending discard included) and, in `Phase::RobKong`, the tile of
     * the kong that is being robbed (the owner holds it in hand, everybody else sees it). Bonus tiles are not counted. What is not
     * visible is unseen: `UnseenCopies` (`ComputerTurn.h`).
     *
     * A default view is the view of no seat (`MakeTableView` for a number that is no seat): nothing in it, no actions.
     */
    struct TableView {
        Seat seat = kNoSeat;
        Phase phase = Phase::Finished;
        /// As in `HandModel`: the seat to act in `Phase::Discard`, the discarder in `Phase::Claim`, the kong's owner in `Phase::RobKong`.
        Seat currentSeat = kNoSeat;
        /// The tile a window is about (`HandModel::PendingTile`); invalid outside a window.
        Tile pendingTile;
        Wind seatWind = Wind::East;
        Wind roundWind = Wind::East;
        /// The whole hand of `seat`.
        PlayerHand own;
        eastl::array<PublicSeat, kSeatCount> seats;
        TileCounts visible{};
        /// The tiles a regular draw can still take (`Wall::LiveCount`).
        int liveCount = 0;
        /// `HandModel::LegalActions(seat)`: the list the player chooses from.
        eastl::vector<HandAction> legal;
    };

    /// The view of `seat` on `model`; empty (`TableView{}`) for a number that is no seat.
    [[nodiscard]] TableView MakeTableView(const HandModel& model, Seat seat);

}  // namespace BECore::Mahjong
