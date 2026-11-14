#pragma once

#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    inline constexpr size_t kTileSetSize = static_cast<size_t>(kKindCount) * kCopiesPerTile;
    inline constexpr size_t kTileSetSizeWithBonus = kTileSetSize + kBonusCount;

    /**
     * @brief The full set in a fixed order: the four copies of each playable kind in id order, then, when `flowers` is set, the bonus
     *        tiles in id order. The order is the input of the seeded shuffle, so it is part of what makes a seed give the same game;
     *        do not change it.
     */
    [[nodiscard]] Vector<Tile> BuildTileSet(bool flowers);

}  // namespace BECore::Mahjong
