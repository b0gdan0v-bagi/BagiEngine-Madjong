#pragma once

#include "MahjongTableTestSupport.h"

#include <BECore/Renderer/ViewTransform.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIPreview.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TouchRegime.h>

/**
 * What the tests of the touch size share (`MahjongTouchSizeTest`, and the screen tests that run a canvas over the size matrix): the
 * windows of the engine's matrix as the game sees them, a canvas laid out in one of them the way `ValidateOnMatrix` does, the rect of an
 * element turned into view units of the table (so it can be compared with the tiles), the worst-case layout of the table, and the
 * matrix loop with the touch size dropped in the windows where the test says a control is allowed to be small. Header only; free
 * functions, they take the canvas from `MahjongTableTestBase::Canvas`.
 */
namespace BECore::Mahjong::Tests {

    /// One window of `UILayoutMatrix()`.
    struct MatrixWindow {
        eastl::string label;
        float outputWidth = 0.0f;
        float outputHeight = 0.0f;
        Rect safeArea;
        SafeAreaSimulation simulation = SafeAreaSimulation::None;
        /// Output pixels per window unit; the matrix runs at 1.
        float pixelDensity = 1.0f;

        [[nodiscard]] bool HasInsets() const {
            return simulation != SafeAreaSimulation::None;
        }
    };

    /// The engine's touch minimum (`minTouchSize`, window units): what every control of the game is held to in the matrix.
    inline float EngineTouchMinimum() {
        return CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
    }

    /// The table of the matrix as the game fits it: view units to output pixels (scale and bars).
    inline ViewTransform TableFit(const MatrixWindow& window) {
        return ViewTransform::Fit(kViewWidth, kViewHeight, window.outputWidth, window.outputHeight);
    }

    /// The simulation that made `safeArea` for this output size (the matrix keeps only the area).
    inline SafeAreaSimulation SimulationOf(const UIMatrixCase& matrixCase) {
        for (const SafeAreaSimulation kind : {SafeAreaSimulation::TopNotch, SafeAreaSimulation::SideInsets}) {
            const Rect area = UIPreview::SimulatedSafeArea(kind, matrixCase.context.outputWidth, matrixCase.context.outputHeight);
            const Rect& own = matrixCase.context.safeArea;
            if (area.x == own.x && area.y == own.y && area.w == own.w && area.h == own.h) {
                return kind;
            }
        }
        return SafeAreaSimulation::None;
    }

