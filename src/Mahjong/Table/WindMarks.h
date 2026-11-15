#pragma once

#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /**
     * The wind tiles of the viewer's hand that score for it: the own seat wind and the round wind (the value tiles of the computer
     * player too, `IsValueTile`). Plain C++, no scene: only the two winds of the viewer's view (`TableView::seatWind`, `roundWind`) go
     * in, so it knows no more than the viewer sees. Drawn (`kSeatWindMark`, `kRoundWindMark`) whatever the `Hints` switch says.
     */
    CORE_ENUM(WindMark, uint8_t, None, Seat, Round, Both)

    /// `Seat` for the wind tile of `seatWind`, `Round` for that of `roundWind`, `Both` when they are the same wind, `None` for every
    /// other tile.
    [[nodiscard]] WindMark WindMarkOf(Tile tile, Wind seatWind, Wind roundWind) noexcept;

}  // namespace BECore::Mahjong
