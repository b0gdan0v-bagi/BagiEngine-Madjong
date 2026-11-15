#pragma once

#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * The rectangles of the table as named regions, in view units: what the table publishes to the canvases of its nodes, so that an
     * element anchors to the table's own places (`region="table.plate.0"`) instead of repeating its numbers. A name is a `RegionName`
     * (no heap); the component interns it.
     *
     * The **zone regions** come from the solved layout and follow every edit of `config/TableLayout.xml` (the plates by screen position,
     * the phone profile's lifted bottom plate included, the centre panel, the wall ring). The **free regions** are authored per shape
     * in the file (`<Free>`) on ground the worst-case table leaves clear; a name once per shape and never a zone name.
     */

    /// The plate at a screen position (0 bottom, 1 right, 2 top, 3 left), not a seat: `zones.seatPlates[SeatAtPosition(viewer, position)]`.
    inline constexpr const char* kPlateRegionNames[kSeatCount] = {"table.plate.0", "table.plate.1", "table.plate.2", "table.plate.3"};
    /// The inside of the wall ring (`zones.centerPanel`) and the ring (`zones.wall`).
    inline constexpr const char* kCenterPanelRegionName = "table.centerPanel";
    inline constexpr const char* kWallRegionName = "table.wall";

    /// The free regions every shape of `config/TableLayout.xml` carries; the node files of the table name them with `region="..."`. The
    /// place of each is the file's, per shape.
    inline constexpr const char* kMoveLineRegionName = "table.moveLine";          // the last move in words (Hud)
    inline constexpr const char* kSelectedTileRegionName = "table.selectedTile";  // the name of the tile selected by a tap (Hud)
    inline constexpr const char* kHintRegionName = "table.hint";                  // the sentence of the button under the pointer (Actions)
    inline constexpr const char* kActionsRegionName = "table.actions";            // the rows of action buttons (Actions)
    inline constexpr const char* kWaitingRegionName = "table.waiting";            // the waiting tiles of the hints (Hints)
    inline constexpr const char* kHintButtonRegionName = "table.hintButton";      // the button that asks for advice (HintsButton)
    inline constexpr const char* kAdviceRegionName = "table.advice";              // the sentence of the advice (Hints)
    inline constexpr const char* kFreeRegionNames[] = {kMoveLineRegionName, kSelectedTileRegionName, kHintRegionName, kActionsRegionName, kWaitingRegionName, kHintButtonRegionName, kAdviceRegionName};

    /// Whether `name` is the name of a zone region (a free region may not take it).
    [[nodiscard]] bool IsZoneRegionName(StringView name) noexcept;

    /// A region of the table: its name and rectangle in view units.
    struct TableRegion {
        RegionName name;
        Rect rect;
    };

    /// The regions of `layout`: the plates by screen position, the centre panel, the wall ring, then the free regions in the order of the
    /// spec. `regions` keeps its capacity: the table asks every frame.
    void RebuildTableRegions(const TableLayout& layout, Vector<TableRegion>& regions);

    [[nodiscard]] Vector<TableRegion> MakeTableRegions(const TableLayout& layout);

}  // namespace BECore::Mahjong
