#pragma once

#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /// Tiles in the set without bonus tiles (34 kinds x 4) and with them (plus the 8 flowers and seasons).
    inline constexpr size_t kTileSetSize = static_cast<size_t>(kKindCount) * kCopiesPerTile;
    inline constexpr size_t kTileSetSizeWithBonus = kTileSetSize + kBonusCount;

    /**
     * @brief The full set in a fixed order: the four copies of each playable kind in id order (1p 1p 1p 1p 2p ...), then, when `flowers` is
     *        set, the eight bonus tiles in id order. 136 tiles, or 144 with flowers. The order is the input of the seeded shuffle, so it
     *        is part of what makes a seed give the same game; do not change it.
     */
    [[nodiscard]] eastl::vector<Tile> BuildTileSet(bool flowers);

}  // namespace BECore::Mahjong
