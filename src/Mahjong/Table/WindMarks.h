#pragma once

#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /**
     * The wind tiles of the viewer's hand that score for it: the tile of the own seat wind and the tile of the round wind (a pung of either
     * doubles, a pair of either scores; the same tiles are value tiles of the computer player, `IsValueTile`). Plain C++, no scene: only the
     * two winds of the viewer's view (`TableView::seatWind`, `roundWind`) go in, so it knows no more than the viewer sees. The table draws
     * a bar along the top edge of such a tile (`kSeatWindMark`, `kRoundWindMark`), not tied to the `Hints` switch.
     */
    CORE_ENUM(WindMark, uint8_t, None, Seat, Round, Both)

    /// `Seat` for the wind tile of `seatWind`, `Round` for that of `roundWind`, `Both` when they are the same wind, `None` for every other
    /// tile (a suit tile, a dragon, another wind, a bonus tile, an invalid tile).
    [[nodiscard]] WindMark WindMarkOf(Tile tile, Wind seatWind, Wind roundWind) noexcept;

}  // namespace BECore::Mahjong
