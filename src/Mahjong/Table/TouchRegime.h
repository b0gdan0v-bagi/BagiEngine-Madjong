#pragma once

#include <Mahjong/Table/TableLayout.h>
#include <cmath>

namespace BECore::Mahjong {

    /// The height of an action button of the canvas `Actions` in view units; `config/nodes/MahjongActions.xml` repeats it
    /// (`MahjongTableScreenTest`). `Actions` is a `SceneView` canvas, so the table's fit scales it with the table.
    inline constexpr float kActionButtonHeight = 56.0f;
    /// Taller in the portrait view: it is only 720 units wide, so a phone scales it by about a half and the button still reaches a
    /// touchable size.
    inline constexpr float kPortraitActionButtonHeight = 68.0f;

    /// The height of an action button of `Actions` in the view of this shape.
    [[nodiscard]] constexpr float ActionButtonHeightFor(TableShape shape) noexcept {
        return shape == TableShape::Portrait ? kPortraitActionButtonHeight : kActionButtonHeight;
    }

    /// The scale that fits the view of the table into an output of the given size, the bars left over; 0 for a size that is not
    /// positive. The same number as `ViewTransform::Fit`, which scales a `SceneView` canvas. Every function below that takes a view size
    /// takes the **active view's** (the default is the landscape one, so a landscape caller passes nothing).
    [[nodiscard]] constexpr float FitScale(float outputWidth, float outputHeight, float viewWidth = kViewWidth, float viewHeight = kViewHeight) noexcept {
        if (!(outputWidth > 0.0f) || !(outputHeight > 0.0f) || !(viewWidth > 0.0f) || !(viewHeight > 0.0f)) {
            return 0.0f;
        }
        const float scaleX = outputWidth / viewWidth;
        const float scaleY = outputHeight / viewHeight;
        return scaleX < scaleY ? scaleX : scaleY;
    }

    /// The scale settings of the canvas `ActionsCompact` (`ScaleWithScreen`, `match` 0.5), which the file repeats
    /// (`MahjongTableScreenTest`), so `CompactScale` is the scale the canvas really gets.
    inline constexpr float kCompactReferenceWidth = 1280.0f;
    inline constexpr float kCompactReferenceHeight = 720.0f;
    inline constexpr float kCompactMinScale = 0.6f;

    /// The scale (output pixels per canvas unit) of `ActionsCompact`: the engine's `ScaleWithScreen` at `match` 0.5 times the user scale,
    /// at least `kCompactMinScale`. A user scale that is not positive counts as 1, as the engine's scaler does.
    [[nodiscard]] inline float CompactScale(float outputWidth, float outputHeight, float userScale = 1.0f) noexcept {
        const float user = userScale > 0.0f ? userScale : 1.0f;
        if (!(outputWidth > 0.0f) || !(outputHeight > 0.0f)) {
            return kCompactMinScale;
        }
        const float scale = std::sqrt((outputWidth / kCompactReferenceWidth) * (outputHeight / kCompactReferenceHeight)) * user;
        return scale > kCompactMinScale ? scale : kCompactMinScale;
    }

    /// The height in output pixels of the bar the table leaves under itself (it is centred, so the bar above is as high); 0 where the
    /// table fills the height. The safe area is not taken off.
    [[nodiscard]] constexpr float BarUnderTable(float outputWidth, float outputHeight, float viewWidth = kViewWidth, float viewHeight = kViewHeight) noexcept {
        const float fit = FitScale(outputWidth, outputHeight, viewWidth, viewHeight);
        if (!(fit > 0.0f)) {
            return 0.0f;
        }
        const float bar = (outputHeight - viewHeight * fit) * 0.5f;
        return bar > 0.0f ? bar : 0.0f;
    }

    /// The numbers of the row of `ActionsCompact`, which `config/nodes/MahjongActionsCompact.xml` repeats (`MahjongTableScreenTest`):
    /// the width of a button, the spacing and the buttons of the fullest claim window (the hint button included). The fullest turn never
    /// needs more lines than the claim window, because `Declare` ends a line. `kCompactSafeShare` is the least part of the canvas that
    /// the safe area leaves, which a canvas variant cannot see: it sees the whole canvas.
    inline constexpr float kCompactButtonWidth = 100.0f;
    inline constexpr float kCompactSpacing = 8.0f;
    /// The `Declare` switch of the fullest turn, which its caption and switch need wider than the other buttons.
    inline constexpr float kCompactDeclareWidth = 180.0f;
    inline constexpr int kCompactButtonCount = 8;
    inline constexpr float kCompactSafeShare = 0.82f;

