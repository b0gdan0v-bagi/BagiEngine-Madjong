#pragma once

// The includes below are kept on purpose so that this header stands alone: do not strip them.
#include <BECore/Math/Color.h>
#include <BECore/Math/Rect.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/UiPalette.h>
#include <algorithm>
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

    // ----- the checks of a theme: marks apart from the felt, text legible on its plate -----

    /// The least Euclidean RGB distance (0..441) between a mark laid over the felt, or over its rim, and the felt itself. The weakest mark of `classic` (`kTurnMark`, a thin
    /// gold over the green) is about 93 from the felt and 106 from the rim, so 60 leaves a margin and still finds a felt that has drifted to the mark's colour.
    inline constexpr float kMinMarkDistance = 60.0f;

    /// WCAG AA for normal text.
    inline constexpr float kMinTextContrast = 4.5f;

    struct Mark {
        const char* name;
        Color color;
    };

    /// Every mark of the table that lies over the felt, the rim or the plates (`UiPalette.h`): the gold family, the wind bars, the danger marks and the dealer's red.
    inline Vector<Mark> TableMarks() {
        return {{"kTurnMark", kTurnMark},
                {"kPendingFrame", kPendingFrame},
                {"kLastDiscardMark", kLastDiscardMark},
                {"kDrawnTileMark", kDrawnTileMark},
                {"kClaimedEdge", kClaimedEdge},
                {"kClaimHighlight", kClaimHighlight},
                {"kClaimLostFrame", kClaimLostFrame},
                {"kCallingFrame", kCallingFrame},
                {"kKeepsWaitingMark", kKeepsWaitingMark},
                {"kSeatWindMark", kSeatWindMark},
                {"kRoundWindMark", kRoundWindMark},
                {"kSafeMark", kSafeMark},
                {"kRiskyMark", kRiskyMark},
                {"kDangerMark", kDangerMark},
                {"kDeclareMark", kDeclareMark},
                {"kAdvisedFrame", kAdvisedFrame},
                {"kSeatTurnFrame", kSeatTurnFrame},
                {"kDealerMark", kDealerMark},
                {"kPanelTurnLight", kPanelTurnLight},
                {"kPanelDealerMark", kPanelDealerMark}};
    }

    /// `top` laid over the opaque `bottom`, as an opaque colour.
    inline Color Over(const Color& top, const Color& bottom) {
        const auto channel = [&](unsigned char a, unsigned char b) {
            const float alpha = static_cast<float>(top.a) / 255.0f;
            return static_cast<unsigned char>(std::lround(static_cast<float>(a) * alpha + static_cast<float>(b) * (1.0f - alpha)));
        };
        return Color(channel(top.r, bottom.r), channel(top.g, bottom.g), channel(top.b, bottom.b), 255);
    }

    inline float Distance(const Color& a, const Color& b) {
        const float dr = static_cast<float>(a.r) - static_cast<float>(b.r);
        const float dg = static_cast<float>(a.g) - static_cast<float>(b.g);
        const float db = static_cast<float>(a.b) - static_cast<float>(b.b);
        return std::sqrt(dr * dr + dg * dg + db * db);
    }

    inline float Luminance(const Color& color) {
        const auto linear = [](unsigned char channel) {
            const float value = static_cast<float>(channel) / 255.0f;
            return value <= 0.03928f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        };
        return 0.2126f * linear(color.r) + 0.7152f * linear(color.g) + 0.0722f * linear(color.b);
    }

    /// The WCAG contrast ratio of two colours, 1 (the same) to 21 (black on white).
    inline float ContrastRatio(const Color& a, const Color& b) {
        const float first = Luminance(a);
        const float second = Luminance(b);
        return (std::max(first, second) + 0.05f) / (std::min(first, second) + 0.05f);
    }

    /// The colour of a token in the current theme; a token nobody defines is `kMissingTokenColor`, which the completeness check reports.
    inline Color Token(const UIThemes& themes, const char* name) {
        return themes.Find(name).value_or(kMissingTokenColor);
    }

    /// A pair of `classic` that was never held to the contrast ratio and falls under it, named `<text role> on <plate role>` (a role with a `Deep` end names the end). `classic` is
    /// not changed here (the owner decides); the pair is named so that it is found again if it is ever fixed, and no other theme has an exception.
    struct KnownPair {
        const char* theme;
        const char* pair;
    };
    inline constexpr Array<KnownPair, 2> kKnownLowContrastPairs{{
        {"classic", "textMuted on primary"},     // 3.49
        {"classic", "textMuted on placeBadge"},  // 4.16
    }};

    inline bool IsKnownLowContrastPair(StringView theme, StringView pair) {
        return std::any_of(kKnownLowContrastPairs.begin(), kKnownLowContrastPairs.end(), [&](const KnownPair& known) { return theme == known.theme && pair == known.pair; });
    }

}  // namespace BECore::Mahjong::Tests
