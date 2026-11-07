#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong {

    /// Tiles each seat holds after the deal.
    inline constexpr int kDealtTiles = 13;
    /// The deal goes round the table taking this many tiles at a time, until every seat has `kDealtBlockRounds * kDealBlock` tiles.
    inline constexpr int kDealBlock = 4;
    inline constexpr int kDealBlockRounds = 3;

    /**
     * @brief Deals 13 tiles to each seat from the live wall.
     *
     * East takes first and the deal goes counter-clockwise: three rounds of 4 tiles at a time (12 tiles), then one tile each. Every
     * tile comes from `wall.Draw()`. A bonus tile (when `flowers` is set) is laid open in the seat's `bonus` and replaced by
     * `wall.DrawLoose()`, again and again while the replacement is a bonus tile, so each seat ends with 13 concealed tiles. Without
     * `flowers` a bonus tile is treated like any other tile (the set has none). The hands are indexed by seat.
     *
     * A wall too short to deal from (tests only) ends the deal early: the seats hold what they got. East's first draw is not part of
     * the deal.
     */
    [[nodiscard]] eastl::array<PlayerHand, kSeatCount> DealHands(Wall& wall, Seat east, bool flowers);

}  // namespace BECore::Mahjong
