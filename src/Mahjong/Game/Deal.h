#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong {

    inline constexpr int kDealtTiles = 13;
    /// The deal goes round the table taking this many tiles at a time, until every seat has `kDealBlockRounds * kDealBlock` tiles.
    inline constexpr int kDealBlock = 4;
    inline constexpr int kDealBlockRounds = 3;
    static_assert(kDealtTiles == kDealBlockRounds * kDealBlock + 1);

    /**
     * @brief Deals 13 tiles to each seat from the live wall.
     *
     * East takes first and the deal goes counter-clockwise: rounds of `kDealBlock` tiles, then one tile each. A bonus tile (when
     * `flowers` is set) is laid open in the seat's `bonus` and replaced by `wall.DrawLoose()` until it is not a bonus tile. The
     * hands are indexed by seat.
     *
     * A wall too short to deal from (tests only) ends the deal early. East's first draw is not part of the deal.
     */
    [[nodiscard]] Array<PlayerHand, kSeatCount> DealHands(Wall& wall, Seat east, bool flowers);

}  // namespace BECore::Mahjong
