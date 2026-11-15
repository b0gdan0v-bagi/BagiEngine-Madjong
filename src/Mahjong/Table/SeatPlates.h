#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// The seat at the bottom of the table: the viewer, else the person that waits for the cover to go, else the first person, else seat 0.
    [[nodiscard]] Seat BottomSeat(const TableSession& session);

    /// The seat whose turn it is: the one that discards, the discarder (or the kong's owner) while the others decide; `kNoSeat` unless
    /// the session is `Playing`. Not the seat that has to act (the first one with a claim): that would tell a person that this computer
    /// could claim.
    [[nodiscard]] Seat TurnSeat(const TableSession& session, const HandModel& hand);

    /// What the plate of one seat says. The wind, the points and the dealer are the centre panel's (`MakeCenterPanel`); the table paints
    /// the dealer bar of the plate from the winds of the hand. Public data only, so it is the same under the hot-seat cover, for a
    /// spectator and at the end of a hand.
    struct SeatPlateText {
        /// The seat at this screen position.
        Seat seat = 0;
        /// `SeatLabel` without the wind: the number and "You" for the one person of a local table; in a room the player's name (a computer
        /// keeps its number) and "You" on the viewer's seat.
        String name;
        /// A person or a computer (`session.KindOf`): the emblem of the plate.
        SeatKind kind = SeatKind::Person;
        /// The level of the seat (`session.LevelOf`); a person has one too, which the emblem ignores.
        ComputerLevel level = kDefaultLevel;
        /// It is this seat's turn (`TurnSeat`).
        bool turn = false;
        /// The seat has declared its calling hand (`callingDeclared`, public). The flag stays until the next deal, so at a hand's end it
        /// still tells who had declared.
        bool calling = false;
    };

    /// Design units inside the plate (`kSeatPlateW` x `kSeatPlateH`): the emblem's ring box is centred at the top below the dealer bar,
    /// the name box under it; both sit on the `kGridUnit` grid and do not touch each other's rows.
    inline constexpr float kEmblemBoxSize = 44.0f;
    inline constexpr float kEmblemBoxTop = 8.0f;
    inline constexpr float kNameBoxWidth = 136.0f;
    inline constexpr float kNameBoxHeight = 24.0f;
    inline constexpr float kNameBoxTop = 52.0f;

    /// The square the emblem and its two rings are drawn in, for the seat plate `plate` (view units); `unit` is the view units per design
    /// unit (`LayoutScale`). Always inside the plate and clear of `NameBox`.
    [[nodiscard]] Rect EmblemBox(const Rect& plate, float unit) noexcept;

    /// The box of the name label of the seat plate `plate`: under the emblem, centred.
    [[nodiscard]] Rect NameBox(const Rect& plate, float unit) noexcept;

    /// The four plates by screen position (0 bottom, 1 right, 2 top, 3 left; `SeatAtPosition(BottomSeat(session), position)` is the seat
    /// at each).
    [[nodiscard]] Array<SeatPlateText, kSeatCount> MakeSeatPlates(const UIStrings& strings, const TableSession& session, const Match& match);

}  // namespace BECore::Mahjong
