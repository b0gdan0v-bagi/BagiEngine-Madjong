#pragma once

#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>

/**
 * A wall laid out tile by tile, so that a test knows what every seat holds and what comes next. Header only; the rules tests of
 * the hand (deal, turn, claims, kongs) build their walls with it.
 *
 * The tiles are written in the notation of `ParseTiles` ("1p 2p 3p Ew Rd F1"). Whatever a spec does not name is padded with unused
 * tiles of the full set, never more than four of a kind: a spec that asks for a fifth copy (or a bonus tile without `flowers`) fails
 * the test (`ADD_FAILURE`). Padding comes out in the order of the set (1p 1p 1p 1p 2p ...), so a test that relies on what a draw
 * brings, or on no claim being possible, names those tiles itself.
 */
namespace BECore::Mahjong::Tests {

    /**
     * @brief What a fixed wall holds, in the order it is taken.
     *
     * - `hands[seat]`: the 13 tiles of the seat **in the order the deal gives them** (four, four, four, one; see `DealHands`). A bonus
     *   tile written here lands in a deal slot and is replaced from the loose tiles, as in the real deal.
     * - `live`: the tiles drawn after the deal, in order (East's first draw is the first).
     * - `loose`: the two loose tiles, then the tiles that refill the reserve after each loose draw, in the order they join it. The
     *   refills are laid at the end of the live wall (the tile just before the dead wall is the first refill).
     * - `liveAfterDeal`: the live wall holds exactly this many tiles after the deal (the refills included), so that a drawn hand comes
     *   soon; the wall is then shorter than the ring. Each bonus tile dealt takes one refill from the live tail, so the live wall ends
     *   the deal with `liveAfterDeal - bonusTilesDealt`. It has to lie between what `live` and the refills need and what the ring
     *   leaves after the deal and the dead wall, otherwise the test fails once and the value is clamped. Unset: the wall is a whole
     *   ring.
     */
    struct FixedWallSpec {
        std::array<std::string, kSeatCount> hands;
        std::string live;
        std::string loose;
        std::optional<int> liveAfterDeal;
    };

    namespace FixedWallDetail {

        /// The tiles of a text; a bad text fails the test and gives no tiles.
        inline eastl::vector<Tile> Parse(const std::string& text, const char* what) {
            const std::optional<eastl::vector<Tile>> tiles = ParseTiles(text);
            if (!tiles.has_value()) {
                ADD_FAILURE() << "FixedWallSpec." << what << ": cannot read the tiles \"" << text << "\"";
                return {};
            }
            return *tiles;
        }

    }  // namespace FixedWallDetail

    /**
     * @brief The wall of `spec`: the loose reserve, the deal in the order of `DealHands` (from `east`), `live`, padding, the refills of
     *        the reserve, then the 14 tiles of the dead wall (the last 14 unused tiles of the set).
     *
     * The wall comes out through `Wall::FromOrder`, so `DealHands(wall, east, flowers)` gives each seat exactly its `hands[seat]`.
     */
    inline Wall MakeFixedWall(const FixedWallSpec& spec, bool flowers, Seat east = 0) {
        // How many copies of each kind are left to use.
        std::array<int, kTileIdCount> left{};
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile{id};
            left[id] = tile.IsBonus() ? (flowers ? 1 : 0) : kCopiesPerTile;
        }
        const auto take = [&left](Tile tile) {
            if (--left[tile.Id()] < 0) {
                ADD_FAILURE() << "FixedWallSpec uses more copies of " << ToString(tile) << " than the set has";
                left[tile.Id()] = 0;
            }
        };

