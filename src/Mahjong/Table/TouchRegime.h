#pragma once

#include <Mahjong/Table/TableLayout.h>
#include <cmath>

namespace BECore::Mahjong {

    /// The height of an action button of the canvas `Actions` in view units; the one place (`config/SceneConfig.xml` repeats it and
    /// `MahjongTableScreenTest` compares the two). `Actions` is a `SceneView` canvas, so the table's fit scales it with the table.
    inline constexpr float kActionButtonHeight = 56.0f;

    /// The scale that fits the view of the table (kViewWidth x kViewHeight) into an output of the given size, the bars left over;
    /// 0 for a size that is not positive. The same number as `ViewTransform::Fit`, which scales a `SceneView` canvas.
    [[nodiscard]] constexpr float FitScale(float outputWidth, float outputHeight) noexcept {
        if (!(outputWidth > 0.0f) || !(outputHeight > 0.0f)) {
            return 0.0f;
        }
        const float scaleX = outputWidth / kViewWidth;
        const float scaleY = outputHeight / kViewHeight;
        return scaleX < scaleY ? scaleX : scaleY;
    }

    /// The scale settings of the canvas `ActionsCompact` (`ScaleWithScreen`, `match` 0.5): the file repeats them and
    /// `MahjongTableScreenTest` compares the two, so `CompactScale` is the scale the canvas really gets.
    inline constexpr float kCompactReferenceWidth = 1280.0f;
    inline constexpr float kCompactReferenceHeight = 720.0f;
    inline constexpr float kCompactMinScale = 0.6f;

    /// The scale (output pixels per canvas unit) of `ActionsCompact` in an output of this size: the geometric mean of the two ratios to the
    /// reference size (the engine's `ScaleWithScreen` at `match` 0.5) times the user scale, at least `kCompactMinScale`. A user scale that is
    /// not positive counts as 1, as the engine's scaler does.
    [[nodiscard]] inline float CompactScale(float outputWidth, float outputHeight, float userScale = 1.0f) noexcept {
        const float user = userScale > 0.0f ? userScale : 1.0f;
        if (!(outputWidth > 0.0f) || !(outputHeight > 0.0f)) {
            return kCompactMinScale;
        }
        const float scale = std::sqrt((outputWidth / kCompactReferenceWidth) * (outputHeight / kCompactReferenceHeight)) * user;
        return scale > kCompactMinScale ? scale : kCompactMinScale;
    }

    /// The height in output pixels of the bar the table leaves under itself in an output of this size (the table is centred, so the bar
    /// above is as high); 0 where the table fills the height and for a size that is not positive. The safe area is not taken off.
    [[nodiscard]] constexpr float BarUnderTable(float outputWidth, float outputHeight) noexcept {
        const float fit = FitScale(outputWidth, outputHeight);
        if (!(fit > 0.0f)) {
            return 0.0f;
        }
        const float bar = (outputHeight - kViewHeight * fit) * 0.5f;
        return bar > 0.0f ? bar : 0.0f;
    }

    /**
     * Whether `ActionsCompact` is the canvas of the action buttons in an output of this size: a button of `Actions` (`kActionButtonHeight`
     * view units scaled by the fit of the table, over the pixel density) would be smaller than the touch minimum
     * (`UISettings::minTouchSize`, in window units) **and** the bar under the table can hold one line of the compact row
     * (`kActionButtonHeight * CompactScale`). The row lies at the bottom of the window, so without such a bar (a window as high as the table:
     * every 16:9 window below 731.4 x 411.4, a short wide one, a landscape phone) it would lie on the viewer's hand and take the click from
     * its tiles; there `Actions` stays, in the free corner of the table, slightly under the minimum but clear of the hand. The bar is taken
     * without the safe area's inset, so a window with insets is judged like one without. False for an output of no size or a density that is
     * not positive, so the first frame, before anything was drawn, shows `Actions`. The component and the tests use this one rule.
     */
    [[nodiscard]] inline bool NeedsCompactActions(float outputWidth, float outputHeight, float pixelDensity, float minTouchSize, float userScale = 1.0f) noexcept {
        const float fit = FitScale(outputWidth, outputHeight);
        if (!(fit > 0.0f) || !(pixelDensity > 0.0f)) {
            return false;
        }
        if (!(kActionButtonHeight * fit / pixelDensity < minTouchSize)) {
            return false;
        }
        return BarUnderTable(outputWidth, outputHeight) >= kActionButtonHeight * CompactScale(outputWidth, outputHeight, userScale);
    }

}  // namespace BECore::Mahjong
