#pragma once

#include "MahjongButtonSupport.h"
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

    /// A landscape window (density 1) where the buttons of `Actions` are under the touch minimum and the bar under the table holds the row of
    /// `ActionsCompact`: the tests of the compact canvas stand in it. It is a narrow window: that table fills the width and leaves a bar of the
    /// row's height only in a nearly square one (610 x 600: a button of 31.6 px, a bar of 83 px; the landscape and the wide table cannot, their
    /// windows are 1.55 times as wide as high or more). A square or a tall output is the portrait view.
    inline constexpr float kCompactWindowWidth = 610.0f;
    inline constexpr float kCompactWindowHeight = 600.0f;

    /// The engine's touch minimum (`minTouchSize`, window units): what every control of the game is held to in the matrix.
    inline float EngineTouchMinimum() {
        return CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
    }

    /// The view the scene draws with in the window: the landscape view, the portrait one where the window is at least as tall as wide, or the
    /// wide one where it is twice as wide as high or more (`SceneViewVariants`, picked as `Scene::Draw` does).
    inline ActiveSceneView ViewOf(const MatrixWindow& window) {
        return ViewOfOutput(window.outputWidth, window.outputHeight);
    }

    /// The shape of the table in the window (`ShapeOfView` of its view).
    inline TableShape ShapeOf(const MatrixWindow& window) {
        const ActiveSceneView view = ViewOf(window);
        return ShapeOfView(view.width, view.height);
    }

    /// The window without its insets: the table is a fixed design that the safe area does not move, so a check of where a canvas lies on the
    /// table takes the layout of the design (the elements that keep to the safe area move inwards with insets: a known deviation).
    inline MatrixWindow WithoutInsets(MatrixWindow window) {
        window.simulation = SafeAreaSimulation::None;
        window.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::None, window.outputWidth, window.outputHeight);
        return window;
    }

    /// The table of the matrix as the game fits it: view units to output pixels (scale and bars).
    inline ViewTransform TableFit(const MatrixWindow& window) {
        const ActiveSceneView view = ViewOf(window);
        return ViewTransform::Fit(view.width, view.height, window.outputWidth, window.outputHeight);
    }

    /// The bar under the table in the window, in output pixels (`BarUnderTable` for the view of the window's shape).
    inline float BarUnderTableOf(const MatrixWindow& window) {
        const ActiveSceneView view = ViewOf(window);
        return BarUnderTable(window.outputWidth, window.outputHeight, view.width, view.height);
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

    /// A phone in the browser, the bars of the browser taken off: `windowWidth` x `windowHeight` window units at `density` output
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

    /// The window of the design of a shape: the output is the view (1280 x 720, 720 x 1280 or 1560 x 720) at density 1, no insets.
    inline MatrixWindow DesignWindowOf(TableShape shape) {
        MatrixWindow window;
        window.outputWidth = DesignWidthOf(shape);
        window.outputHeight = DesignHeightOf(shape);
        window.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::None, window.outputWidth, window.outputHeight);
        window.label = Format("design {}x{}", static_cast<int>(window.outputWidth), static_cast<int>(window.outputHeight));
        return window;
    }

    /// The phones the table is held to: the landscape ones (the two phone windows of the entry, 915 x 330, the worst, and 780 x 300, a
    /// taller one, 851 x 393, and 915 x 330 with the insets of the sides, all at density 2.6: every one of them is wider than 2 : 1, so
    /// the wide view), then two portrait ones (390 x 844 at 3 and 360 x 640 at 2). The matrix holds the wide windows of a monitor (21:9
    /// and 32:9 at several sizes, with and without insets).
    inline const std::vector<MatrixWindow>& PhoneWindows() {
        constexpr float kPhoneDensity = 2.6f;
        constexpr float kTallPhoneDensity = 3.0f;
        constexpr float kSmallPhoneDensity = 2.0f;
        static const std::vector<MatrixWindow> windows{MakePhoneWindow(915.0f, 330.0f, kPhoneDensity),
                                                       MakePhoneWindow(780.0f, 300.0f, kPhoneDensity),
                                                       MakePhoneWindow(851.0f, 393.0f, kPhoneDensity),
                                                       MakePhoneWindow(915.0f, 330.0f, kPhoneDensity, SafeAreaSimulation::SideInsets),
                                                       MakePhoneWindow(780.0f, 300.0f, kPhoneDensity, SafeAreaSimulation::TopNotch),
                                                       MakePhoneWindow(390.0f, 844.0f, kTallPhoneDensity),
                                                       MakePhoneWindow(360.0f, 640.0f, kSmallPhoneDensity)};
        return windows;
    }

    /// The windows of the compact row (the matrix has none: the narrow table's buttons are 33 px or more in every one of its windows, and the
    /// portrait table's under 339 px wide ones are outside it): the near-square narrow windows, 610 x 600 with side insets too (the inset
    /// lifts the row, so the rule keeps `Actions` there: the window is here for the tests that name it), and two portrait ones. Density 1.
    inline const std::vector<MatrixWindow>& CompactWindows() {
        static const std::vector<MatrixWindow> windows{MakePhoneWindow(kCompactWindowWidth, kCompactWindowHeight, 1.0f),
                                                       MakePhoneWindow(kCompactWindowWidth, kCompactWindowHeight, 1.0f, SafeAreaSimulation::SideInsets),
                                                       MakePhoneWindow(600.0f, 580.0f, 1.0f),
                                                       MakePhoneWindow(560.0f, 550.0f, 1.0f),
                                                       MakePhoneWindow(300.0f, 800.0f, 1.0f),
                                                       MakePhoneWindow(320.0f, 900.0f, 1.0f, SafeAreaSimulation::SideInsets)};
        return windows;
    }

    /// The narrow windows (4:3, 3:2, 5:4 landscape) the matrix lacks (its own 4:3 and 3:2 windows are in `ForEachMatrixWindow`): at density 1 the
    /// sizes a monitor window or a tablet browser has (1024 x 768, 1080 x 768 the design, 1200 x 800, 800 x 600, 640 x 480, 1500 x 1000 and
    /// 1400 x 1000, a 5:4 monitor) and a tablet 1024 x 768 at density 2. Kept apart from `PhoneWindows` because the loops that run over the
    /// matrix alone (`ForEachMatrixAndNarrowWindow`) visit them too.
    inline const std::vector<MatrixWindow>& NarrowWindows() {
        constexpr float kTabletDensity = 2.0f;
        static const std::vector<MatrixWindow> windows{MakePhoneWindow(1024.0f, 768.0f, 1.0f),  MakePhoneWindow(kNarrowViewWidth, kNarrowViewHeight, 1.0f),
                                                       MakePhoneWindow(1200.0f, 800.0f, 1.0f),  MakePhoneWindow(800.0f, 600.0f, 1.0f),
                                                       MakePhoneWindow(640.0f, 480.0f, 1.0f),   MakePhoneWindow(1500.0f, 1000.0f, 1.0f),
                                                       MakePhoneWindow(1400.0f, 1000.0f, 1.0f), MakePhoneWindow(1024.0f, 768.0f, kTabletDensity)};
        return windows;
    }

    /// Calls `fn(window)` for every window the table is checked in: the matrix of the engine, then the landscape phones, the portrait ones and
    /// the narrow windows. The rule `TableProfileFor` gives the phone profile to some windows of the matrix too (the short ones), so a check of
    /// the table takes the layout of its window (`TableLayoutFor`).
    inline void ForEachTableWindow(const std::function<void(const MatrixWindow&)>& fn) {
        ForEachMatrixWindow(fn);
        for (const MatrixWindow& window : PhoneWindows()) {
            fn(window);
        }
        for (const MatrixWindow& window : NarrowWindows()) {
            fn(window);
        }
    }

    /// Calls `fn(window)` for every case of the matrix and for the narrow windows the matrix lacks: what the loops of the touch size and of the
    /// words (`MatrixFailuresExceptTouchWhere`, `WordsThatDoNotFitOnTheMatrix`) run over.
    inline void ForEachMatrixAndNarrowWindow(const std::function<void(const MatrixWindow&)>& fn) {
        ForEachMatrixWindow(fn);
        for (const MatrixWindow& window : NarrowWindows()) {
            fn(window);
        }
    }

    /// Whether the window is at density 1, as the engine's matrix and the narrow windows are, not one of `PhoneWindows`: the checks of the touch size of the HUD run in those only.
    inline bool IsMatrixWindow(const MatrixWindow& window) {
        return window.pixelDensity == 1.0f;
    }

    /// The profile of the table the component draws in this window (`TableProfileFor`, with the engine's touch minimum).
    inline TableProfile TableProfileOf(const MatrixWindow& window) {
        const ActiveSceneView view = ViewOf(window);
        return TableProfileFor(window.outputWidth, window.outputHeight, window.pixelDensity, CoreManager::GetUIEnvironment().GetSettings().minTouchSize, view.width, view.height);
    }

    /// The layout of the table the component draws for this input in this window: the profile the rule gives the window, the view of its shape.
    inline TableLayout TableLayoutFor(const MatrixWindow& window, TableInput input) {
        input.profile = TableProfileOf(window);
        const ActiveSceneView view = ViewOf(window);
        return MakeTableLayout(input, view.width, view.height);
    }

    /// Sets on `regions` what the table publishes in this window (`MakeTableRegions` of its worst-case layout, the phone profile's lifted plate
    /// included): a canvas laid out in the window by a helper below holds them while it does, so an element that names a region lies in it.
    inline void PublishTableRegionsOf(UIRegionSet& regions, const MatrixWindow& window) {
        PublishRegionsOf(regions, TableLayoutFor(window, WorstCaseTableInput(0)));
    }

    /// The layout context `ValidateOnMatrix` builds for a window of the matrix, for a scene whose view is the table (the view of the window's
    /// shape).
    inline UILayoutContext ContextOf(const MatrixWindow& window, float userScale = 1.0f) {
        UILayoutContext context;
        context.outputWidth = window.outputWidth;
        context.outputHeight = window.outputHeight;
        context.safeArea = window.safeArea;
        context.userScale = userScale;
        context.pixelDensity = window.pixelDensity;
        const ActiveSceneView view = ViewOf(window);
        context.sceneViewWidth = view.width;
        context.sceneViewHeight = view.height;
        return context;
    }

    /// Lays the canvas out in the window as the game does (the scene's view size, the density of the window) in the scale mode and at
    /// the user scale given (a `SceneView` canvas ignores both), and returns where it is. The rects of the elements are in canvas
    /// units afterwards; the canvas keeps the scale mode until its next `UpdateLayout`, which takes it from the file again. The canvas lies
    /// against the regions of the table in this window (`PublishTableRegionsOf`) while it is laid out; it holds the regions of the last
    /// draw again afterwards, so the next draw, which publishes only a change, finds them as it left them.
    inline UICanvasResolved LayOutInWindow(UICanvasComponent& canvas, const MatrixWindow& window, CanvasScaleMode mode = CanvasScaleMode::ScaleWithScreen, float userScale = 1.0f) {
        canvas.UpdateLayout(kViewWidth, kViewHeight);  // the reflected fields of the component reach the layout on its first update
        UICanvasLayout& layout = canvas.GetLayout();
        if (layout.params.space == CanvasSpace::Screen) {
            layout.params.scaleMode = mode;
        }
        const UIRegionSet published = layout.regions;
        layout.regions.Clear();
        PublishTableRegionsOf(layout.regions, window);
        layout.Update(ContextOf(window, userScale));
        layout.regions = published;
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

    /// The inset at the bottom of the window in output pixels (`window.safeArea` is what the matrix gives it; 0 for none).
    inline float BottomInsetOf(const MatrixWindow& window) {
        return window.HasInsets() ? window.outputHeight - (window.safeArea.y + window.safeArea.h) : 0.0f;
    }

    /// Whether the buttons of `Actions` are under the engine's touch minimum in this window and the rule shows `ActionsCompact` (at this
    /// user scale: the row is as high as the canvas it lies in makes it, and the user scale makes the canvas smaller; the window's bottom
    /// inset lifts the row, so the bar has to hold it above the inset).
    inline bool IsCompactWindow(const MatrixWindow& window, float userScale = 1.0f) {
        const ActiveSceneView view = ViewOf(window);
        return NeedsCompactActions(window.outputWidth, window.outputHeight, window.pixelDensity, EngineTouchMinimum(), userScale, view.width, view.height, BottomInsetOf(window));
    }

    /// Whether the buttons of `Actions` are under the engine's touch minimum in this window, whatever canvas the rule shows.
    inline bool ActionButtonsAreUnderTheMinimum(const MatrixWindow& window) {
        return ActionButtonHeightFor(ShapeOf(window)) * TableFit(window).scale / window.pixelDensity < EngineTouchMinimum();
    }

    /// Whether `Actions` is the canvas shown in this window at this user scale with buttons under the engine's touch minimum: the rule keeps
    /// it because the bar under the table, less the bottom inset, cannot hold the compact row. In the matrix that is the portrait window of
    /// 1:1 at 480 px (25.5 px, no bar at all); no landscape window of it has small buttons now that 4:3 and 3:2 are the narrow view.
    /// Outside it, the near-square narrow window with side insets (610 x 600: the table's 31.6 px, the row above the inset does not fit).
    inline bool ActionsStaysSmall(const MatrixWindow& window, float userScale) {
        return ActionButtonsAreUnderTheMinimum(window) && !IsCompactWindow(window, userScale);
    }

    /// Where a matrix test may leave the touch size of `Actions` out: every window where its buttons are small, whichever canvas the rule shows
    /// (`ActionsCompact` holds them in the near-square narrow windows and the portrait ones under 339 px; the matrix's own is only 1:1 at 480 px, which
    /// keeps `Actions`, `MahjongTouchSizeTest` holds that it is the only one).
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

    /// The table with every zone at its capacity as the component draws it in this window (the profile the rule gives the window).
    inline TableLayout MakeWorstCaseTableLayout(Seat viewer, const MatrixWindow& window) {
        return TableLayoutFor(window, WorstCaseTableInput(viewer));
    }

    /// The input of the table as the component makes it from the model (the seat at the bottom is seat 0).
    inline TableInput InputOf(const HandModel& hand) {
        TableInput input;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const PlayerHand& held = hand.GetHand(seat);
            input.concealedCount[seat] = static_cast<int>(held.concealed.size());
            input.melds[seat] = held.melds;
            input.bonusCount[seat] = static_cast<int>(held.bonus.size());
        }
        for (const DiscardEntry& entry : hand.Discards()) {
            input.discardSeats.push_back(entry.seat);
        }
        input.wallRemaining = static_cast<int>(hand.GetWall().Remaining());
        return input;
    }

    /// Whether the layouts have the same slots in the same places.
    inline bool SameSlots(const TableLayout& a, const TableLayout& b) {
        if (a.slots.size() != b.slots.size()) {
            return false;
        }
        for (size_t i = 0; i < a.slots.size(); ++i) {
            const Rect& ra = a.slots[i].rect;
            const Rect& rb = b.slots[i].rect;
            if (ra.x != rb.x || ra.y != rb.y || ra.w != rb.w || ra.h != rb.h || a.slots[i].role != b.slots[i].role || a.slots[i].index != b.slots[i].index) {
                return false;
            }
        }
        return true;
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
    /// does. Every canvas is laid out against the view of its window's shape; `portraitWindows` false leaves the portrait windows out (a
    /// canvas with no portrait variant yet). Returns the failures, empty when the canvas passes.
    inline eastl::vector<UIMatrixFailure> MatrixFailuresExceptTouchWhere(UICanvasComponent& canvas, float minTouchSize, const std::function<bool(const MatrixWindow&)>& skip,
                                                                         bool portraitWindows = true) {
        canvas.UpdateLayout(kViewWidth, kViewHeight);
        UICanvasLayout& layout = canvas.GetLayout();
        const UICanvasScaleParams saved = layout.params;
        const UIRegionSet savedRegions = layout.regions;
        const bool screen = saved.space == CanvasSpace::Screen;
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        const float userScales[] = {1.0f, 1.5f};

        eastl::vector<UIMatrixFailure> failures;
        ForEachMatrixAndNarrowWindow([&](const MatrixWindow& window) {
            if (!portraitWindows && ShapeOf(window) == TableShape::Portrait) {
                return;
            }
            const bool dropTouch = skip(window);
            layout.regions.Clear();
            PublishTableRegionsOf(layout.regions, window);
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
        layout.regions = savedRegions;
        layout.Invalidate();
        return failures;
    }

    /// Every label and button of the canvas, shown or not, in tree order.
    inline void CollectTextControls(const UIElement& element, std::vector<const UIElement*>& found) {
        if (ShowsText(element)) {
            found.push_back(&element);
        }
        for (const auto& child : element.children) {
            CollectTextControls(*child, found);
        }
    }

    /// The words of the canvas on the whole matrix: `ValidateOnMatrix` does not flag a text that can shrink (`AutoFit`, ellipsis), so
    /// this asks every shown label and button on the text it shows, with `air` units kept free each side of an interactive one, in every
    /// window, scale mode and user scale of the matrix (a `SceneView` canvas once per window; with `only` the windows of that shape alone).
    /// Returns a line per control that cuts its text, empty when every word fits.
    inline std::vector<std::string> WordsThatDoNotFitOnTheMatrix(UICanvasComponent& canvas, float air, std::optional<TableShape> only = std::nullopt) {
        canvas.UpdateLayout(kViewWidth, kViewHeight);
        UICanvasLayout& layout = canvas.GetLayout();
        const UICanvasScaleParams saved = layout.params;
        const UIRegionSet savedRegions = layout.regions;
        const bool screen = saved.space == CanvasSpace::Screen;
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        const float userScales[] = {1.0f, 1.5f};

        std::vector<std::string> cut;
        ForEachMatrixAndNarrowWindow([&](const MatrixWindow& window) {
            if (only.has_value() && ShapeOf(window) != *only) {
                return;
            }
            layout.regions.Clear();
            PublishTableRegionsOf(layout.regions, window);
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
                        const float spare = control->IsInteractive() ? air : 0.0f;
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
        layout.regions = savedRegions;
        layout.Invalidate();
        return cut;
    }

    /// `MatrixFailuresExceptTouchWhere` as an expectation, with the name of the node in the message.
    inline void ExpectMatrixExceptTouchWhere(UICanvasComponent& canvas, const char* node, float minTouchSize, const std::function<bool(const MatrixWindow&)>& skip) {
        const auto failures = MatrixFailuresExceptTouchWhere(canvas, minTouchSize, skip);
        EXPECT_TRUE(failures.empty()) << node << ": " << DescribeMatrixFailures(failures).c_str();
    }

}  // namespace BECore::Mahjong::Tests