    /// Calls `fn(window)` for every case of `UILayoutMatrix()`: each aspect ratio at each size that fits 4K, with no insets, a notch and
    /// side insets.
    inline void ForEachMatrixWindow(const std::function<void(const MatrixWindow&)>& fn) {
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            MatrixWindow window;
            window.label = matrixCase.label;
            window.outputWidth = matrixCase.context.outputWidth;
            window.outputHeight = matrixCase.context.outputHeight;
            window.safeArea = matrixCase.context.safeArea;
            window.simulation = SimulationOf(matrixCase);
            fn(window);
        }
    }

    /// A landscape phone in the browser, the bars of the browser taken off: `windowWidth` x `windowHeight` window units at `density` output
    /// pixels per unit, so the output is that much larger. `simulation` adds the insets of a notch or of the sides (in output pixels, as
    /// `UIPreview` makes them for the matrix).
    inline MatrixWindow MakePhoneWindow(float windowWidth, float windowHeight, float density, SafeAreaSimulation simulation = SafeAreaSimulation::None) {
        MatrixWindow window;
        window.outputWidth = windowWidth * density;
        window.outputHeight = windowHeight * density;
        window.pixelDensity = density;
        window.simulation = simulation;
        window.safeArea = UIPreview::SimulatedSafeArea(simulation, window.outputWidth, window.outputHeight);
        const char* insets = simulation == SafeAreaSimulation::TopNotch ? " notch" : simulation == SafeAreaSimulation::SideInsets ? " side insets" : "";
        window.label = Format("phone {}x{} @{}{}", static_cast<int>(windowWidth), static_cast<int>(windowHeight), density, insets);
        return window;
    }

    /// The landscape phones the table is held to: the two phone windows of the entry (915 x 330, the worst, 780 x 300), a taller one
    /// (851 x 393) and 915 x 330 with the insets of the sides, all at density 2.6.
    inline const std::vector<MatrixWindow>& PhoneWindows() {
        constexpr float kPhoneDensity = 2.6f;
        static const std::vector<MatrixWindow> windows{MakePhoneWindow(915.0f, 330.0f, kPhoneDensity), MakePhoneWindow(780.0f, 300.0f, kPhoneDensity), MakePhoneWindow(851.0f, 393.0f, kPhoneDensity),
                                                       MakePhoneWindow(915.0f, 330.0f, kPhoneDensity, SafeAreaSimulation::SideInsets),
                                                       MakePhoneWindow(780.0f, 300.0f, kPhoneDensity, SafeAreaSimulation::TopNotch)};
        return windows;
    }

    /// Calls `fn(window)` for every window the table is checked in: the matrix of the engine, then the landscape phones. The rule
    /// `TableProfileFor` gives the phone profile to some windows of the matrix too (the short ones), so a check of the table takes the
    /// layout of its window (`TableLayoutFor`).
    inline void ForEachTableWindow(const std::function<void(const MatrixWindow&)>& fn) {
        ForEachMatrixWindow(fn);
        for (const MatrixWindow& window : PhoneWindows()) {
            fn(window);
        }
    }

    /// The profile of the table the component draws in this window (`TableProfileFor`, with the engine's touch minimum).
    inline TableProfile TableProfileOf(const MatrixWindow& window) {
        return TableProfileFor(window.outputWidth, window.outputHeight, window.pixelDensity, CoreManager::GetUIEnvironment().GetSettings().minTouchSize);
    }

    /// The layout of the table the component draws for this input in this window: the profile the rule gives the window, the fixed view.
    inline TableLayout TableLayoutFor(const MatrixWindow& window, TableInput input) {
        input.profile = TableProfileOf(window);
        return MakeTableLayout(input);
    }

    /// The layout context `ValidateOnMatrix` builds for a window of the matrix, for a scene whose view is the table.
    inline UILayoutContext ContextOf(const MatrixWindow& window, float userScale = 1.0f) {
        UILayoutContext context;
        context.outputWidth = window.outputWidth;
        context.outputHeight = window.outputHeight;
        context.safeArea = window.safeArea;
        context.userScale = userScale;
        context.pixelDensity = window.pixelDensity;
        context.sceneViewWidth = kViewWidth;
        context.sceneViewHeight = kViewHeight;
        return context;
    }

    /// Lays the canvas out in the window as the game does (the scene's view size, the density of the window) in the scale mode and at
    /// the user scale given (a `SceneView` canvas ignores both), and returns where it is. The rects of the elements are in canvas
    /// units afterwards; the canvas keeps the scale mode until its next `UpdateLayout`, which takes it from the file again.
    inline UICanvasResolved LayOutInWindow(UICanvasComponent& canvas, const MatrixWindow& window, CanvasScaleMode mode = CanvasScaleMode::ScaleWithScreen, float userScale = 1.0f) {
        canvas.UpdateLayout(kViewWidth, kViewHeight);  // the reflected fields of the component reach the layout on its first update
        UICanvasLayout& layout = canvas.GetLayout();
        if (layout.params.space == CanvasSpace::Screen) {
            layout.params.scaleMode = mode;
        }
        layout.Update(ContextOf(window, userScale));
        return layout.GetResolved();
    }

    /// A rect of an element of the canvas (canvas units, from the last `LayOutInWindow`) in view units of the table: canvas units to
    /// output pixels by the canvas's own transform (the scale of a `Screen` canvas, the fit of a `SceneView` one), then back through
    /// the fit of the table. For a `SceneView` canvas the rect is its own.
    inline Rect ToView(const Rect& rect, const UICanvasComponent& canvas, const MatrixWindow& window) {
        const Rect output = canvas.GetLayout().GetResolved().transform.ToOutput(rect.x, rect.y, rect.w, rect.h);
        const ViewTransform fit = TableFit(window);
        return {fit.ToViewX(output.x), fit.ToViewY(output.y), output.w / fit.scale, output.h / fit.scale};
    }

    /// Whether the buttons of `Actions` are under the engine's touch minimum in this window and the rule shows `ActionsCompact` (at this
    /// user scale: the row is as high as the canvas it lies in makes it, and the user scale makes the canvas smaller).
    inline bool IsCompactWindow(const MatrixWindow& window, float userScale = 1.0f) {
        return NeedsCompactActions(window.outputWidth, window.outputHeight, window.pixelDensity, EngineTouchMinimum(), userScale);
    }

    /// Whether the buttons of `Actions` are under the engine's touch minimum in this window, whatever canvas the rule shows.
    inline bool ActionButtonsAreUnderTheMinimum(const MatrixWindow& window) {
        return kActionButtonHeight * TableFit(window).scale / window.pixelDensity < EngineTouchMinimum();
    }

    /// Whether `Actions` is the canvas shown in this window at this user scale with buttons under the engine's touch minimum: the rule keeps
    /// it because the bar under the table cannot hold the compact row. In the matrix that is 640 x 480 at both user scales (28 px) and
    /// 720 x 480 at 1.5 (31.5 px; at 1.0 it is compact).
    inline bool ActionsStaysSmall(const MatrixWindow& window, float userScale) {
        return ActionButtonsAreUnderTheMinimum(window) && !IsCompactWindow(window, userScale);
    }

    /// Where a matrix test may leave the touch size of `Actions` out. The matrix test runs a `SceneView` canvas once per window, whatever
    /// the user scale, so it is every window where `Actions` may be shown small at either of the two user scales: the windows that show
    /// `ActionsCompact` at 1.0, and the windows 640 x 480 (both scales) and 720 x 480 (at 1.5) that keep `Actions` for lack of a bar for the
    /// row (`MahjongTouchSizeTest` holds that these are the only ones).
    inline bool ActionsMayBeSmall(const MatrixWindow& window) {
        return IsCompactWindow(window) || ActionsStaysSmall(window, 1.0f) || ActionsStaysSmall(window, 1.5f);
    }

    /// The fullest turn (a win, a declaration, three kongs and the upgrade) and the fullest claim window (all claims, three chows,
    /// pass): the buttons the action canvases have to hold at once.
    inline const std::vector<std::vector<ButtonKind>>& FullestButtonSets() {
        static const std::vector<std::vector<ButtonKind>> sets{
            {ButtonKind::Win, ButtonKind::Declare, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2, ButtonKind::UpgradeKong},
            {ButtonKind::ClaimMahjong, ButtonKind::ClaimKong, ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2, ButtonKind::Pass}};
        return sets;
    }

    /// Sets the size of the window the fixture's `RecordingRenderer` stands for (before the draw that should see it).
    inline void SetOutputSize(RecordingRenderer& renderer, float width, float height) {
        renderer.width = width;
        renderer.height = height;
    }

    /// Puts the fixture's renderer and the density of the engine's UI environment where `window` has them (the size and the pixel density
    /// the table's rule reads); the fixture gives the density back in `TearDown`.
    inline void SetWindow(RecordingRenderer& renderer, const MatrixWindow& window) {
        SetOutputSize(renderer, window.outputWidth, window.outputHeight);
        CoreManager::GetUIEnvironment().SetPixelDensityOverride(window.pixelDensity);
    }

    /// The middle of an element of a canvas in output pixels, as the last layout of the canvas puts it (a click point).
    inline std::pair<float, float> OutputCenterOf(const UICanvasComponent& canvas, const UIElement& element) {
        const Rect output = canvas.GetLayout().GetResolved().transform.ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h);
        return {output.x + output.w * 0.5f, output.y + output.h * 0.5f};
    }

    /// The input of the table with every zone at its capacity (14 concealed tiles, four kongs and 8 bonus tiles a seat, 100 discards, the
    /// 144-tile wall). `viewer` is the seat at the bottom (the zones are the same, the seats turn).
    inline TableInput WorstCaseTableInput(Seat viewer = 0) {
        constexpr int kMostDiscards = 100;  // longer than any hand leaves (136 tiles, 52 in the hands at the least)
        TableInput worst;
        worst.viewer = viewer;
        worst.concealedCount.fill(kMaxConcealedTiles);
        worst.bonusCount.fill(kMaxBonusTiles);
        Meld kong;
        kong.kind = MeldKind::Kong;
        kong.first = T("1p");
        for (auto& melds : worst.melds) {
            melds.assign(kMaxMelds, kong);
        }
        for (int i = 0; i < kMostDiscards; ++i) {
            worst.discardSeats.push_back(i % kSeatCount);
        }
        worst.wallRemaining = 2 * kWallRingCells;
        return worst;
    }

    /// The table with every zone at its capacity in the desktop profile, so no tile of any desktop game lies where this does not.
    inline TableLayout MakeWorstCaseTableLayout(Seat viewer = 0) {
        return MakeTableLayout(WorstCaseTableInput(viewer));
    }

    /// The table with every zone at its capacity as the component draws it in this window (the profile the rule gives the window).
    inline TableLayout MakeWorstCaseTableLayout(Seat viewer, const MatrixWindow& window) {
        return TableLayoutFor(window, WorstCaseTableInput(viewer));
    }

    /// Whether two rects share an area; rects that only touch are apart.
    inline bool Intersects(const Rect& a, const Rect& b) {
        return !(a.x + a.w <= b.x || a.x >= b.x + b.w || a.y + a.h <= b.y || a.y >= b.y + b.h);
    }

    /// The first slot of the layout the rect (view units) covers, null when it covers none.
    inline const TileSlot* FirstSlotUnder(const Rect& rect, const TableLayout& layout) {
        for (const TileSlot& slot : layout.slots) {
            if (Intersects(rect, slot.rect)) {
                return &slot;
            }
        }
        return nullptr;
    }

    /// No slot of the layout intersects the rect (view units); `label` names the window and the element in the message.
    inline void ExpectClearOfTable(const Rect& rect, const TableLayout& layout, const char* label) {
        const TileSlot* slot = FirstSlotUnder(rect, layout);
        EXPECT_EQ(slot, nullptr) << label << ": the rect (" << rect.x << ", " << rect.y << ", " << rect.w << " x " << rect.h << ") covers the slot of role "
                                 << (slot != nullptr ? static_cast<int>(slot->role) : -1) << " seat " << (slot != nullptr ? slot->seat : -1) << " index " << (slot != nullptr ? slot->index : -1);
    }

    /// `ValidateOnMatrix` on the canvas, with the `TouchTargetTooSmall` issues dropped in the windows where `skip(window)` is true (the
    /// failure labels of the engine helper cannot be filtered, so the loop over `UILayoutMatrix()` is written out here). A `Screen`
    /// canvas runs with each of the three scale modes and the user scales 1.0 and 1.5, a `SceneView` canvas once, as the engine's helper
    /// does. Returns the failures, empty when the canvas passes.
    inline eastl::vector<UIMatrixFailure> MatrixFailuresExceptTouchWhere(UICanvasComponent& canvas, float minTouchSize, const std::function<bool(const MatrixWindow&)>& skip) {
        canvas.UpdateLayout(kViewWidth, kViewHeight);
        UICanvasLayout& layout = canvas.GetLayout();
        const UICanvasScaleParams saved = layout.params;
        const bool screen = saved.space == CanvasSpace::Screen;
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        const float userScales[] = {1.0f, 1.5f};

        eastl::vector<UIMatrixFailure> failures;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            const bool dropTouch = skip(window);
            for (const CanvasScaleMode mode : modes) {
                for (const float userScale : userScales) {
                    if (!screen && (mode != modes[0] || userScale != userScales[0])) {
                        continue;
                    }
                    layout.params = saved;
                    if (screen) {
                        layout.params.scaleMode = mode;
                    }
                    layout.Update(ContextOf(window, userScale));

                    UILayoutValidatorOptions options;
                    options.minTouchSize = minTouchSize;
                    options.pixelDensity = window.pixelDensity;
                    eastl::vector<UILayoutIssue> issues = layout.Validate(options);
                    if (dropTouch) {
                        issues.erase(eastl::remove_if(issues.begin(), issues.end(), [](const UILayoutIssue& issue) { return issue.kind == UILayoutIssueKind::TouchTargetTooSmall; }), issues.end());
                    }
                    if (!issues.empty()) {
                        const eastl::string_view modeName = screen ? EnumUtils<CanvasScaleMode>::ToString(mode) : eastl::string_view("-");
                        failures.push_back({Format("{} {} x{}", window.label, modeName, userScale), std::move(issues)});
                    }
                }
            }
        });
        layout.params = saved;
        layout.Invalidate();
        return failures;
    }

    /// Every label and button of the canvas, shown or not, in tree order.
    inline void CollectTextControls(const UIElement& element, std::vector<const UIElement*>& found) {
        if (element.Cast<UILabel>() != nullptr || element.Cast<UIButton>() != nullptr) {
            found.push_back(&element);
        }
        for (const auto& child : element.children) {
            CollectTextControls(*child, found);
        }
    }

    /// The words of the canvas on the whole matrix: `ValidateOnMatrix` does not flag a text that can shrink (`AutoFit`, ellipsis), so
    /// this asks every shown label and button on the text it shows, with `air` units kept free each side of an interactive one, in every
    /// window, scale mode and user scale of the matrix (a `SceneView` canvas once per window). Returns a line per control that cuts its
    /// text, empty when every word fits.
    inline std::vector<std::string> WordsThatDoNotFitOnTheMatrix(UICanvasComponent& canvas, float air) {
        canvas.UpdateLayout(kViewWidth, kViewHeight);
        UICanvasLayout& layout = canvas.GetLayout();
        const UICanvasScaleParams saved = layout.params;
        const bool screen = saved.space == CanvasSpace::Screen;
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        const float userScales[] = {1.0f, 1.5f};

        std::vector<std::string> cut;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            for (const CanvasScaleMode mode : modes) {
                for (const float userScale : userScales) {
                    if (!screen && (mode != modes[0] || userScale != userScales[0])) {
                        continue;
                    }
                    layout.params = saved;
                    if (screen) {
                        layout.params.scaleMode = mode;
                    }
                    layout.Update(ContextOf(window, userScale));
                    const float scale = layout.GetResolved().scale;
                    std::vector<const UIElement*> controls;
                    CollectTextControls(canvas.GetRoot(), controls);
                    for (const UIElement* control : controls) {
                        if (control->rect.IsEmpty()) {
                            continue;  // hidden
                        }
                        const float spare = control->interactive ? air : 0.0f;
                        const UIContentFit fit = control->CheckContent(control->rect.w - 2.0f * spare, control->rect.h, scale);
                        if (fit.hasContent && !fit.fits) {
                            const eastl::string_view modeName = screen ? EnumUtils<CanvasScaleMode>::ToString(mode) : eastl::string_view("-");
                            cut.push_back(std::string(
                                Format("{} {} x{}: {} ({} x {}) scale {}", window.label, modeName, userScale, control->name.ToStringView(), control->rect.w, control->rect.h, scale).c_str()));
                        }
                    }
                }
            }
        });
        layout.params = saved;
        layout.Invalidate();
        return cut;
    }

    /// `MatrixFailuresExceptTouchWhere` as an expectation, with the name of the node in the message.
    inline void ExpectMatrixExceptTouchWhere(UICanvasComponent& canvas, const char* node, float minTouchSize, const std::function<bool(const MatrixWindow&)>& skip) {
        const auto failures = MatrixFailuresExceptTouchWhere(canvas, minTouchSize, skip);
        EXPECT_TRUE(failures.empty()) << node << ": " << DescribeMatrixFailures(failures).c_str();
    }

}  // namespace BECore::Mahjong::Tests
