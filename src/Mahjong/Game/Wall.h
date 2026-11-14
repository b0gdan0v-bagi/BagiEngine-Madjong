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
    /// The last stacks of the draw order are the dead wall: never drawn.
    inline constexpr int kDeadStacks = 7;
    inline constexpr size_t kDeadWallTiles = static_cast<size_t>(kDeadStacks) * kTilesPerStack;
    /// The first tiles at the break are the loose tiles: a reserve for the replacement draws (after a kong or a bonus tile).
    inline constexpr size_t kLooseTiles = 2;

    /// Stacks in one side of the wall; the set with bonus tiles needs one more.
    [[nodiscard]] constexpr int StacksPerSide(bool flowers) noexcept {
        return flowers ? kStacksPerSideWithBonus : kStacksPerSide;
    }

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
     * @brief The ring index of the first tile drawn, from the two rolls of the break (an assumption of this repository).
     *
     * Ring index 0 is the right end of East's own side. The sides lie **clockwise** in the ring: side `s` (counted counter-clockwise
     * from East's wall, see `BreakSide`) starts at ring position `((kSeatCount - s) % kSeatCount) * perSide` stacks, so the index is
     * the physical position of the break as on a real table. The wall is broken after `eastDice + ownerDice` stacks counted from the
     * right end of the side `eastDice` picks, wrapping round the ring.
     */
    [[nodiscard]] constexpr int BreakIndex(int eastDice, int ownerDice, int perSide) noexcept {
        const int stacksInRing = kSeatCount * perSide;
        const int sideStart = ((kSeatCount - BreakSide(eastDice)) % kSeatCount) * perSide;
        const int stack = (sideStart + eastDice + ownerDice) % stacksInRing;
        return stack * kTilesPerStack;
    }

    /// How a wall was broken: whose wall, the two rolls and the ring index of the first tile drawn (`BreakIndex`). All zero for a wall that was not built by `MakeWall`.
    struct WallBreak {
        Seat seat = 0;
        int eastDice = 0;
        int ownerDice = 0;
        int index = 0;
    };

    /**
     * @brief The wall of one hand, as the tiles in the order they are taken, starting at the break.
     *
     * - The first `kLooseTiles` tiles are the **loose tiles**, a separate reserve. `DrawLoose` takes the front of it and refills it
     *   from the **live tail** (the live tile just before the dead wall), so the live wall shrinks by one with each loose draw.
     * - The last `kDeadWallTiles` are the **dead wall**: they are never drawn and never refill the reserve.
     * - The tiles between are the **live wall**: `Draw` takes the front. `LiveCount() == 0` means a drawn hand when the next draw is due.
     *
     * The loose-tile model (a reserve apart from the dead wall) is an assumption of this repository. A wall too short for both (tests)
     * gives the reserve what it can first, then the dead wall, then the live wall.
     */
    class Wall {
    public:
        Wall() = default;

        /// A wall from tiles in draw order (the first tile is the first loose tile). Any length; the break is not recorded.
        [[nodiscard]] static Wall FromOrder(Vector<Tile> order);

        /// A wall from its three parts exactly as given (for `HandModel::Restore` and the tests): `loose` front first, `live` in draw
        /// order, `dead` in draw order. No layout is guessed from the sizes and the break is not recorded.
        [[nodiscard]] static Wall FromParts(Vector<Tile> loose, Vector<Tile> live, Vector<Tile> dead);

        /// The next tile of the live wall, or `nullopt` when it is empty. Never touches the loose tiles or the dead wall.
        [[nodiscard]] Optional<Tile> Draw();

        /// The front of the reserve, refilled from the live tail while the live wall has a tile; `nullopt` when the reserve is empty.
        [[nodiscard]] Optional<Tile> DrawLoose();

        [[nodiscard]] size_t LiveCount() const noexcept {
            return _liveEnd - _liveBegin;
        }
        [[nodiscard]] size_t LooseCount() const noexcept {
            return _loose.size();
        }
        /// Tiles of the dead wall; it never changes during a hand.
        [[nodiscard]] size_t DeadWallSize() const noexcept {
            return _dead.size();
        }
        /// Every tile still in the wall: live, loose and dead. The tiles dealt, drawn and discarded are not counted.
        [[nodiscard]] size_t Remaining() const noexcept {
            return LiveCount() + LooseCount() + DeadWallSize();
        }

        [[nodiscard]] const Vector<Tile>& LooseTiles() const noexcept {
            return _loose;
        }
        /// For inspection only, nothing takes from the dead wall.
        [[nodiscard]] const Vector<Tile>& DeadWall() const noexcept {
            return _dead;
        }
        [[nodiscard]] Vector<Tile> LiveTiles() const {
            return Vector<Tile>(_live.begin() + _liveBegin, _live.begin() + _liveEnd);
        }

        [[nodiscard]] const WallBreak& GetBreak() const noexcept {
            return _break;
        }

    private:
        friend Wall MakeWall(MahjongRng& rng, const MahjongSettings& settings, Seat east);

        Vector<Tile> _loose;
        Vector<Tile> _live;
        Vector<Tile> _dead;
        size_t _liveBegin = 0;
        size_t _liveEnd = 0;
        WallBreak _break;
    };

    /**
     * @brief Builds the wall of a hand: shuffles the tile set, lays it in the ring, rolls the break and returns the wall in draw order
     *        from the break.
     *
     * The rolls come from `rng` in a fixed order: the shuffle, then East's 2d6, then the owner's 2d6 (`BreakIndex`). `east` is the
     * seat of the dealer; it only names the seat whose wall is broken (`GetBreak().seat`). The same seed gives the same wall.
     */
    [[nodiscard]] Wall MakeWall(MahjongRng& rng, const MahjongSettings& settings, Seat east);

}  // namespace BECore::Mahjong
