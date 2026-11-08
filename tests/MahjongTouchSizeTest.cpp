#include "MahjongTouchTestSupport.h"

#include <BECore/UI/UIButton.h>

/**
 * The touch size of the shipped scene: every control is a touch target of the engine's minimum in every window of the size matrix. The
 * helpers of `MahjongTouchTestSupport.h` are tested first (the windows of the matrix, where a canvas lies in a window and in view units
 * of the table, the loop that drops the touch size where a test allows it); the tests of the canvases come with the canvases.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kEngineTouchMinimum = 32.0f;

        /// The window of 1280 x 720 pixels, where the view is the output and a canvas of a scene is the view.
        MatrixWindow DesignWindow() {
            MatrixWindow window;
            window.label = "design 1280x720";
            window.outputWidth = kViewWidth;
            window.outputHeight = kViewHeight;
            return window;
        }

        MatrixWindow WindowOf(float width, float height) {
            MatrixWindow window = DesignWindow();
            window.outputWidth = width;
            window.outputHeight = height;
            return window;
        }

        void ExpectSameRect(const Rect& actual, const Rect& expected, const char* label) {
            EXPECT_NEAR(actual.x, expected.x, 0.01f) << label;
            EXPECT_NEAR(actual.y, expected.y, 0.01f) << label;
            EXPECT_NEAR(actual.w, expected.w, 0.01f) << label;
            EXPECT_NEAR(actual.h, expected.h, 0.01f) << label;
        }

    }  // namespace

    class MahjongTouchSizeTest : public MahjongTableTestBase {};

    TEST_F(MahjongTouchSizeTest, EveryWindowOfTheMatrixIsVisitedOnceWithItsInsets) {
        size_t visited = 0;
        size_t notches = 0;
        size_t sides = 0;
        size_t plain = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            const UIMatrixCase& matrixCase = BECore::Tests::UILayoutMatrix()[visited];
            EXPECT_EQ(window.label, matrixCase.label);
            EXPECT_EQ(window.outputWidth, matrixCase.context.outputWidth);
            EXPECT_EQ(window.outputHeight, matrixCase.context.outputHeight);
            EXPECT_GT(window.outputWidth, 0.0f);
            EXPECT_GT(window.outputHeight, 0.0f);
            EXPECT_EQ(window.pixelDensity, 1.0f);
            // The label of the matrix names the simulation, so the two are checked against each other
            EXPECT_EQ(window.label.find("notch") != eastl::string::npos, window.simulation == SafeAreaSimulation::TopNotch) << window.label.c_str();
            EXPECT_EQ(window.label.find("side insets") != eastl::string::npos, window.simulation == SafeAreaSimulation::SideInsets) << window.label.c_str();
            EXPECT_EQ(window.HasInsets(), window.simulation != SafeAreaSimulation::None);
            window.simulation == SafeAreaSimulation::TopNotch ? ++notches : window.simulation == SafeAreaSimulation::SideInsets ? ++sides : ++plain;
            ++visited;
        });
        EXPECT_EQ(visited, BECore::Tests::UILayoutMatrix().size());
        EXPECT_GT(plain, 0u);
        EXPECT_EQ(notches, plain);
        EXPECT_EQ(sides, plain);
    }

    TEST_F(MahjongTouchSizeTest, ARectOnTheTableIsFoundAndOneOnABarOrOutsideTheViewIsClear) {
        const TableLayout layout = MakeWorstCaseTableLayout();
        ASSERT_FALSE(layout.slots.empty());

        // The middle of a slot is on the table, whatever else lies there
        const Rect& slot = layout.slots.front().rect;
        const Rect inside{slot.x + slot.w * 0.5f, slot.y + slot.h * 0.5f, 2.0f, 2.0f};
        const TileSlot* found = FirstSlotUnder(inside, layout);
        ASSERT_NE(found, nullptr);
        EXPECT_TRUE(Intersects(inside, found->rect));

        // Rects on the bars of a narrow window lie outside the view: above and below it, and beyond its sides
        EXPECT_EQ(FirstSlotUnder({0.0f, -50.0f, kViewWidth, 40.0f}, layout), nullptr);
        EXPECT_EQ(FirstSlotUnder({0.0f, kViewHeight + 10.0f, kViewWidth, 40.0f}, layout), nullptr);
        EXPECT_EQ(FirstSlotUnder({-50.0f, 0.0f, 40.0f, kViewHeight}, layout), nullptr);
        EXPECT_EQ(FirstSlotUnder({kViewWidth + 10.0f, 0.0f, 40.0f, kViewHeight}, layout), nullptr);
        ExpectClearOfTable({0.0f, kViewHeight + 10.0f, kViewWidth, 40.0f}, layout, "a bar under the table");

        // A rect that only touches a slot is apart from it
        EXPECT_FALSE(Intersects({slot.x + slot.w, slot.y, 10.0f, slot.h}, slot));
        EXPECT_TRUE(Intersects({slot.x + slot.w - 1.0f, slot.y, 10.0f, slot.h}, slot));
    }

    TEST_F(MahjongTouchSizeTest, TheWorstCaseLayoutHoldsEveryZoneAtItsCapacity) {
        const TableLayout layout = MakeWorstCaseTableLayout();
        int roles[5] = {};
        for (const TileSlot& slot : layout.slots) {
            ++roles[static_cast<int>(slot.role)];
        }
        EXPECT_EQ(roles[static_cast<int>(SlotRole::HandTile)], kSeatCount * kMaxConcealedTiles);
        EXPECT_EQ(roles[static_cast<int>(SlotRole::MeldTile)], kSeatCount * kMaxMeldTiles);
        EXPECT_EQ(roles[static_cast<int>(SlotRole::BonusTile)], kSeatCount * kMaxBonusTiles);
        EXPECT_EQ(roles[static_cast<int>(SlotRole::DiscardTile)], 100);
        EXPECT_EQ(roles[static_cast<int>(SlotRole::WallStack)], kWallRingCells);
    }

    TEST_F(MahjongTouchSizeTest, ASceneViewCanvasIsItsOwnRectInViewUnitsInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        auto actions = Canvas("Actions");
        ASSERT_TRUE(actions);

        // The row of buttons at the design size: the canvas units are view units and so is the output
        const MatrixWindow design = DesignWindow();
        const UICanvasResolved resolved = LayOutInWindow(*actions, design);
        EXPECT_EQ(resolved.scale, 1.0f);
        const UIElement* row = Element("Actions", "ActionRow");
        ASSERT_NE(row, nullptr);
        ASSERT_FALSE(row->rect.IsEmpty());
        ExpectSameRect(ToView(row->rect, *actions, design), row->rect, "1280x720");

        // The fit of the table scales a SceneView canvas with the table, so the rect stays the same in any window, bars or not
        for (const MatrixWindow& window : {WindowOf(640.0f, 360.0f), WindowOf(480.0f, 480.0f), WindowOf(480.0f, 853.0f), WindowOf(2560.0f, 1080.0f)}) {
            LayOutInWindow(*actions, window);
            const UIElement* narrow = Element("Actions", "ActionRow");
            ASSERT_NE(narrow, nullptr);
            ExpectSameRect(ToView(narrow->rect, *actions, window), narrow->rect, window.label.c_str());
            EXPECT_FLOAT_EQ(actions->GetLayout().GetResolved().scale, TableFit(window).scale) << "a SceneView canvas has the scale of the table's fit";
        }
    }

    TEST_F(MahjongTouchSizeTest, AScreenCanvasIsItsOwnRectAtTheDesignSizeAndScaledByItsOwnScaleElsewhere) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        // Shown by hand, and no draw after it (a draw would hide it again): the cover is a Screen canvas with a floor on its scale
        CoreManager::GetUIVars().Set("handoffSeat"_intern, eastl::string_view("Seat 2 (South)"));
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Handoff"_intern, true);
        auto cover = Canvas("Handoff");
        ASSERT_TRUE(cover);

        // 1280 x 720, ScaleWithScreen, user scale 1.0: the scale is 1.0 and the canvas is the view
        const MatrixWindow design = DesignWindow();
        const UICanvasResolved resolved = LayOutInWindow(*cover, design, CanvasScaleMode::ScaleWithScreen, 1.0f);
        EXPECT_EQ(resolved.scale, 1.0f);
        const UIElement* reveal = Element("Handoff", "Reveal");
        ASSERT_NE(reveal, nullptr);
        ASSERT_FALSE(reveal->rect.IsEmpty());
        ExpectSameRect(ToView(reveal->rect, *cover, design), reveal->rect, "1280x720");

        // The same canvas at twice the size of the output: the scale is 2 and so is the fit of the table, so the rect is again its own
        const MatrixWindow big = WindowOf(2560.0f, 1440.0f);
        LayOutInWindow(*cover, big, CanvasScaleMode::ScaleWithScreen, 1.0f);
        ExpectSameRect(ToView(Element("Handoff", "Reveal")->rect, *cover, big), Element("Handoff", "Reveal")->rect, "2560x1440");

        // 480 x 480: the floor of the canvas (`minScale` 0.7) is higher than the table's fit (0.375), so the button is bigger in view units
        const MatrixWindow narrow = WindowOf(480.0f, 480.0f);
        const UICanvasResolved floored = LayOutInWindow(*cover, narrow, CanvasScaleMode::ScaleWithScreen, 1.0f);
        const float fit = TableFit(narrow).scale;
        EXPECT_NEAR(floored.scale, 0.7f, 0.001f);
        EXPECT_NEAR(fit, 0.375f, 0.001f);
        const UIElement* narrowReveal = Element("Handoff", "Reveal");
        ASSERT_NE(narrowReveal, nullptr);
        const Rect view = ToView(narrowReveal->rect, *cover, narrow);
        EXPECT_NEAR(view.w, narrowReveal->rect.w * floored.scale / fit, 0.01f);
        EXPECT_NEAR(view.h, narrowReveal->rect.h * floored.scale / fit, 0.01f);
        // And the pixels agree: the button of 56 units is 0.7 x 56 = 39 px high
        EXPECT_NEAR(view.h * fit, narrowReveal->rect.h * floored.scale, 0.01f);
    }

    TEST_F(MahjongTouchSizeTest, TheLoopDropsTheTouchSizeOnlyWhereTheTestAllowsItAndNothingElse) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto actions = Canvas("Actions");
        ASSERT_TRUE(actions);
        // The fullest window of buttons (all claims, three chows, pass), as the screen test has them: all thirteen do not fit a row
        for (const ButtonKind kind :
             {ButtonKind::ClaimMahjong, ButtonKind::ClaimKong, ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2, ButtonKind::Pass}) {
            const ButtonSpec& spec = SpecOf(kind);
            ASSERT_TRUE(actions->SetElementVisible(PoolString::Intern(spec.element), true)) << spec.element;
        }

        const auto never = [](const MatrixWindow&) { return false; };
        const auto always = [](const MatrixWindow&) { return true; };
        const auto compact = [](const MatrixWindow& window) { return NeedsCompactActions(window.outputWidth, window.outputHeight, window.pixelDensity, kEngineTouchMinimum); };

        // With the engine's minimum the buttons are too small in the narrow windows, and that is all that is wrong with them
        const auto strict = MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, never);
        ASSERT_FALSE(strict.empty());
        for (const BECore::Tests::UIMatrixFailure& failure : strict) {
            for (const UILayoutIssue& issue : failure.issues) {
                EXPECT_EQ(issue.kind, UILayoutIssueKind::TouchTargetTooSmall) << failure.label.c_str();
            }
        }
        EXPECT_TRUE(MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, always).empty());
        // Dropping them exactly where the rule says compact leaves a failure for no window: that is what the rule is for
        EXPECT_TRUE(MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, compact).empty());
        // Dropping them only in windows the rule calls roomy leaves the narrow ones failing
        const auto roomy = [&](const MatrixWindow& window) { return !compact(window); };
        EXPECT_FALSE(MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, roomy).empty());
        // The canvas keeps what the file declared
        EXPECT_EQ(actions->GetLayout().params.space, CanvasSpace::SceneView);
    }

    // ----- the New game button of the HUD -----

    TEST_F(MahjongTouchSizeTest, TheNewGameButtonIsATouchTargetInEveryWindowAndCoversNoTileWhereTheWindowHasNoInsets) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(menu);
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        EXPECT_EQ(minTouch, kEngineTouchMinimum);
        ExpectPassesTheMatrix("HudMenu", minTouch);
        EXPECT_EQ(menu->GetLayout().params.space, CanvasSpace::Screen) << "the button is in a Screen canvas";
        EXPECT_EQ(menu->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);

        const TableLayout layout = MakeWorstCaseTableLayout();
        size_t checked = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            if (window.HasInsets()) {
                return;  // with insets the button lies nearer the table, as the HUD does
            }
            LayOutInWindow(*menu, window, CanvasScaleMode::ScaleWithScreen, 1.0f);
            const UIElement* button = Element("HudMenu", "NewGame");
            ASSERT_NE(button, nullptr);
            ASSERT_FALSE(button->rect.IsEmpty()) << window.label.c_str();
            ExpectClearOfTable(ToView(button->rect, *menu, window), layout, window.label.c_str());
            ++checked;
        });
        EXPECT_GT(checked, 0u);
    }

    // ----- the Next hand button of the score table -----

    TEST_F(MahjongTouchSizeTest, TheActionBarOfTheScoreTableLiesInsideTheWindowAndDoesNotTouchThePlate) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->GetSession().RunComputers();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        Draw();
        auto plate = Canvas("HandOver");
        auto action = Canvas("HandOverAction");
        ASSERT_TRUE(plate && action);
        ASSERT_TRUE(plate->TakesInput() && action->TakesInput());
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        ExpectPassesTheMatrix("HandOverAction", minTouch);
        EXPECT_EQ(action->GetLayout().params.space, CanvasSpace::Screen);
        EXPECT_EQ(action->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);

        size_t checked = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            // The plate (SceneView) and the bar (Screen, ScaleWithScreen, user scale 1.0), both in view units of the table
            LayOutInWindow(*plate, window);
            const UIElement* plateElement = Element("HandOver", "Plate");
            ASSERT_NE(plateElement, nullptr);
            ASSERT_FALSE(plateElement->rect.IsEmpty()) << window.label.c_str();
            const Rect plateRect = ToView(plateElement->rect, *plate, window);

            LayOutInWindow(*action, window, CanvasScaleMode::ScaleWithScreen, 1.0f);
            const UIElement* bar = Element("HandOverAction", "Bar");
            const UIElement* button = Element("HandOverAction", "NextHand");
            ASSERT_TRUE(bar != nullptr && button != nullptr);
            ASSERT_FALSE(bar->rect.IsEmpty()) << window.label.c_str();
            EXPECT_FALSE(Intersects(ToView(bar->rect, *action, window), plateRect)) << window.label.c_str() << ": the bar touches the plate";
            EXPECT_FALSE(Intersects(ToView(button->rect, *action, window), plateRect)) << window.label.c_str() << ": the button touches the plate";

            // Inside the window (the validator says the same for every mode and user scale)
            const Rect output = action->GetLayout().GetResolved().transform.ToOutput(bar->rect.x, bar->rect.y, bar->rect.w, bar->rect.h);
            EXPECT_GE(output.x, -0.01f) << window.label.c_str();
            EXPECT_GE(output.y, -0.01f) << window.label.c_str();
            EXPECT_LE(output.x + output.w, window.outputWidth + 0.01f) << window.label.c_str();
            EXPECT_LE(output.y + output.h, window.outputHeight + 0.01f) << window.label.c_str();
            ++checked;
        });
        EXPECT_EQ(checked, BECore::Tests::UILayoutMatrix().size());
    }

}  // namespace BECore::Mahjong::Tests
