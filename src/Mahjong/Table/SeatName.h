#pragma once

#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// How a seat is named on screen: "Seat 2 (South)" (seats count from 1 for a person; `wind` is the seat's wind in the hand). The words are the keys `seat.name` and `wind.*`.
    [[nodiscard]] inline eastl::string SeatName(const UIStrings& strings, Seat seat, Wind wind) {
        const eastl::string number = Format("{}", seat + 1);
        const eastl::string windName = WindName(strings, wind);
        return Tr(strings, "seat.name", {{"n", number}, {"wind", windName}});
    }

    /// Whether `seat` is "you": the person at a table where exactly one person plays (with two or more nobody is, with none nobody is).
    [[nodiscard]] inline bool IsYou(Seat seat, const SeatSetup& seats) {
        int people = 0;
        for (const SeatKind kind : seats) {
            people += kind == SeatKind::Person ? 1 : 0;
        }
        return people == 1 && IsSeat(seat) && seats[seat] == SeatKind::Person;
    }

    /// `name`, and the word `seat.you` after it when `seat` is "you".
    [[nodiscard]] inline eastl::string MarkYou(const UIStrings& strings, eastl::string name, Seat seat, const SeatSetup& seats) {
        if (IsYou(seat, seats)) {
            name += Tr(strings, "seat.you");
        }
        return name;
    }

    /// `SeatName`, and the word `seat.you` after it for "you".
    [[nodiscard]] inline eastl::string SeatLabel(const UIStrings& strings, Seat seat, Wind wind, const SeatSetup& seats) {
        return MarkYou(strings, SeatName(strings, seat, wind), seat, seats);
    }

    /// "Seat 2" without the wind (where the wind would name the wrong deal), and the word `seat.you` for "you".
    [[nodiscard]] inline eastl::string SeatLabel(const UIStrings& strings, Seat seat, const SeatSetup& seats) {
        const eastl::string number = Format("{}", seat + 1);
        return MarkYou(strings, Tr(strings, "seat.short", {{"n", number}}), seat, seats);
    }

}  // namespace BECore::Mahjong
