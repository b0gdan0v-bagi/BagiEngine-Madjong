#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/Rect.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/UiPalette.h>
#include <gtest/gtest.h>

/**
 * What the tests of the look share (`Table/UiPalette.h`): a colour compared field by field, the colour of a role of the palette by its
 * name, the gap between two rects one above the other, rects that are apart and the plates the table paints under the zones.
 */
namespace BECore::Mahjong::Tests {

    inline bool SameColor(const Color& a, const Color& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    /// The colour of a role of the palette (an entry of `kUiPaletteNames`: `surface`, `header`, `textMuted`, `accent`, ...).
    inline Color PaletteColor(const char* role) {
        for (int i = 0; i < kUiPaletteSize; ++i) {
            if (eastl::string_view(kUiPaletteNames[i]) == role) {
                return kUiPalette[i];
            }
        }
        ADD_FAILURE() << "no role " << role;
        return {};
    }

    /// The gap between two rects one above the other (negative when they overlap).
    inline float GapBelow(const Rect& upper, const Rect& lower) {
        return lower.y - (upper.y + upper.h);
    }

    /// Whether two rects share no area (touching edges are apart).
    inline bool Apart(const Rect& a, const Rect& b) {
        return a.x + a.w <= b.x || a.x >= b.x + b.w || a.y + a.h <= b.y || a.y >= b.y + b.h;
    }

    /// `rect` grown by `margin` on every side.
    inline Rect Grown(const Rect& rect, float margin) {
        return Rect(rect.x - margin, rect.y - margin, rect.w + 2.0f * margin, rect.h + 2.0f * margin);
    }

    /// Where the table paints the plates (`kZonePlate`) under the discard blocks and the wall ring: the zones grown by `kPlatePadding`.
    inline eastl::vector<Rect> ZonePlates(const TableLayout& layout) {
        eastl::vector<Rect> plates;
        for (const Rect& zone : layout.zones.discards) {
            plates.push_back(Grown(zone, kPlatePadding));
        }
        plates.push_back(Grown(layout.zones.wall, kPlatePadding));
        return plates;
    }

}  // namespace BECore::Mahjong::Tests
