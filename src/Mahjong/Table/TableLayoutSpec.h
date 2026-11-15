#pragma once

#include <EASTL/array.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * The table as data: where each zone lies, as fractions of the design view of its shape. Plain values, no scene, no XML. A zone says
     * only where tiles may lie; the size of the tile in it is solved (`TableSolver.h`), so a zone is never too small for what the rules
     * allow it to hold.
     */

    /// Where a strip's tiles lie across its line (the vertical axis of a row, the horizontal one of a column) when the zone is wider.
    CORE_ENUM(ZoneAlign, uint8_t, Start, Center, End)

    /// A zone as fractions of the design width and height of the shape (0..1; not snapped, see `SnapZone`).
    struct ZoneRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        [[nodiscard]] bool operator==(const ZoneRect& other) const noexcept = default;
    };

    /// A zone for tiles in a line: the hand, the melds or the bonus tiles of a seat.
    struct StripZone {
        ZoneRect rect;
        ZoneAlign align = ZoneAlign::Start;
        /// The tiles are spread over the zone's length instead of lying side by side (the viewer's hand in portrait).
        bool spread = false;
        /// The tile width is asked for, not solved (the phone profile's hand: `TableInput::profile.handTileW`, clamped).
        bool profileTile = false;
        /// The smallest tile width the zone may solve to; a spec whose zone solves smaller is refused. 0 asks nothing.
        int minTileW = 0;

        [[nodiscard]] bool operator==(const StripZone& other) const noexcept = default;
    };

    /// A zone for the pile of a seat: `rows` rows of `kDiscardsPerRow` tiles without squeeze.
    struct DiscardZone {
        ZoneRect rect;
        int rows = kDiscardRowCapacity;

        [[nodiscard]] bool operator==(const DiscardZone& other) const noexcept = default;
    };

    /// The zones of one seat, by screen position.
    struct SeatSpec {
        StripZone hand;
        StripZone melds;
        StripZone bonus;
        DiscardZone discards;
        ZoneRect plate;

        [[nodiscard]] bool operator==(const SeatSpec& other) const noexcept = default;
    };

    /// A named free-ground region: a place for a canvas of the table, clear of every tile, block, ring and plate (`CheckTableLayoutSpec`).
    /// A region that must lie on the wall or the centre panel is a zone region built from the layout (`TableRegions.h`), not a free one.
    struct FreeRegion {
        RegionName name;
        ZoneRect rect;

        [[nodiscard]] bool operator==(const FreeRegion& other) const noexcept = default;
    };

    /// The zones of one shape. A `like` shape borrows every zone of another shape (Wide is Landscape): its seats, wall and phone override
    /// are not read, and the borrowed table lies `inset` (a fraction of this shape's design width) from the left edge; its free regions
    /// are the borrowed shape's, shifted with the table.
    struct ShapeSpec {
        /// Set by the reader when the file describes the shape.
        bool present = false;
        bool like = false;
        TableShape likeShape = TableShape::Landscape;
        float inset = 0.0f;
        /// By screen position: 0 bottom, 1 right, 2 top, 3 left.
        Array<SeatSpec, kSeatCount> seats;
        ZoneRect wall;
        /// The free-ground regions of the shape, by name (a name once).
        Vector<FreeRegion> freeRegions;
        /// The viewer's side (position 0) for the phone profile.
        bool hasPhone = false;
        SeatSpec phone;

        [[nodiscard]] bool operator==(const ShapeSpec& other) const noexcept = default;
    };

    inline constexpr size_t kTableShapeCount = 4;

    /// The whole spec: one `ShapeSpec` per `TableShape`, indexed by `+shape`.
    struct TableLayoutSpec {
        Array<ShapeSpec, kTableShapeCount> shapes;

        [[nodiscard]] const ShapeSpec& Shape(TableShape shape) const noexcept {
            return shapes[static_cast<size_t>(+shape)];
        }

        [[nodiscard]] ShapeSpec& Shape(TableShape shape) noexcept {
            return shapes[static_cast<size_t>(+shape)];
        }

        [[nodiscard]] bool operator==(const TableLayoutSpec& other) const noexcept = default;
    };

    /// The shape whose zones lay out `shape`: itself, or the one it is `like`; null when the spec does not describe it (or its `like`
    /// is missing or a `like` itself).
    [[nodiscard]] const ShapeSpec* ZonesOf(const TableLayoutSpec& spec, TableShape shape) noexcept;

}  // namespace BECore::Mahjong