        std::array<eastl::vector<Tile>, kSeatCount> hands;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            hands[seat] = FixedWallDetail::Parse(spec.hands[seat], "hands");
            EXPECT_EQ(hands[seat].size(), static_cast<size_t>(kDealtTiles)) << "FixedWallSpec.hands[" << seat << "] needs 13 tiles";
            for (const Tile tile : hands[seat]) {
                take(tile);
            }
        }
        const eastl::vector<Tile> live = FixedWallDetail::Parse(spec.live, "live");
        eastl::vector<Tile> loose = FixedWallDetail::Parse(spec.loose, "loose");
        for (const Tile tile : live) {
            take(tile);
        }
        for (const Tile tile : loose) {
            take(tile);
        }

        // Unused tiles in the order of the set: the front pads, the last 14 are the dead wall.
        eastl::vector<Tile> pool;
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            pool.insert(pool.end(), static_cast<size_t>(left[id]), Tile{id});
        }
        size_t nextPad = 0;
        const auto pad = [&pool, &nextPad]() {
            if (nextPad >= pool.size()) {
                ADD_FAILURE() << "FixedWallSpec: the set has no unused tiles left to pad with";
                return Tile{static_cast<uint8_t>(0)};
            }
            return pool[nextPad++];
        };

        eastl::vector<Tile> order;
        // The reserve; the tiles after the first two refill it from the live tail.
        eastl::vector<Tile> refills;
        for (size_t i = 0; i < loose.size(); ++i) {
            if (i < kLooseTiles) {
                order.push_back(loose[i]);
            } else {
                refills.push_back(loose[i]);
            }
        }
        while (order.size() < kLooseTiles) {
            order.push_back(pad());
        }

        // The deal, in the order DealHands takes the tiles: four at a time from East, three times, then one each.
        const auto dealt = [&](Seat seat, int from, int count) {
            for (int i = from; i < from + count; ++i) {
                const size_t index = static_cast<size_t>(i);
                order.push_back(index < hands[seat].size() ? hands[seat][index] : pad());
            }
        };
        for (int round = 0; round < kDealBlockRounds; ++round) {
            for (int step = 0; step < kSeatCount; ++step) {
                dealt((east + step) % kSeatCount, round * kDealBlock, kDealBlock);
            }
        }
        for (int step = 0; step < kSeatCount; ++step) {
            dealt((east + step) % kSeatCount, kDealBlockRounds * kDealBlock, 1);
        }

        // The live wall after the deal: `live`, padding, and the refills at the very end.
        const size_t ringSize = static_cast<size_t>(RingSize(StacksPerSide(flowers)));
        const size_t dealtSize = order.size();
        const size_t liveNeeded = live.size() + refills.size();
        const size_t liveAvailable = ringSize > dealtSize + kDeadWallTiles ? ringSize - dealtSize - kDeadWallTiles : 0;
        size_t liveWanted = liveAvailable;
        if (spec.liveAfterDeal.has_value()) {
            const int cut = *spec.liveAfterDeal;
            if (cut < 0 || static_cast<size_t>(cut) < liveNeeded || static_cast<size_t>(cut) > liveAvailable) {
                ADD_FAILURE() << "FixedWallSpec.liveAfterDeal " << cut << " is outside " << liveNeeded << ".." << liveAvailable
                              << " (`live` and the refills at least, the ring after the deal and the dead wall at most)";
                const size_t cutSize = cut < 0 ? 0 : static_cast<size_t>(cut);
                liveWanted = eastl::max(liveNeeded, eastl::min(cutSize, liveAvailable));
            } else {
                liveWanted = static_cast<size_t>(cut);
            }
        } else {
            EXPECT_GE(liveWanted, liveNeeded) << "FixedWallSpec: `live` and the refills do not fit the live wall";
        }
        for (const Tile tile : live) {
            order.push_back(tile);
        }
        const size_t padTo = dealtSize + eastl::max(liveWanted, liveNeeded) - refills.size();
        while (order.size() < padTo) {
            order.push_back(pad());
        }
        for (auto it = refills.rbegin(); it != refills.rend(); ++it) {
            order.push_back(*it);
        }

        // The dead wall: the last unused tiles.
        if (pool.size() - nextPad < kDeadWallTiles) {
            ADD_FAILURE() << "FixedWallSpec: the set has too few unused tiles for the dead wall";
        }
        const size_t deadBegin = pool.size() >= kDeadWallTiles ? pool.size() - kDeadWallTiles : 0;
        for (size_t i = deadBegin; i < pool.size(); ++i) {
            order.push_back(pool[i]);
        }
        return Wall::FromOrder(eastl::move(order));
    }

    /**
     * @brief A started hand on the fixed wall of `spec`: the winds follow counter-clockwise from `east` (that seat is the dealer and
     *        takes the first tile), `flowers` is the setting of the hand (it also decides the size of the wall).
     */
    inline HandModel MakeModel(const FixedWallSpec& spec, Seat east = 0, Wind roundWind = Wind::East, bool flowers = false) {
        MahjongSettings settings;
        settings.flowers = flowers;
        HandModel model;
        const HandStatus begun = model.Begin(settings, MakeFixedWall(spec, flowers, east), MakeSeatWinds(east), roundWind);
        EXPECT_TRUE(begun.has_value()) << "MakeModel: Begin refused the hand";
        return model;
    }

}  // namespace BECore::Mahjong::Tests
