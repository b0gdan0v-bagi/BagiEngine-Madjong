#pragma once

// The includes below are kept on purpose so that this header stands alone: do not strip them.
#include <BECore/Math/Color.h>
#include <BECore/Math/Rect.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/UiPalette.h>
#include <cmath>
#include <gtest/gtest.h>
#include <tests/ShippedThemes.h>

namespace BECore::Mahjong::Tests {

    inline bool SameColor(const Color& a, const Color& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    /// Two rects equal to within a hundredth of a unit (what a draw call and a layout slot may differ by).
    inline bool SameRect(const Rect& a, const Rect& b) {
        constexpr float kSameRectEpsilon = 0.01f;
        return std::abs(a.x - b.x) < kSameRectEpsilon && std::abs(a.y - b.y) < kSameRectEpsilon && std::abs(a.w - b.w) < kSameRectEpsilon && std::abs(a.h - b.h) < kSameRectEpsilon;
    }

    /// The colour of a role of the shipped `classic` theme (`kUiPaletteNames` and `kTablePaletteNames`: `surface`, `header`, `textMuted`, `accent`, ...). Read from the
    /// file, not from the table of `CoreManager`, so it needs no services; a colour a screen resolved is compared with it after a draw.
    inline Color PaletteColor(const char* role) {
        static const UIThemes classic = [] {
            UIThemes themes;
            const Vector<String> problems = LoadShippedThemes(themes);
            EXPECT_TRUE(problems.empty()) << "config/UIThemes.xml: " << (problems.empty() ? "" : problems.front().c_str());
            return themes;
        }();
        const Optional<Color> color = classic.Find(role);
        if (!color) {
            ADD_FAILURE() << "no role " << role;
            return {};
        }
        return *color;
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

    /// The discard blocks and the wall ring grown by `kPlatePadding`: what the HUD and the seat plates keep clear of (the table paints no plate there).
    inline Vector<Rect> ZonePlates(const TableLayout& layout) {
        Vector<Rect> plates;
        for (const Rect& zone : layout.zones.discards) {
            plates.push_back(Grown(zone, kPlatePadding));
        }
        plates.push_back(Grown(layout.zones.wall, kPlatePadding));
        return plates;
    }

}  // namespace BECore::Mahjong::Tests
