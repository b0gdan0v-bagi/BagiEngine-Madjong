#include "Wall.h"

#include <Mahjong/Game/TileSet.h>

namespace BECore::Mahjong {

    Wall Wall::FromOrder(eastl::vector<Tile> order) {
        Wall wall;
        const size_t loose = eastl::min(kLooseTiles, order.size());
        const size_t dead = eastl::min(kDeadWallTiles, order.size() - loose);
        const size_t liveEnd = order.size() - dead;
        wall._loose.assign(order.begin(), order.begin() + loose);
        wall._dead.assign(order.begin() + liveEnd, order.end());
        wall._live.assign(order.begin() + loose, order.begin() + liveEnd);
        wall._liveBegin = 0;
        wall._liveEnd = wall._live.size();
        return wall;
    }

    Wall Wall::FromParts(eastl::vector<Tile> loose, eastl::vector<Tile> live, eastl::vector<Tile> dead) {
        Wall wall;
        wall._loose = eastl::move(loose);
        wall._live = eastl::move(live);
        wall._dead = eastl::move(dead);
        wall._liveBegin = 0;
        wall._liveEnd = wall._live.size();
        return wall;
    }

    std::optional<Tile> Wall::Draw() {
        if (LiveCount() == 0) {
            return std::nullopt;
        }
        return _live[_liveBegin++];
    }

    std::optional<Tile> Wall::DrawLoose() {
        if (_loose.empty()) {
            return std::nullopt;
        }
        const Tile tile = _loose.front();
        _loose.erase(_loose.begin());
        if (LiveCount() > 0) {
            _loose.push_back(_live[--_liveEnd]);
        }
        return tile;
    }

    Wall MakeWall(MahjongRng& rng, const MahjongSettings& settings, Seat east) {
        eastl::vector<Tile> ring = BuildTileSet(settings.flowers);
        rng.Shuffle(ring);

        WallBreak wallBreak;
        wallBreak.eastDice = rng.Roll2d6();
        wallBreak.ownerDice = rng.Roll2d6();
        wallBreak.seat = (east + BreakSide(wallBreak.eastDice)) % kSeatCount;
        wallBreak.index = BreakIndex(wallBreak.eastDice, wallBreak.ownerDice, StacksPerSide(settings.flowers));

        // The draw order is the ring read from the break onwards, round the end of the ring.
        eastl::rotate(ring.begin(), ring.begin() + wallBreak.index, ring.end());
        Wall wall = Wall::FromOrder(eastl::move(ring));
        wall._break = wallBreak;
        return wall;
    }

}  // namespace BECore::Mahjong
