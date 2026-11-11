#include "WindMarks.h"

namespace BECore::Mahjong {

    WindMark WindMarkOf(Tile tile, Wind seatWind, Wind roundWind) noexcept {
        if (!tile.IsWind()) {
            return WindMark::None;
        }
        const bool seat = tile == Tile::FromWind(static_cast<int>(seatWind));
        const bool round = tile == Tile::FromWind(static_cast<int>(roundWind));
        if (seat && round) {
            return WindMark::Both;
        }
        return seat ? WindMark::Seat : (round ? WindMark::Round : WindMark::None);
    }

}  // namespace BECore::Mahjong
