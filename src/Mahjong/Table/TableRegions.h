#pragma once

#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * The rectangles of the table as named regions: what the table publishes to the canvases of its nodes, so that an element anchors to
     * the table's own places (`region="table.plate.0"`) instead of repeating its numbers. Pure like the rest of `Table/`: a name is a
     * `RegionName` (no heap), the component interns it, the rectangles are in view units (what a `SceneView` canvas takes).
     *
     * Two kinds. The **zone regions** come from the solved layout, so they follow every change of `config/TableLayout.xml` by themselves:
     * the four plates by screen position (the real rectangle, the phone profile's lifted bottom plate included), the centre panel and the
     * wall ring. The **free regions** are authored in the file per shape (`<Free>`, `ShapeSpec::freeRegions`) on ground that the worst-case
     * table leaves clear; their names are the file's, a name once per shape and not one of the zone names.
     */

    /// The plate at screen position 0 (bottom), 1 (right), 2 (top), 3 (left): `table.plate.<position>`; the region is that of
    /// `zones.seatPlates[SeatAtPosition(viewer, position)]`.
    inline constexpr const char* kPlateRegionNames[kSeatCount] = {"table.plate.0", "table.plate.1", "table.plate.2", "table.plate.3"};
    /// The inside of the wall ring (`zones.centerPanel`) and the ring (`zones.wall`).
    inline constexpr const char* kCenterPanelRegionName = "table.centerPanel";
    inline constexpr const char* kWallRegionName = "table.wall";

    /// Whether `name` is the name of a zone region (a free region may not take it).
    [[nodiscard]] bool IsZoneRegionName(eastl::string_view name) noexcept;

    /// A region of the table: its name and rectangle in view units.
    struct TableRegion {
        RegionName name;
        Rect rect;
    };

    /// The regions of `layout`: the plates by screen position, the centre panel, the wall ring, then the free regions in the order of the spec.
    /// A layout of a spec without free regions gives the zone regions only. `regions` keeps its capacity (the table asks every frame).
    void RebuildTableRegions(const TableLayout& layout, eastl::vector<TableRegion>& regions);

    [[nodiscard]] eastl::vector<TableRegion> MakeTableRegions(const TableLayout& layout);

}  // namespace BECore::Mahjong
