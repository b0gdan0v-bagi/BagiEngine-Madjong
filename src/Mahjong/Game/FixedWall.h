#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong {

    /**
     * @brief A deal laid out tile by tile, so that a caller knows what every seat holds and what comes next: the rules tests build
     *        their walls with it, the control API's `set_state` and the match log's `state` start a match from it.
     *
     * - `hands[seat]`: the 13 tiles of the seat **in the order the deal gives them** (four, four, four, one; see `DealHands`). A bonus
     *   tile written here lands in a deal slot and is replaced from the loose tiles, as in the real deal.
     * - `live`: the tiles drawn after the deal, in order (East's first draw is the first).
     * - `loose`: the two loose tiles, then the tiles that refill the reserve after each loose draw, in the order they join it. The
     *   refills are laid at the end of the live wall (the tile just before the dead wall is the first refill).
     * - `east`: the dealer; the deal starts from this seat.
     * - `liveAfterDeal`: the live wall holds exactly this many tiles after the deal (the refills included), so that a drawn hand comes
     *   soon; the wall is then shorter than the ring. Each bonus tile dealt takes one refill from the live tail, so the live wall ends
     *   the deal with `liveAfterDeal - bonusTilesDealt`. It has to lie between what `live` and the refills need and what the ring
     *   leaves after the deal and the dead wall. Unset: the wall is a whole ring.
     *
     * Whatever the deal does not name is padded with unused tiles of the full set, never more than four of a kind (a bonus tile only
     * with `flowers`), in the order of the set (1p 1p 1p 1p 2p ...), so a caller that relies on what a draw brings, or on no claim
     * being possible, names those tiles itself.
     */
    struct FixedDeal {
        Array<Vector<Tile>, kSeatCount> hands;
        Vector<Tile> live;
        Vector<Tile> loose;
        Seat east = 0;
        Optional<int> liveAfterDeal;

        bool operator==(const FixedDeal&) const = default;
    };

    /// A match position to start from: the first hand's deal, the hands already counted (the round wind follows) and the scores.
    struct FixedStart {
        FixedDeal deal;
        int handsPlayed = 0;
        SeatScores scores{};

        bool operator==(const FixedStart&) const = default;
    };

    /// What is wrong with a deal: the member as a path inside it (`hands[2]`, `live`, `liveAfterDeal`, empty for the deal as a whole)
    /// and what is wrong.
    struct FixedDealProblem {
        String member;
        String what;
    };

    /// A wall that is always built, with the list of what was wrong with the deal (in the order found).
    struct FixedWallBuild {
        Wall wall;
        Vector<FixedDealProblem> problems;
    };

    /**
     * @brief The wall of `deal`: the loose reserve, the deal in the order of `DealHands` (from `deal.east`), `live`, padding, the refills
     *        of the reserve, then the 14 tiles of the dead wall (the last 14 unused tiles of the set).
     *
     * Lenient: a wall comes out whatever is wrong (a fifth copy is dropped from the count, a hand that is not 13 tiles is padded or
     * cut, an `east` that is no seat is 0, a `liveAfterDeal` outside its range is clamped), and every defect is in `problems`. The
     * wall comes out through `Wall::FromOrder`, so `DealHands(wall, east, flowers)` gives each seat exactly its `hands[seat]` when
     * there are none.
     */
    [[nodiscard]] FixedWallBuild BuildFixedWall(const FixedDeal& deal, bool flowers);

    /// The same wall, or the first problem as a text that names the member (`deal.hands[2]: needs 13 tiles, has 12`).
    [[nodiscard]] std::expected<Wall, String> TryBuildFixedWall(const FixedDeal& deal, bool flowers);

}  // namespace BECore::Mahjong
