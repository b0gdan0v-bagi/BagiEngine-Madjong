#pragma once

#include <Mahjong/Table/TableLayout.h>

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

    /**
     * Whether a button of `Actions` would be smaller than the touch minimum (`UISettings::minTouchSize`, in window units) in an output of
     * this size: the canvas `ActionsCompact` is shown instead of `Actions` exactly then. The size of a button as seen is
     * `kActionButtonHeight * FitScale / pixelDensity`. False for an output of no size or a density that is not positive, so the first
     * frame, before anything was drawn, shows `Actions`. The component and the tests use this one rule.
     */
    [[nodiscard]] constexpr bool NeedsCompactActions(float outputWidth, float outputHeight, float pixelDensity, float minTouchSize) noexcept {
        const float fit = FitScale(outputWidth, outputHeight);
        if (!(fit > 0.0f) || !(pixelDensity > 0.0f)) {
            return false;
        }
        return kActionButtonHeight * fit / pixelDensity < minTouchSize;
    }

}  // namespace BECore::Mahjong
