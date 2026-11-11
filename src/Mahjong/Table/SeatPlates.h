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

    /// What the plate of one seat says: its name, what sits there, whether it is that seat's turn and whether its hand is declared. The wind, the
    /// points and the dealer are the centre panel's (`MakeCenterPanel`); the table paints the dealer bar of the plate from the winds of the hand.
    /// Public data only, so it is the same under the hot-seat cover, for a spectator and at the end of a hand.
    struct SeatPlateText {
        /// The seat at this screen position.
        Seat seat = 0;
        /// "Seat 2" and, for the person at a table with exactly one person, the word "You" (`SeatLabel` without the wind).
        eastl::string name;
        /// A person or a computer (`session.KindOf`): the emblem of the plate.
        SeatKind kind = SeatKind::Person;
        /// The level of the seat (`session.LevelOf`); a person has one too, which the emblem ignores.
        ComputerLevel level = kDefaultLevel;
        /// It is this seat's turn (`TurnSeat`).
        bool turn = false;
        /// The seat has declared its calling hand (`callingDeclared`, public like the frame round the hand). The flag stays until the hand is
        /// dealt again, so at a hand's end it still tells who had declared.
        bool calling = false;
    };

    /// The plate is `kSeatPlateW` x `kSeatPlateH` units. The ring box of the emblem lies at its top, centred, `kEmblemBoxTop` in from the top edge (below the
    /// dealer bar), the name box under it; both are on the grid of `kGridUnit` in height and do not touch each other's rows.
    inline constexpr float kEmblemBoxSize = 44.0f;
    inline constexpr float kEmblemBoxTop = 8.0f;
    inline constexpr float kNameBoxWidth = 136.0f;
    inline constexpr float kNameBoxHeight = 24.0f;
    inline constexpr float kNameBoxTop = 52.0f;

    /// The ring box of a seat plate `plate` (a rectangle of the table, in view units), `unit` the view units per design unit (`LayoutScale`): the
    /// square the emblem and its two rings are drawn in. Always inside the plate and clear of `NameBox`.
    [[nodiscard]] Rect EmblemBox(const Rect& plate, float unit) noexcept;

    /// The box of the name label of the seat plate `plate`: under the emblem, centred.
    [[nodiscard]] Rect NameBox(const Rect& plate, float unit) noexcept;

    /// The four plates by screen position (0 bottom, 1 right, 2 top, 3 left; `SeatAtPosition(BottomSeat(session), position)` is the seat
    /// at each), in the current language of `strings`.
    [[nodiscard]] eastl::array<SeatPlateText, kSeatCount> MakeSeatPlates(const UIStrings& strings, const TableSession& session, const Match& match);

}  // namespace BECore::Mahjong
