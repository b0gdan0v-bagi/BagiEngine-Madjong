#pragma once

#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /// The wall is a ring of four sides; a side is a row of stacks of two tiles.
    inline constexpr int kTilesPerStack = 2;
    inline constexpr int kStacksPerSide = 17;
    inline constexpr int kStacksPerSideWithBonus = 18;
    /// The last 7 stacks of the draw order are the dead wall: 14 tiles that are never drawn.
    inline constexpr int kDeadStacks = 7;
    inline constexpr size_t kDeadWallTiles = static_cast<size_t>(kDeadStacks) * kTilesPerStack;
    /// The first two tiles at the break are the loose tiles: a reserve for the replacement draws (after a kong or a bonus tile).
    inline constexpr size_t kLooseTiles = 2;

    /// Stacks in one side of the wall: 17, or 18 when the eight bonus tiles are in the set (136 or 144 tiles in the ring).
    [[nodiscard]] constexpr int StacksPerSide(bool flowers) noexcept {
        return flowers ? kStacksPerSideWithBonus : kStacksPerSide;
    }

    /// Tiles in the whole ring for `perSide` stacks per side.
    [[nodiscard]] constexpr int RingSize(int perSide) noexcept {
        return kSeatCount * perSide * kTilesPerStack;
    }

    /**
     * @brief The side of the wall that is broken, as the number of seats counter-clockwise from East's own wall (0 = East's wall).
     *        East's roll `eastDice` (2..12) is counted from East's own wall as side 1: side `(eastDice - 1) % 4`.
     */
    [[nodiscard]] constexpr int BreakSide(int eastDice) noexcept {
        return (eastDice - 1) % kSeatCount;
    }

    /**
     * @brief The ring index of the first tile drawn, from the two rolls of the break (an assumption of this repository, the usual
     *        way to break the wall).
     *
     * Ring index 0 is the right end of East's own side; a side holds `perSide` stacks of `kTilesPerStack` tiles and the sides follow
     * counter-clockwise. East rolls `eastDice` and that picks the side (`BreakSide`); the owner of that side rolls `ownerDice`; the
     * wall is broken after `eastDice + ownerDice` stacks counted from the right end of that side. When the count runs past the end of
     * the side it goes on into the next side, and past the last side back to the first. Both dice are the totals of 2d6 (2..12).
     */
    [[nodiscard]] constexpr int BreakIndex(int eastDice, int ownerDice, int perSide) noexcept {
        const int stacksInRing = kSeatCount * perSide;
        const int stack = (BreakSide(eastDice) * perSide + eastDice + ownerDice) % stacksInRing;
        return stack * kTilesPerStack;
    }

    /// How a wall was broken: whose wall, and the two rolls. All zero for a wall that was not built by `MakeWall`.
    struct WallBreak {
        Seat seat = 0;
        int eastDice = 0;
        int ownerDice = 0;
        int index = 0;
    };

    /**
     * @brief The wall of one hand, as the tiles in the order they are taken, starting at the break.
     *
     * - The first `kLooseTiles` (2) tiles are the **loose tiles**, a separate reserve. `DrawLoose` takes the front of it and refills it
     *   from the **live tail** (the live tile just before the dead wall), so the live wall shrinks by one with each loose draw.
     * - The last `kDeadWallTiles` (14, 7 stacks) are the **dead wall**: they are never drawn and never refill the reserve.
     * - The tiles between are the **live wall**: `Draw` takes the front. `LiveCount() == 0` means a drawn hand when the next draw is due.
     *
     * The loose-tile model (a reserve apart from the 14 dead tiles) is an assumption of this repository; the numbers are the named
     * constants above. A wall shorter than 16 tiles (tests) gives the reserve what it can first, then the dead wall, then the live wall.
     */
    class Wall {
    public:
        Wall() = default;

        /// A wall from tiles in draw order (the first tile is the first loose tile). Any length; the break is not recorded.
        [[nodiscard]] static Wall FromOrder(eastl::vector<Tile> order);

        /// The next tile of the live wall, or `nullopt` when it is empty. Never touches the loose tiles or the dead wall.
        [[nodiscard]] std::optional<Tile> Draw();

        /// The front of the reserve, then the reserve is refilled from the live tail when the live wall has a tile left; `nullopt`
        /// when the reserve is empty.
        [[nodiscard]] std::optional<Tile> DrawLoose();

        /// Tiles a regular `Draw` can still take.
        [[nodiscard]] size_t LiveCount() const noexcept {
            return _liveEnd - _liveBegin;
        }
        /// Tiles in the loose reserve.
        [[nodiscard]] size_t LooseCount() const noexcept {
            return _loose.size();
        }
        /// Tiles of the dead wall (14 for a full wall; it never changes during a hand).
        [[nodiscard]] size_t DeadWallSize() const noexcept {
            return _dead.size();
        }
        /// Every tile still in the wall: live, loose and dead. The tiles dealt, drawn and discarded are not counted.
        [[nodiscard]] size_t Remaining() const noexcept {
            return LiveCount() + LooseCount() + DeadWallSize();
        }

        /// The loose reserve, front first (the tile `DrawLoose` takes next).
        [[nodiscard]] const eastl::vector<Tile>& LooseTiles() const noexcept {
            return _loose;
        }
        /// The dead wall in draw order; for inspection only, nothing takes from it.
        [[nodiscard]] const eastl::vector<Tile>& DeadWall() const noexcept {
            return _dead;
        }
        /// The live tiles still to be drawn, in draw order.
        [[nodiscard]] eastl::vector<Tile> LiveTiles() const {
            return eastl::vector<Tile>(_live.begin() + _liveBegin, _live.begin() + _liveEnd);
        }

        [[nodiscard]] const WallBreak& GetBreak() const noexcept {
            return _break;
        }

    private:
        friend Wall MakeWall(MahjongRng& rng, const MahjongSettings& settings, Seat east);

        eastl::vector<Tile> _loose;
        eastl::vector<Tile> _live;
        eastl::vector<Tile> _dead;
        size_t _liveBegin = 0;
        size_t _liveEnd = 0;
        WallBreak _break;
    };

    /**
     * @brief Builds the wall of a hand: shuffles the tile set (136 tiles, 144 with `settings.flowers`), lays it in the ring (four sides
     *        of `StacksPerSide` stacks), rolls the break and returns the wall in draw order from the break.
     *
     * The rolls come from `rng` in a fixed order: the shuffle, then East's 2d6, then the owner's 2d6 (`BreakIndex`). `east` is the
     * seat of the dealer; it only names the seat whose wall is broken (`GetBreak().seat`). The same seed gives the same wall.
     */
    [[nodiscard]] Wall MakeWall(MahjongRng& rng, const MahjongSettings& settings, Seat east);

}  // namespace BECore::Mahjong
