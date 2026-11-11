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

    /// The shapes of the table, each the design of one view of the scene (`viewVariants` of `config/SceneConfig.xml`, which repeats the
    /// sizes and the rule; `MahjongSceneFilesTest` compares them): `Landscape` is the view of `kViewWidth` x `kViewHeight`, `Portrait`
    /// one at least as high as wide, `Wide` a landscape one that is `kWideMinAspect` times as wide as high or more (the landscape table
    /// with a wing on each side), `Narrow` a landscape one that is `kNarrowMaxAspect` times as wide as high or less (4:3, 3:2: the
    /// viewer's side is arranged the way the phone profile does it).
    CORE_ENUM(TableShape, uint8_t, Landscape, Portrait, Wide, Narrow)

    inline constexpr float kPortraitViewWidth = 720.0f;
    inline constexpr float kPortraitViewHeight = 1280.0f;

    /// The wide view: the landscape table centred in it with a wing of `kWideWingWidth` units on each side (free ground for the canvases;
    /// the table is as high as before, so the tiles do not grow).
    inline constexpr float kWideViewWidth = 1560.0f;
    inline constexpr float kWideWingWidth = (kWideViewWidth - kViewWidth) / 2.0f;
    /// A landscape view of this aspect (width over height) or more is wide; `orientation:landscape` views below it are landscape.
    inline constexpr float kWideMinAspect = 2.0f;

    /// The narrow view: a landscape view of this aspect or less (4:3, 3:2, 5:4); the middle of 4:3 and 3:2 so neither loses much. It is
    /// 48 units higher than the landscape view: the viewer's bonus tiles and melds lie in a row of their own above the hand.
    inline constexpr float kNarrowViewWidth = 1080.0f;
    inline constexpr float kNarrowViewHeight = 768.0f;
    inline constexpr float kNarrowMaxAspect = 1.55f;

    /// The shape of a view of this size: portrait iff `height >= width`, else narrow iff `width / height <= kNarrowMaxAspect`, else wide
    /// iff `width / height >= kWideMinAspect` (the same float expressions as the engine's `UIVariantCondition`), else landscape; a size
    /// that is not positive (the first frame, no output yet) is landscape.
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

    /// The shape of the view the scene picks for an output of this size: the same rule as `ShapeOfView` (the `orientation` and aspect of the
    /// scene file test the output, so a square output is portrait).
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

    /// Named golden numbers, the few the layout is held to by code outside the file: the viewer's hand tile at the desktop (the zone of
    /// `config/TableLayout.xml` must solve to it: `CheckTableLayoutSpec`; `TapSelect.h` and `TouchRegime.h` decide the touch rules by it).
    /// Every other tile is the largest that fits its zone (`TableSolver.h`); no constant of its size is kept.
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

    /// The widest hand tile the phone profile holds: the bottom plate (which the phone profile lifts to end where the tallest hand starts)
    /// and the rim decide it. The 14 tiles of it span 85 percent of the view.
    inline constexpr float kPhoneHandTileMaxW = 78.0f;

    /**
     * How the viewer's side of the table is laid out. `phone` is for a window where the desktop hand tile (`kViewerHandTileW` units) would
     * be too small to touch (`TableProfileFor` in `TouchRegime.h` decides): the 14 concealed tiles are one row `handTileW` units wide
     * each in a plain row from the left of the view (side by side, as on the desktop), the viewer's bonus tiles and melds lie in a row of their own above it
     * and the bottom discard block is lower and squeezed. The wall ring, the centre panel, the three other seats and the other plates stay
     * where they are (the bottom plate only moves up to clear the tallest hand tile). Not `phone` is today's table whatever `handTileW` is.
     * Landscape and wide only: the portrait and the narrow table have a one-row hand of their own and ignore the profile.
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
    /// size, in design units; `MahjongSeatPlates.xml` repeats it (`TableLayoutTest` holds the size). The places are the `Plate` zones
    /// of `config/TableLayout.xml`, each with the things it stays clear of.
    inline constexpr float kSeatPlateW = 152.0f;
    inline constexpr float kSeatPlateH = 80.0f;

    /// The zones of the table, in view units: the block that holds each seat's discards (by seat, whatever the pile holds), the ring
    /// that holds the wall stacks, the plate of each seat (by seat, for the viewer at the bottom: it lies at the screen position
    /// `SeatPosition(viewer, seat)`) and the centre panel. A zone holds its slots and overlaps no other zone and no hand, meld or bonus
    /// slot; a seat plate overlaps no slot and no other seat plate, and lies at least `kPlatePadding` clear of every discard block and
    /// of the wall ring. The centre panel is the inside of the wall ring (the ring less one wall stack on every side): it holds no slot
    /// and no wall stack, and is the same for every viewer.
    struct TableZones {
        eastl::array<Rect, kSeatCount> discards{};
        Rect wall;
        eastl::array<Rect, kSeatCount> seatPlates{};
        Rect centerPanel;
    };

    /// The slots of the four seats and the wall: per seat the hand, the melds, the bonus tiles, then the pile, then the wall stacks.
    struct TableLayout {
        Seat viewer = 0;
        /// The shape the layout was made in: `ShapeOfView(viewWidth, viewHeight)`.
        TableShape shape = TableShape::Landscape;
        /// The profile the layout was made with, as used (a desktop one always has `kViewerHandTileW`, a phone one a clamped width; the
        /// default in portrait and narrow).
        TableProfile profile;
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

    struct TableLayoutSpec;

    /// Lays out the table for a view of the given size (see `kViewWidth`) in the shape of that view (`ShapeOfView`): the landscape table
    /// of `kViewWidth` x `kViewHeight` units, the portrait one of `kPortraitViewWidth` x `kPortraitViewHeight`, the wide view of
    /// `kWideViewWidth` x `kViewHeight` (the landscape table `kWideWingWidth` from its left edge) or the narrow one of `kNarrowViewWidth`
    /// x `kNarrowViewHeight`, fitted into the view (`LayoutScale`) and
    /// centred. Deterministic: the same input gives the same slots. The zones come from the active spec (`ActiveTableLayoutSpec`); with none
    /// set the layout has its shape and no slots (and the first such call logs it).
    [[nodiscard]] TableLayout MakeTableLayout(const TableInput& input, float viewWidth = kViewWidth, float viewHeight = kViewHeight);

    /// The same from a spec: the zones are `spec`'s (fractions of the design view of the shape, `TableLayoutSpec.h`), the tile in each is
    /// the largest that the zone holds for what the rules allow (`TableSolver.h`). A shape the spec does not describe gives a layout with
    /// no slots. Pure: no active spec, no log.
    [[nodiscard]] TableLayout MakeTableLayout(const TableInput& input, const TableLayoutSpec& spec, float viewWidth = kViewWidth, float viewHeight = kViewHeight);

    /// The spec the overload without one reads (a pointer: the caller keeps the spec alive until it sets another or null). Set and read on
    /// the main thread only. The game sets it from `config/TableLayout.xml` (`MahjongTableComponent`), the tests from the source tree.
    void SetActiveTableLayoutSpec(const TableLayoutSpec* spec) noexcept;
    [[nodiscard]] const TableLayoutSpec* ActiveTableLayoutSpec() noexcept;

    /// What is wrong with a spec as a table: for every shape, the worst table (every zone at what the rules allow) for every viewer and,
    /// for the landscape-like shapes that have a phone override, the phone profile at the least, a middle and the greatest tile width, is
    /// built and checked. One message per distinct problem, each naming the shape (and the profile) and what: a slot outside the design
    /// view, zones that overlap each other or a hand, meld or bonus row, a seat plate that overlaps a slot or lies nearer than
    /// `kPlatePadding` to a discard block or the wall ring, a tile under its zone's `minTileW`, a zone too small for its tiles, the
    /// viewer's hand tile under `kViewerHandTileW`, the widest phone tile other than `kPhoneHandTileMaxW`. Empty when the spec is sound.
    [[nodiscard]] eastl::vector<eastl::string> CheckTableLayoutSpec(const TableLayoutSpec& spec);

    /// The smallest rectangle that holds every slot of `role` of `seat` (`seat` ignored for the wall); empty when there is none.
    [[nodiscard]] Rect BoundsOf(const TableLayout& layout, SlotRole role, Seat seat);

    /// The rectangle round the slots of meld `meldIndex` of `seat`; empty when there is none.
    [[nodiscard]] Rect BoundsOfMeld(const TableLayout& layout, Seat seat, int meldIndex);

    /// The index (in the viewer's concealed tiles) of the hand tile for a click at the point (x, y). Forgiving: the region is the strip of
    /// the viewer's hand, from the top of the tiles to the bottom of the view (the margin under them included) and from the left edge of
    /// the first tile to the right edge of the last; inside it the tile whose horizontal span is nearest x wins (a gap goes to the nearest
    /// tile; where tiles overlap or on a tie, the later one). Nothing outside the strip (the bonus row lies above it, the melds to its
    /// right), nothing for a seat that is not the viewer or without hand tiles. The region is the unraised strip.
    [[nodiscard]] std::optional<int> HitHandTile(const TableLayout& layout, float x, float y);

}  // namespace BECore::Mahjong
