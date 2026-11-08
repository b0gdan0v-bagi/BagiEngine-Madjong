#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /**
     * The table is designed on a view of kViewWidth x kViewHeight units (the view size of `MahjongScene`, which repeats these two
     * numbers; `MahjongTableUiTest` compares them). `MakeTableLayout` for another view size scales the design uniformly to fit and
     * centres it. All sizes below are in design units; every tile has the proportions of the atlas cell (3 : 4).
     */
    inline constexpr float kViewWidth = 1280.0f;
    inline constexpr float kViewHeight = 720.0f;

    /// The viewer's concealed tiles are large; the tiles of the other seats are small. Sizes (width, height) of the tiles of each zone.
    inline constexpr float kViewerHandTileW = 45.0f;
    inline constexpr float kViewerHandTileH = 60.0f;
    inline constexpr float kViewerMeldTileW = 30.0f;
    inline constexpr float kViewerMeldTileH = 40.0f;
    inline constexpr float kOpponentHandTileW = 28.0f;
    inline constexpr float kOpponentHandTileH = 37.0f;
    inline constexpr float kOpponentMeldTileW = 24.0f;
    inline constexpr float kOpponentMeldTileH = 32.0f;
    inline constexpr float kViewerBonusTileW = 24.0f;
    inline constexpr float kViewerBonusTileH = 32.0f;
    inline constexpr float kOpponentBonusTileW = 16.0f;
    inline constexpr float kOpponentBonusTileH = 21.0f;
    inline constexpr float kDiscardTileW = 24.0f;
    inline constexpr float kDiscardTileH = 32.0f;
    /// A wall stack (two tiles, seen from above) is a square of this side.
    inline constexpr float kWallStackSize = 14.0f;
    /// The space between two melds of a seat.
    inline constexpr float kMeldGap = 8.0f;

    /// What a zone is made to hold; more is squeezed together (the tiles overlap) so that a zone never grows. The rules allow no more.
    inline constexpr int kMaxConcealedTiles = 14;
    inline constexpr int kMaxMelds = 4;
    inline constexpr int kMaxMeldTiles = kMaxMelds * 4;
    inline constexpr int kMaxBonusTiles = 8;
    /// The discards of a seat lie in rows of this many tiles, in the order discarded.
    inline constexpr int kDiscardsPerRow = 6;
    /// Rows a discard block holds without overlap; a longer pile squeezes its rows together.
    inline constexpr int kDiscardRowCapacity = 5;
    /// The wall is a ring of stacks round the middle of the table: a rectangle of these cells; its cells are the most stacks it shows
    /// (a 144-tile set is 72 stacks).
    inline constexpr int kWallRingColumns = 26;
    inline constexpr int kWallRingRows = 12;
    inline constexpr int kWallRingCells = 2 * (kWallRingColumns + kWallRingRows) - 4;

    /// What a slot is for.
    CORE_ENUM(SlotRole, uint8_t, HandTile, MeldTile, BonusTile, DiscardTile, WallStack)

    /**
     * @brief One rectangle of the table, in view units.
     *
     * `seat` is the seat the slot belongs to (the discarder for a discard; 0 for a wall stack). `index` says which of the seat's things it
     * is: `HandTile` the index in the seat's concealed tiles, `MeldTile` the index of the meld in the seat's melds (`part` the tile in
     * the meld), `BonusTile` the index in the seat's bonus tiles, `DiscardTile` the index in the pile (`HandModel::Discards()`),
     * `WallStack` the stack counted along the ring from its first cell.
     */
    struct TileSlot {
        Rect rect;
        SlotRole role = SlotRole::HandTile;
        Seat seat = 0;
        int index = 0;
        int part = 0;
    };

    /// What is on the table, as numbers. The layout needs no tile identity, only how many of each thing there are.
    struct TableInput {
        /// The seat at the bottom of the view. A number that is no seat is laid out as seat 0.
        Seat viewer = 0;
        eastl::array<int, kSeatCount> concealedCount{};
        /// The melds as laid: a chow or pung is three tiles, a kong four (a concealed kong is laid as four tiles too).
        eastl::array<eastl::vector<Meld>, kSeatCount> melds;
        eastl::array<int, kSeatCount> bonusCount{};
        /// The seat of each entry of the pile, in the order discarded (`HandModel::Discards()`).
        eastl::vector<Seat> discardSeats;
        /// `Wall::Remaining()`: two tiles make a stack.
        int wallRemaining = 0;
    };

    /// The slots of the four seats and the wall: per seat the hand, the melds, the bonus tiles, then the pile, then the wall stacks.
    struct TableLayout {
        Seat viewer = 0;
        float viewWidth = kViewWidth;
        float viewHeight = kViewHeight;
        eastl::vector<TileSlot> slots;
    };

    /// Where `seat` sits for `viewer`: 0 bottom, 1 right, 2 top, 3 left (counter-clockwise from the viewer).
    [[nodiscard]] constexpr int SeatPosition(Seat viewer, Seat seat) noexcept {
        return Offset(viewer, seat);
    }

    /// The seat at `position` (0 bottom, 1 right, 2 top, 3 left) for `viewer`; the inverse of `SeatPosition`.
    [[nodiscard]] constexpr Seat SeatAtPosition(Seat viewer, int position) noexcept {
        return (viewer + position) % kSeatCount;
    }

    /// The number of wall stacks drawn for `wallRemaining` tiles: two tiles a stack, at most `kWallRingCells`.
    [[nodiscard]] int WallStackCount(int wallRemaining) noexcept;

    /// Lays out the table for a view of the given size (see `kViewWidth`). Deterministic: the same input gives the same slots.
    [[nodiscard]] TableLayout MakeTableLayout(const TableInput& input, float viewWidth = kViewWidth, float viewHeight = kViewHeight);

    /// The smallest rectangle that holds every slot of `role` of `seat` (`seat` ignored for the wall); empty when there is none.
    [[nodiscard]] Rect BoundsOf(const TableLayout& layout, SlotRole role, Seat seat);

    /// The index (in the viewer's concealed tiles) of the hand tile at the point (x, y); the later tile where tiles overlap.
    [[nodiscard]] std::optional<int> HitHandTile(const TableLayout& layout, float x, float y);

}  // namespace BECore::Mahjong
