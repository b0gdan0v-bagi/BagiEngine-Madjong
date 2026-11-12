#include "TableRegions.h"

namespace BECore::Mahjong {

    bool IsZoneRegionName(eastl::string_view name) noexcept {
        if (name == kCenterPanelRegionName || name == kWallRegionName) {
            return true;
        }
        for (const char* plate : kPlateRegionNames) {
            if (name == plate) {
                return true;
            }
        }
        return false;
    }

    void RebuildTableRegions(const TableLayout& layout, eastl::vector<TableRegion>& regions) {
        regions.clear();
        for (int position = 0; position < kSeatCount; ++position) {
            regions.push_back(TableRegion{kPlateRegionNames[position], layout.zones.seatPlates[SeatAtPosition(layout.viewer, position)]});
        }
        regions.push_back(TableRegion{kCenterPanelRegionName, layout.zones.centerPanel});
        regions.push_back(TableRegion{kWallRegionName, layout.zones.wall});
        for (const FreeZone& zone : layout.zones.freeRegions) {
            regions.push_back(TableRegion{zone.name.c_str(), zone.rect});
        }
    }

    eastl::vector<TableRegion> MakeTableRegions(const TableLayout& layout) {
        eastl::vector<TableRegion> regions;
        RebuildTableRegions(layout, regions);
        return regions;
    }

}  // namespace BECore::Mahjong
