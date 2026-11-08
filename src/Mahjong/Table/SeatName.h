#pragma once

#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/TableSession.h>

namespace BECore::Mahjong {

    /// How a seat is named on screen: "Seat 2 (South)" (seats count from 1 for a person; `wind` is the seat's wind in the hand).
    [[nodiscard]] inline eastl::string SeatName(Seat seat, Wind wind) {
        return Format("Seat {} ({})", seat + 1, EnumUtils<Wind>::ToString(wind));
    }

    /// The word that marks "you" after a seat's name.
    inline constexpr eastl::string_view kYouMark = " You";

    /// Whether `seat` is "you": the person at a table where exactly one person plays (with two or more nobody is, with none nobody is).
    [[nodiscard]] inline bool IsYou(Seat seat, const SeatSetup& seats) {
        int people = 0;
        for (const SeatKind kind : seats) {
            people += kind == SeatKind::Person ? 1 : 0;
        }
        return people == 1 && IsSeat(seat) && seats[seat] == SeatKind::Person;
    }

    /// `SeatName`, and `kYouMark` after it for "you".
    [[nodiscard]] inline eastl::string SeatLabel(Seat seat, Wind wind, const SeatSetup& seats) {
        eastl::string name = SeatName(seat, wind);
        if (IsYou(seat, seats)) {
            name.append(kYouMark.data(), kYouMark.size());
        }
        return name;
    }

}  // namespace BECore::Mahjong
