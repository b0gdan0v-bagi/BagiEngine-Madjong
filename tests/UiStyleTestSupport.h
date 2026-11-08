#pragma once

#include <Mahjong/Table/UiPalette.h>

/**
 * What the tests of the look share (`Table/UiPalette.h`): a colour compared field by field, the colour of a role of the palette by its
 * name, and the gap between two rects one above the other.
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

}  // namespace BECore::Mahjong::Tests
