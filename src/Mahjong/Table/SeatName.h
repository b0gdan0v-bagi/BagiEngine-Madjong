#pragma once

#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /// How a seat is named on screen: "Seat 2 (South)" (seats count from 1 for a person; `wind` is the seat's wind in the hand).
    [[nodiscard]] inline eastl::string SeatName(Seat seat, Wind wind) {
        return Format("Seat {} ({})", seat + 1, EnumUtils<Wind>::ToString(wind));
    }

}  // namespace BECore::Mahjong
