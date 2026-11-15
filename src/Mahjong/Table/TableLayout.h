#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /**
     * The table is designed on a view of kViewWidth x kViewHeight design units (`MahjongScene` repeats the two numbers). A view of
     * another size gets the design scaled uniformly to fit and centred. Every tile has the proportions of the atlas cell (3 : 4).
     */
    inline constexpr float kViewWidth = 1280.0f;
    inline constexpr float kViewHeight = 720.0f;

    /// The shapes of the table, each the design of one view of the scene (`viewVariants` of `config/SceneConfig.xml` repeats the sizes
    /// and the rule). `Wide` is the landscape table with a wing on each side; `Narrow` arranges the viewer's side as the phone profile does.
    CORE_ENUM(TableShape, uint8_t, Landscape, Portrait, Wide, Narrow)

    inline constexpr float kPortraitViewWidth = 720.0f;
    inline constexpr float kPortraitViewHeight = 1280.0f;

    /// The wings are free ground for the canvases; the table is as high as the landscape one, so the tiles do not grow.
    inline constexpr float kWideViewWidth = 1560.0f;
    inline constexpr float kWideWingWidth = (kWideViewWidth - kViewWidth) / 2.0f;
    /// A landscape view of this aspect (width over height) or more is wide.
    inline constexpr float kWideMinAspect = 2.0f;

    /// A landscape view of this aspect or less is narrow. It is higher than the landscape view: the viewer's bonus tiles and melds lie in
    /// a row of their own above the hand.
    inline constexpr float kNarrowViewWidth = 1080.0f;
    inline constexpr float kNarrowViewHeight = 768.0f;
    inline constexpr float kNarrowMaxAspect = 1.55f;

    /// The shape of a view of this size. The comparisons are the engine's `UIVariantCondition` ones, so the scene file and the layout
    /// pick the same shape; a size that is not positive (the first frame, no output yet) is landscape.
    [[nodiscard]] constexpr TableShape ShapeOfView(float viewWidth, float viewHeight) noexcept {
        if (!(viewWidth > 0.0f) || !(viewHeight > 0.0f)) {
            return TableShape::Landscape;
        }
        if (viewHeight >= viewWidth) {
            return TableShape::Portrait;
        }
        const float aspect = viewWidth / viewHeight;
        if (aspect <= kNarrowMaxAspect) {
            return TableShape::Narrow;
        }
        return aspect >= kWideMinAspect ? TableShape::Wide : TableShape::Landscape;
    }

    /// The shape of the view the scene picks for an output of this size: the scene file tests the output with the same rule (a square
    /// output is portrait).
    [[nodiscard]] constexpr TableShape ShapeOfOutput(float outputWidth, float outputHeight) noexcept {
        return ShapeOfView(outputWidth, outputHeight);
    }

    /// Whether the shape has the landscape table (the phone profile, the landscape geometry): `Landscape` and `Wide`.
    [[nodiscard]] constexpr bool IsLandscapeLike(TableShape shape) noexcept {
        return shape == TableShape::Landscape || shape == TableShape::Wide;
    }

    /// The design size of a shape, in view units.
    [[nodiscard]] constexpr float DesignWidthOf(TableShape shape) noexcept {
        switch (shape) {
            case TableShape::Portrait:
                return kPortraitViewWidth;
            case TableShape::Wide:
                return kWideViewWidth;
            case TableShape::Narrow:
                return kNarrowViewWidth;
            default:
                return kViewWidth;
        }
    }

    [[nodiscard]] constexpr float DesignHeightOf(TableShape shape) noexcept {
        switch (shape) {
            case TableShape::Portrait:
                return kPortraitViewHeight;
            case TableShape::Narrow:
                return kNarrowViewHeight;
            default:
                return kViewHeight;
        }
    }

    /// The uniform scale that fits the design of the shape of a view of this size into it (view units per design unit); 0 for a size that
    /// is not positive.
    [[nodiscard]] constexpr float LayoutScale(float viewWidth, float viewHeight) noexcept {
        if (!(viewWidth > 0.0f) || !(viewHeight > 0.0f)) {
            return 0.0f;
        }
        const TableShape shape = ShapeOfView(viewWidth, viewHeight);
        const float scaleX = viewWidth / DesignWidthOf(shape);
        const float scaleY = viewHeight / DesignHeightOf(shape);
        return scaleX < scaleY ? scaleX : scaleY;
    }

    /// The viewer's hand tile at the desktop: the touch rules (`TapSelect.h`, `TouchRegime.h`) decide by it, so the zone of
    /// `config/TableLayout.xml` must solve to it (`CheckTableLayoutSpec`). Every other tile is the largest that fits its zone
    /// (`TableSolver.h`), so no constant of its size exists.
    inline constexpr float kViewerHandTileW = 48.0f;
    inline constexpr float kViewerHandTileH = 64.0f;
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
    /// The wall is a ring of stacks round the middle of the table: a rectangle of these cells, which are the most stacks it shows.
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
     * `seat` is the slot's seat (the discarder for a discard; 0 for a wall stack). `index` is the position among the seat's things:
     * the concealed tile (`HandTile`), the meld (`MeldTile`, with `part` the tile in the meld), the bonus tile, the entry of the pile
     * (`HandModel::Discards()`), the stack counted along the ring from its first cell.
     *
     * `rect` is the rectangle the eye sees (what a mark covers and a click hits). A side seat's tiles are drawn turned (`rotation`,
     * degrees clockwise on the screen) and `rect` is then the wider-than-high bounding box of the turned tile; `UprightRect` gives
     * the rectangle for a draw that applies the turn about the centre of `rect`.
     *
     * `claimEdge` is set on the one `MeldTile` slot of a claimed open meld that holds the claimed tile: the side of `rect` (the screen
     * rectangle, whatever the turn) that faces the seat it came from; `None` elsewhere.
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

    /// The turn of the tiles of the seat at the left and at the right of the screen, so that each reads upright for the player who sits
    /// there. The top seat's tiles stay upright (the owner's decision).
    inline constexpr float kLeftSeatRotation = 90.0f;
    inline constexpr float kRightSeatRotation = 270.0f;

    /// The unturned rectangle that a turn of `rotation` degrees (modulo 360) about the centre of `screenRect` makes `screenRect`: width
    /// and height swapped about that centre (`RectCentreX`, `RectCentreY`) for a quarter or three-quarter turn, else `screenRect`.
    [[nodiscard]] Rect UprightRect(const Rect& screenRect, float rotation) noexcept;

    [[nodiscard]] constexpr float RectCentreX(const Rect& rect) noexcept {
        return rect.x + rect.w / 2.0f;
    }

    [[nodiscard]] constexpr float RectCentreY(const Rect& rect) noexcept {
        return rect.y + rect.h / 2.0f;
    }

    /// The widest hand tile the phone profile holds: the bottom plate (lifted to end where the tallest hand starts) and the rim decide it.
    inline constexpr float kPhoneHandTileMaxW = 78.0f;

    /**
     * How the viewer's side of the table is laid out. `phone` is for a window where the desktop hand tile would be too small to touch
     * (`TableProfileFor` in `TouchRegime.h` decides): the 14 concealed tiles are one row of `handTileW` wide tiles, the bonus tiles and
     * melds a row above it, the bottom discard block lower and squeezed. The rest of the table stays (the bottom plate only moves up to
     * clear the tallest hand tile). Not `phone` is the desktop table whatever `handTileW` is.
     * Landscape and wide only: portrait and narrow have a one-row hand of their own and ignore the profile.
     */
    struct TableProfile {
        bool phone = false;
        float handTileW = kViewerHandTileW;

        [[nodiscard]] bool operator==(const TableProfile& other) const noexcept {
            return phone == other.phone && handTileW == other.handTileW;
        }
    };

    /// What is on the table, as numbers. The layout needs no tile identity, only how many of each thing there are.
    struct TableInput {
        /// The seat at the bottom of the view. A number that is no seat is laid out as seat 0.
        Seat viewer = 0;
        /// The profile of the viewer's side; a phone `handTileW` outside `kViewerHandTileW .. kPhoneHandTileMaxW` is clamped into it.
        TableProfile profile;
        Array<int, kSeatCount> concealedCount{};
        /// The melds as laid: a chow or pung is three tiles, a kong four (a concealed kong is laid as four tiles too).
        Array<Vector<Meld>, kSeatCount> melds;
        Array<int, kSeatCount> bonusCount{};
        /// The seat of each entry of the pile, in the order discarded (`HandModel::Discards()`).
        Vector<Seat> discardSeats;
        /// `Wall::Remaining()`: two tiles make a stack.
        int wallRemaining = 0;
    };

    /// The size of each of the four seat plates; `MahjongSeatPlates.xml` repeats it. Their places are the `Plate` zones of
    /// `config/TableLayout.xml`.
    inline constexpr float kSeatPlateW = 152.0f;
    inline constexpr float kSeatPlateH = 80.0f;

    /// The name of a region of the table (`TableRegions.h`): at most this long, so a layout and a spec keep it without the heap.
    inline constexpr size_t kMaxRegionNameLength = 31;
    using RegionName = FixedString<kMaxRegionNameLength + 1, false>;

    /// A free-ground region of the layout (`ShapeSpec::freeRegions`), fitted to the view like a zone: a place a canvas of the table may use,
    /// clear of every tile and plate of the worst-case table.
    struct FreeZone {
        RegionName name;
        Rect rect;
    };

    /// The zones of the table, in view units. `discards` and `seatPlates` are indexed by seat (a seat's plate lies at the screen position
    /// `SeatPosition(viewer, seat)`). A zone holds its slots and overlaps no other zone and no hand, meld or bonus slot; a plate lies at
    /// least `kPlatePadding` clear of every discard block and of the wall ring. The centre panel is the inside of the wall ring (the ring
    /// less one stack on every side), holds no slot and is the same for every viewer.
    struct TableZones {
        Array<Rect, kSeatCount> discards{};
        Rect wall;
        Array<Rect, kSeatCount> seatPlates{};
        Rect centerPanel;
        /// `ShapeSpec::freeRegions` fitted like the zones, in the order of the file; clear of every slot and zone for the worst-case table.
        Vector<FreeZone> freeRegions;
    };

    /// The slots of the four seats and the wall: per seat the hand, the melds, the bonus tiles, then the pile, then the wall stacks.
    struct TableLayout {
        Seat viewer = 0;
        /// The shape the layout was made in: `ShapeOfView(viewWidth, viewHeight)`.
        TableShape shape = TableShape::Landscape;
        /// The profile as used: a desktop one always has `kViewerHandTileW`, a phone one a clamped width; the default in portrait and narrow.
        TableProfile profile;
        float viewWidth = kViewWidth;
        float viewHeight = kViewHeight;
        Vector<TileSlot> slots;
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

    struct TableLayoutSpec;

    /// Lays out the table in the shape of a view of this size (`ShapeOfView`), designed at that shape's size and fitted into the view
    /// (`LayoutScale`). Deterministic. The zones come from the active spec; with none set the layout has its shape and no slots (and the
    /// first such call logs it).
    [[nodiscard]] TableLayout MakeTableLayout(const TableInput& input, float viewWidth = kViewWidth, float viewHeight = kViewHeight);

    /// The same from a spec: the zones are `spec`'s, the tile in each the largest that the zone holds for what the rules allow
    /// (`TableSolver.h`). A shape the spec does not describe gives no slots. Pure: no active spec, no log.
    [[nodiscard]] TableLayout MakeTableLayout(const TableInput& input, const TableLayoutSpec& spec, float viewWidth = kViewWidth, float viewHeight = kViewHeight);

    /// `MakeTableLayout` into `layout`, which keeps the capacity of its slots, so the table lays itself out every frame without the heap.
    void RebuildTableLayout(TableLayout& layout, const TableInput& input, float viewWidth = kViewWidth, float viewHeight = kViewHeight);
    void RebuildTableLayout(TableLayout& layout, const TableInput& input, const TableLayoutSpec& spec, float viewWidth = kViewWidth, float viewHeight = kViewHeight);

    /// The spec the overload without one reads; the caller keeps it alive until it sets another or null. Main thread only.
    void SetActiveTableLayoutSpec(const TableLayoutSpec* spec) noexcept;
    [[nodiscard]] const TableLayoutSpec* ActiveTableLayoutSpec() noexcept;

    /// What is wrong with a spec as a table: the worst table (every zone at what the rules allow) is built for every shape and viewer
    /// (and the phone profile at its least, a middle and its greatest width) and checked against the contract of `TableZones`, `minTileW`,
    /// `kViewerHandTileW` and `kPhoneHandTileMaxW`. One message per distinct problem, naming the shape (and the profile); empty when sound.
    [[nodiscard]] Vector<String> CheckTableLayoutSpec(const TableLayoutSpec& spec);

    /// The smallest rectangle that holds every slot of `role` of `seat` (`seat` ignored for the wall); empty when there is none.
    [[nodiscard]] Rect BoundsOf(const TableLayout& layout, SlotRole role, Seat seat);

    /// The rectangle round the slots of meld `meldIndex` of `seat`; empty when there is none.
    [[nodiscard]] Rect BoundsOfMeld(const TableLayout& layout, Seat seat, int meldIndex);

    /// The index (in the viewer's concealed tiles) of the hand tile for a click at (x, y). Forgiving: the region is the unraised strip of
    /// the hand, from the top of the tiles to the bottom of the view and from the first tile's left edge to the last one's right edge;
    /// inside it the tile nearest in x wins (the later one on an overlap or a tie). The bonus row and the melds lie outside it.
    [[nodiscard]] Optional<int> HitHandTile(const TableLayout& layout, float x, float y);

}  // namespace BECore::Mahjong
