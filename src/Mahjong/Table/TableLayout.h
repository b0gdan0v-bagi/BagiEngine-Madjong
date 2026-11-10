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

    /// The viewer's concealed tiles are large; the tiles of the other seats are small. Sizes (width, height) of the upright tiles of each
    /// zone; a side seat draws them turned, so its slots are height x width.
    inline constexpr float kViewerHandTileW = 48.0f;
    inline constexpr float kViewerHandTileH = 64.0f;
    inline constexpr float kViewerMeldTileW = 30.0f;
    inline constexpr float kViewerMeldTileH = 40.0f;
    inline constexpr float kOpponentHandTileW = 28.0f;
    inline constexpr float kOpponentHandTileH = 37.0f;
    /// The hands of the left and right seats are backs seen from the side, drawn turned: smaller than the top seat's, so that the two
    /// columns of turned tiles leave room for the discard blocks (the table is full across: `TableLayout.cpp`).
    inline constexpr float kSideHandTileW = 24.0f;
    inline constexpr float kSideHandTileH = 32.0f;
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

    /// A side of a tile's rectangle, as seen on the screen.
    CORE_ENUM(Edge, uint8_t, None, Left, Right, Top, Bottom)

    /**
     * @brief One rectangle of the table, in view units.
     *
     * `seat` is the seat the slot belongs to (the discarder for a discard; 0 for a wall stack). `index` says which of the seat's things it
     * is: `HandTile` the index in the seat's concealed tiles, `MeldTile` the index of the meld in the seat's melds (`part` the tile in
     * the meld), `BonusTile` the index in the seat's bonus tiles, `DiscardTile` the index in the pile (`HandModel::Discards()`),
     * `WallStack` the stack counted along the ring from its first cell.
     *
     * `rect` is the rectangle the eye sees (what a mark covers and a click hits). The tiles of the side seats (screen positions 1 and 3)
     * are drawn turned to face the table: `rotation` is the turn in degrees clockwise on the screen (90 for the left seat, 270 for the
     * right one, 0 for every other slot), and `rect` is then wider than high, the bounding box of the turned tile; `UprightRect` gives the
     * rectangle to hand to a draw that applies the turn about the centre of `rect`.
     *
     * `claimEdge` is set on one `MeldTile` slot of an open meld that was claimed from a discard: the claimed tile, and the side of it
     * that faces the seat it came from (`None` on every other slot). It is an edge of `rect` (the screen rectangle), whatever the turn.
     */
    struct TileSlot {
        Rect rect;
        SlotRole role = SlotRole::HandTile;
        Seat seat = 0;
        int index = 0;
        int part = 0;
        Edge claimEdge = Edge::None;
        float rotation = 0.0f;
    };

    /// The turn of the tiles of the seat at the left of the screen and at the right of it, in degrees clockwise (so that each reads upright
    /// for the player who sits there). The top seat's tiles stay upright (the owner's decision).
    inline constexpr float kLeftSeatRotation = 90.0f;
    inline constexpr float kRightSeatRotation = 270.0f;

    /// The rectangle of an unturned tile that a turn of `rotation` degrees about the centre of `screenRect` makes `screenRect`: the width and
    /// the height swapped about the same centre for a quarter or three-quarter turn (the turn is taken modulo 360), `screenRect` itself
    /// for any other turn. The pivot of that turn is the centre of `screenRect` (`RectCentreX`, `RectCentreY`).
    [[nodiscard]] Rect UprightRect(const Rect& screenRect, float rotation) noexcept;

    [[nodiscard]] constexpr float RectCentreX(const Rect& rect) noexcept {
        return rect.x + rect.w / 2.0f;
    }

    [[nodiscard]] constexpr float RectCentreY(const Rect& rect) noexcept {
        return rect.y + rect.h / 2.0f;
    }

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

    /// A seat plate (the seat's number, wind, dealer mark and score, at the side of the table where the seat sits): all four are of this
    /// size, in design units. The places are in `TableLayout.cpp`, each with the things it stays clear of.
    inline constexpr float kSeatPlateW = 152.0f;
    inline constexpr float kSeatPlateH = 80.0f;

    /// The zones of the table, in view units: the block that holds each seat's discards (by seat, whatever the pile holds), the ring
    /// that holds the wall stacks and the plate of each seat (by seat, for the viewer at the bottom: it lies at the screen position
    /// `SeatPosition(viewer, seat)`). A zone holds its slots and overlaps no other zone and no hand, meld or bonus slot; a seat plate
    /// overlaps no slot and no other seat plate, and lies at least `kPlatePadding` clear of every discard block and of the wall ring.
    struct TableZones {
        eastl::array<Rect, kSeatCount> discards{};
        Rect wall;
        eastl::array<Rect, kSeatCount> seatPlates{};
    };

    /// The slots of the four seats and the wall: per seat the hand, the melds, the bonus tiles, then the pile, then the wall stacks.
    struct TableLayout {
        Seat viewer = 0;
        float viewWidth = kViewWidth;
        float viewHeight = kViewHeight;
        eastl::vector<TileSlot> slots;
        TableZones zones;
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

    /// The rectangle round the slots of meld `meldIndex` of `seat`; empty when there is none.
    [[nodiscard]] Rect BoundsOfMeld(const TableLayout& layout, Seat seat, int meldIndex);

    /// The index (in the viewer's concealed tiles) of the hand tile at the point (x, y); the later tile where tiles overlap.
    [[nodiscard]] std::optional<int> HitHandTile(const TableLayout& layout, float x, float y);

}  // namespace BECore::Mahjong
