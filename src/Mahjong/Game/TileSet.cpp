#include "TileSet.h"

namespace BECore::Mahjong {

    eastl::vector<Tile> BuildTileSet(bool flowers) {
        eastl::vector<Tile> tiles;
        tiles.reserve(flowers ? kTileSetSizeWithBonus : kTileSetSize);
        for (uint8_t id = 0; id < kKindCount; ++id) {
            for (uint8_t copy = 0; copy < kCopiesPerTile; ++copy) {
                tiles.push_back(Tile{id});
            }
        }
        if (flowers) {
            for (uint8_t id = kFirstFlowerId; id < kTileIdCount; ++id) {
                tiles.push_back(Tile{id});
            }
        }
        return tiles;
    }

}  // namespace BECore::Mahjong
