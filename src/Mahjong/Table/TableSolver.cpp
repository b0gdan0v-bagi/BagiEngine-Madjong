#include "TableSolver.h"

#include <cmath>

namespace BECore::Mahjong {

    namespace {

        /// A length the solvers can work with: a finite number over 0.
        bool IsUsable(float length) noexcept {
            return length > 0.0f && std::isfinite(length);
        }

        /// The largest integer under `kSolverMaxTileW` that is at most `value`; 0 for anything under 1 or not a number.
        int FloorWidth(float value) noexcept {
            if (!(value >= 1.0f)) {
                return 0;
            }
            return value >= static_cast<float>(kSolverMaxTileW) ? kSolverMaxTileW : static_cast<int>(std::floor(value));
        }

        /// The largest `w` with `tiles * w + gaps * kMeldGap <= along` and a tile height of at most `across`.
        int LargestStripTile(float along, float across, int tiles, int gaps) noexcept {
            if (tiles < 1 || !IsUsable(along) || !IsUsable(across)) {
                return 0;
            }
            const int bound = FloorWidth((along - static_cast<float>(eastl::max(gaps, 0)) * kMeldGap) / static_cast<float>(tiles));
            if (bound < 1) {
                return 0;
            }
            // 3 / 4 of the height is the width the proportions give; the rounding of the height moves it by at most one
            int width = eastl::min(bound, FloorWidth(across * 3.0f / 4.0f));
            while (width < bound && static_cast<float>(TileHeightOf(width + 1)) <= across) {
                ++width;
            }
            while (width > 0 && static_cast<float>(TileHeightOf(width)) > across) {
                --width;
            }
            return width;
        }

        /// How far the tile lies from the start of a span of `room` for the alignment (`room` is never negative).
        float AlignedOffset(ZoneAlign align, float room) noexcept {
            switch (align) {
                case ZoneAlign::Center:
                    return room / 2.0f;
                case ZoneAlign::End:
                    return room;
                default:
                    return 0.0f;
            }
        }

        float Snap(float value) noexcept {
            return std::floor(value / kZoneSnapGrid + 0.5f) * kZoneSnapGrid;
        }

    }  // namespace

    Rect SnapZone(const ZoneRect& zone, float designWidth, float designHeight) noexcept {
        if (!std::isfinite(zone.x) || !std::isfinite(zone.y) || !std::isfinite(zone.w) || !std::isfinite(zone.h) || !std::isfinite(designWidth) || !std::isfinite(designHeight) || zone.w < 0.0f ||
            zone.h < 0.0f) {
            return Rect();
        }
        const float left = Snap(zone.x * designWidth);
        const float top = Snap(zone.y * designHeight);
        const Rect snapped(left, top, Snap((zone.x + zone.w) * designWidth) - left, Snap((zone.y + zone.h) * designHeight) - top);
        return std::isfinite(snapped.x) && std::isfinite(snapped.y) && std::isfinite(snapped.w) && std::isfinite(snapped.h) ? snapped : Rect();
    }

    SolvedStrip SolveStrip(const Rect& zone, ZoneAlign align, bool vertical, int tiles, int gaps) noexcept {
        SolvedStrip strip;
        strip.vertical = vertical;
        const int width = vertical ? LargestStripTile(zone.h, zone.w, tiles, gaps) : LargestStripTile(zone.w, zone.h, tiles, gaps);
        if (width < 1) {
            return strip;
        }
        strip.tileW = static_cast<float>(width);
        strip.tileH = static_cast<float>(TileHeightOf(width));
        strip.slotW = vertical ? strip.tileH : strip.tileW;
        strip.slotH = vertical ? strip.tileW : strip.tileH;
        strip.x = zone.x;
        strip.y = zone.y;
        if (vertical) {
            strip.x += AlignedOffset(align, zone.w - strip.slotW);
        } else {
            strip.y += AlignedOffset(align, zone.h - strip.slotH);
        }
        strip.capacity = vertical ? zone.h : zone.w;
        return strip;
    }

    SolvedStrip PlaceStrip(const Rect& zone, ZoneAlign align, float tileW, float tileH) noexcept {
        SolvedStrip strip;
        if (!IsUsable(tileW) || !IsUsable(tileH) || !IsUsable(zone.w) || !IsUsable(zone.h)) {
            return strip;
        }
        strip.tileW = tileW;
        strip.tileH = tileH;
        strip.slotW = tileW;
        strip.slotH = tileH;
        strip.x = zone.x;
        strip.y = zone.y + AlignedOffset(align, eastl::max(zone.h - tileH, 0.0f));
        strip.capacity = zone.w;
        return strip;
    }

    SolvedBlock SolveDiscardBlock(const Rect& zone, bool vertical, int rows) noexcept {
        SolvedBlock block;
        if (rows < 1 || !IsUsable(zone.w) || !IsUsable(zone.h)) {
            return block;
        }
        // A row of `kDiscardsPerRow` tiles runs along the width (along the height for the turned block of a side seat) and the rows lie
        // across it, each a tile's height thick.
        const float along = vertical ? zone.h : zone.w;
        const float across = vertical ? zone.w : zone.h;
        int width = FloorWidth(along / static_cast<float>(kDiscardsPerRow));
        while (width > 0 && static_cast<float>(TileHeightOf(width)) * static_cast<float>(rows) > across) {
            --width;
        }
        if (width < 1) {
            return block;
        }
        block.tileW = static_cast<float>(width);
        block.tileH = static_cast<float>(TileHeightOf(width));
        block.rows = rows;
        block.x = zone.x;
        block.y = zone.y;
        const float rowLength = block.tileW * static_cast<float>(kDiscardsPerRow);
        const float thickness = block.tileH * static_cast<float>(rows);
        block.blockW = vertical ? thickness : rowLength;
        block.blockH = vertical ? rowLength : thickness;
        return block;
    }

    SolvedWall SolveWall(const Rect& zone) noexcept {
        SolvedWall wall;
        if (!IsUsable(zone.w) || !IsUsable(zone.h)) {
            return wall;
        }
        const int stack = eastl::min(FloorWidth(zone.w / static_cast<float>(kWallRingColumns)), FloorWidth(zone.h / static_cast<float>(kWallRingRows)));
        if (stack < 1) {
            return wall;
        }
        wall.stack = static_cast<float>(stack);
        wall.x = zone.x;
        wall.y = zone.y;
        wall.w = wall.stack * static_cast<float>(kWallRingColumns);
        wall.h = wall.stack * static_cast<float>(kWallRingRows);
        return wall;
    }

}  // namespace BECore::Mahjong