    /// A height variant of the stack `ActionRow`: a canvas up to `maxWidth` units wide gives the row `height` units (the later, narrower
    /// variant wins, as in the file). A stack has the height it is given and starts its lines at its top, so this is how far above the
    /// bottom of the window the row reaches, whatever number of lines the buttons use. Each width is where k buttons of the claim window
    /// stop fitting in a line, over `kCompactSafeShare`, and each height is the lines the canvas below it needs.
    struct CompactRowVariant {
        float maxWidth;
        float height;
    };
    inline constexpr CompactRowVariant kCompactRowVariants[] = {{1043.91f, 120.0f}, {517.08f, 184.0f}, {385.37f, 248.0f}, {253.66f, 504.0f}};

    /// The height in canvas units of the `ActionRow` in a canvas `canvasWidthUnits` wide: one line above the first variant, else the
    /// narrowest variant that applies.
    [[nodiscard]] constexpr float CompactRowHeight(float canvasWidthUnits) noexcept {
        float height = kActionButtonHeight;
        for (const CompactRowVariant& variant : kCompactRowVariants) {
            if (canvasWidthUnits <= variant.maxWidth) {
                height = variant.height;
            }
        }
        return height;
    }

    /**
     * Whether `ActionsCompact` is the canvas of the action buttons in an output of this size: a button of `Actions` would be under the
     * touch minimum (`UISettings::minTouchSize`, in window units) **and** the bar under the table, less `bottomInset` (the compact row keeps
     * to the safe area; output pixels), holds the compact row at its height there. Without such a bar (a window as high as the table, a row
     * of several lines, side insets that lift it) the row would lie on the viewer's hand and take the click from its tiles, so `Actions`
     * stays, in the free corner of the table, slightly under the minimum but clear of the hand. False for an output of no size or a density
     * that is not positive, so the first frame shows `Actions`. The component and the tests use this one rule; the view size comes before
     * the inset so that a caller that names a view keeps its arguments.
     */
    [[nodiscard]] inline bool NeedsCompactActions(float outputWidth, float outputHeight, float pixelDensity, float minTouchSize, float userScale = 1.0f, float viewWidth = kViewWidth,
                                                  float viewHeight = kViewHeight, float bottomInset = 0.0f) noexcept {
        const float fit = FitScale(outputWidth, outputHeight, viewWidth, viewHeight);
        if (!(fit > 0.0f) || !(pixelDensity > 0.0f)) {
            return false;
        }
        if (!(ActionButtonHeightFor(ShapeOfView(viewWidth, viewHeight)) * fit / pixelDensity < minTouchSize)) {
            return false;
        }
        const float scale = CompactScale(outputWidth, outputHeight, userScale);
        const float inset = bottomInset > 0.0f ? bottomInset : 0.0f;
        return BarUnderTable(outputWidth, outputHeight, viewWidth, viewHeight) - inset >= CompactRowHeight(outputWidth / scale) * scale;
    }

    /**
     * The profile of the table in an output of this size (`TableProfile`): desktop where the desktop hand tile reaches the touch minimum
     * (`UISettings::minTouchSize`, in window units) after the fit of the table and the pixel density, else phone with a hand tile just
     * wide enough to reach it, at most `kPhoneHandTileMaxW` (a window too small for the minimum gets the widest tile, the best it can
     * give). Sizes are output pixels, `pixelDensity` the output pixels per window unit (as in `NeedsCompactActions`). Desktop for an
     * output of no size, so the first frame shows the desktop table, and for a portrait or narrow view, whose layout ignores the profile
     * (`UsesTapToSelect` covers the small tile). The component and the tests use this one rule.
     */
    [[nodiscard]] inline TableProfile TableProfileFor(float outputWidth, float outputHeight, float pixelDensity, float minTouchSize, float viewWidth = kViewWidth,
                                                      float viewHeight = kViewHeight) noexcept {
        const float fit = FitScale(outputWidth, outputHeight, viewWidth, viewHeight);
        if (!IsLandscapeLike(ShapeOfView(viewWidth, viewHeight)) || !(fit > 0.0f) || !(pixelDensity > 0.0f) || !(kViewerHandTileW * fit / pixelDensity < minTouchSize)) {
            return TableProfile{};
        }
        const float wanted = minTouchSize * pixelDensity / fit;
        return TableProfile{true, wanted < kPhoneHandTileMaxW ? wanted : kPhoneHandTileMaxW};
    }

}  // namespace BECore::Mahjong
