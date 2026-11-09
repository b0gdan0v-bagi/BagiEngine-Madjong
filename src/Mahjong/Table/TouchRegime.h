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

    /// The numbers of the row of `ActionsCompact` (`config/SceneConfig.xml` repeats them and `MahjongTableScreenTest` compares the two): the
    /// width of a button, the spacing of the row and the buttons of the fullest claim window (mahjong, kong, pung, three chows, pass, and
    /// the hint button `HintButton`, which follows them: eight buttons). The fullest turn (win, three concealed kongs, the upgrade, the hint
    /// button and `Declare`: seven buttons, the last one wider) never needs more lines than the claim window, because `Declare` is the
    /// last element of the row and ends a line; `TouchRegimeTest` wraps both, and every part of them, against the variants.
    /// `kCompactSafeShare` is the part of the canvas that the safe area is at the least (the side insets of the matrix), which a canvas
    /// variant cannot see: it sees the whole canvas.
    inline constexpr float kCompactButtonWidth = 100.0f;
    inline constexpr float kCompactSpacing = 8.0f;
    /// The `Declare` switch of the fullest turn, which its caption and switch need wider than the other buttons.
    inline constexpr float kCompactDeclareWidth = 180.0f;
    inline constexpr int kCompactButtonCount = 8;
    inline constexpr float kCompactSafeShare = 0.82f;

    /// A height variant of the stack `ActionRow`: a canvas up to `maxWidth` units wide gives the row `height` units (the later, narrower
    /// variant wins, as in the file). A stack has the height it is given and starts its lines at its top, so this is how far above the
    /// bottom of the window the row reaches, whatever number of lines the buttons use. Each width is k buttons of the claim window in a
    /// line (`k * kCompactButtonWidth + (k - 1) * kCompactSpacing`) over `kCompactSafeShare`, rounded up to 0.01, and each height is
    /// the lines the canvas below it needs (lines of `kActionButtonHeight` with `kCompactSpacing` between):
    ///  - 1043.91: eight to a line (856) stop fitting: two lines (120) while four to a line (424) fit; above it one line (56);
    ///  - 517.08: four to a line stop fitting: three lines (184) while three to a line (316) fit;
    ///  - 385.37: three to a line stop fitting: four lines (248) while two to a line (208) fit;
    ///  - 253.66: two to a line stop fitting: eight lines (504), one to a line.
    struct CompactRowVariant {
        float maxWidth;
        float height;
    };
    inline constexpr CompactRowVariant kCompactRowVariants[] = {{1043.91f, 120.0f}, {517.08f, 184.0f}, {385.37f, 248.0f}, {253.66f, 504.0f}};

    /// The height in canvas units of the `ActionRow` of `ActionsCompact` in a canvas `canvasWidthUnits` wide: `kActionButtonHeight` (one
    /// line) above the first variant, else the height of the narrowest variant that applies.
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
     * Whether `ActionsCompact` is the canvas of the action buttons in an output of this size: a button of `Actions` (`kActionButtonHeight`
     * view units scaled by the fit of the table, over the pixel density) would be smaller than the touch minimum
     * (`UISettings::minTouchSize`, in window units) **and** the bar under the table holds the compact row at its height there
     * (`CompactRowHeight` of the canvas width, times `CompactScale`). The row lies at the bottom of the window and a stack has the height
     * it is given, so it reaches that far up whatever number of lines its buttons use; without such a bar (a window as high as the table:
     * every 16:9 window below 731.4 x 411.4, a short wide one, a landscape phone; or one where the width makes the row several lines, like
     * 626 x 480, or 640 x 480 and 720 x 480 at the user scale 1.5) it would lie on the viewer's hand and take the click from its
     * tiles, so `Actions` stays, in the free corner of the table, slightly under the minimum but clear of the hand. The bar is taken without the safe area's inset, so a window with insets is judged
     * like one without. False for an output of no size or a density that is not positive, so the first frame, before anything was drawn,
     * shows `Actions`. The component and the tests use this one rule.
     */
    [[nodiscard]] inline bool NeedsCompactActions(float outputWidth, float outputHeight, float pixelDensity, float minTouchSize, float userScale = 1.0f) noexcept {
        const float fit = FitScale(outputWidth, outputHeight);
        if (!(fit > 0.0f) || !(pixelDensity > 0.0f)) {
            return false;
        }
        if (!(kActionButtonHeight * fit / pixelDensity < minTouchSize)) {
            return false;
        }
        const float scale = CompactScale(outputWidth, outputHeight, userScale);
        return BarUnderTable(outputWidth, outputHeight) >= CompactRowHeight(outputWidth / scale) * scale;
    }

}  // namespace BECore::Mahjong
