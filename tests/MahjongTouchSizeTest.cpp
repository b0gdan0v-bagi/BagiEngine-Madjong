#include "MahjongTouchTestSupport.h"

#include <BECore/UI/UIButton.h>
#include <EASTL/map.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/UiPalette.h>
#include <set>

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
            window.label = (std::to_string(static_cast<int>(width)) + "x" + std::to_string(static_cast<int>(height))).c_str();
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
        const auto compact = [](const MatrixWindow& window) { return IsCompactWindow(window); };

        // With the engine's minimum the buttons are too small in the narrow windows, and that is all that is wrong with them
        const auto strict = MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, never);
        ASSERT_FALSE(strict.empty());
        for (const BECore::Tests::UIMatrixFailure& failure : strict) {
            for (const UILayoutIssue& issue : failure.issues) {
                EXPECT_EQ(issue.kind, UILayoutIssueKind::TouchTargetTooSmall) << failure.label.c_str();
            }
        }
        EXPECT_TRUE(MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, always).empty());
        // Dropping them where the rule says compact at the user scale 1.0 leaves nothing: every window below the touch size has a bar that holds
        // the row there (a line of 33.6 px on the 60 px of 640 x 480). The matrix runs a `SceneView` canvas once per window, whatever the user
        // scale, and `ActionsMayBeSmall` adds the windows that keep `Actions` at 1.5 (640 x 480 and 720 x 480, which the rule shows compact at 1.0)
        EXPECT_TRUE(MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, compact).empty());
        EXPECT_TRUE(MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, ActionsMayBeSmall).empty());
        // Dropping them only in windows the rule calls roomy leaves the narrow ones failing
        const auto roomy = [&](const MatrixWindow& window) { return !compact(window); };
        EXPECT_FALSE(MatrixFailuresExceptTouchWhere(*actions, kEngineTouchMinimum, roomy).empty());
        // The canvas keeps what the file declared
        EXPECT_EQ(actions->GetLayout().params.space, CanvasSpace::SceneView);
    }

    // ----- the hint button -----

    TEST_F(MahjongTouchSizeTest, TheHintButtonOfTheTableIsAControlOf56UnitsClearOfTheWorstCaseTableAndNeedsTheCompactRowWhereItIsSmall) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("HintsButton");
        ASSERT_TRUE(canvas);
        ASSERT_TRUE(canvas->SetElementVisible("HintButton"_intern, true));
        canvas->UpdateLayout(kViewWidth, kViewHeight);
        const UIElement* button = Element("HintsButton", "HintButton");
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(canvas->GetLayout().params.space, CanvasSpace::SceneView) << "it lies on the table, scaled with it, where `Actions` is";
        EXPECT_FLOAT_EQ(button->rect.h, kActionButtonHeight) << "the same height as the buttons of `Actions`, so the same boundary of the rule";
        // The strip below the waiting row holds no tile of the worst-case table
        ExpectClearOfTable(button->rect, MakeWorstCaseTableLayout(), "HintButton on the table");

        // Under the engine's minimum exactly where the buttons of `Actions` are, and there the rule shows the row of `ActionsCompact` with its own
        // hint button (but in the windows that keep `Actions` at the user scale 1.5, which `ActionsMayBeSmall` names)
        const auto never = [](const MatrixWindow&) { return false; };
        const auto strict = MatrixFailuresExceptTouchWhere(*canvas, kEngineTouchMinimum, never);
        ASSERT_FALSE(strict.empty());
        for (const BECore::Tests::UIMatrixFailure& failure : strict) {
            for (const UILayoutIssue& issue : failure.issues) {
                EXPECT_EQ(issue.kind, UILayoutIssueKind::TouchTargetTooSmall) << failure.label.c_str();
            }
        }
        EXPECT_TRUE(MatrixFailuresExceptTouchWhere(*canvas, kEngineTouchMinimum, [](const MatrixWindow& window) { return IsCompactWindow(window); }).empty());
        EXPECT_TRUE(MatrixFailuresExceptTouchWhere(*canvas, kEngineTouchMinimum, ActionsMayBeSmall).empty());
    }

    // ----- the New game button of the HUD -----

    TEST_F(MahjongTouchSizeTest, TheNewGameButtonIsATouchTargetInEveryWindowAndCoversNoTileWhereTheWindowHasNoInsets) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(menu);
        const float minTouch = EngineTouchMinimum();
        EXPECT_EQ(minTouch, kEngineTouchMinimum);
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

    TEST_F(MahjongTouchSizeTest, TheRulesButtonIsATouchTargetInEveryWindowCoversNoTileWhereThereAreNoInsetsAndNeverTouchesNewGame) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(menu);
        const float minTouch = EngineTouchMinimum();
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};

        const TableLayout layout = MakeWorstCaseTableLayout();
        size_t checked = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            for (const CanvasScaleMode mode : modes) {
                for (const float userScale : {1.0f, 1.5f}) {
                    const std::string label = Format("{} {} x{}", window.label.c_str(), EnumUtils<CanvasScaleMode>::ToString(mode).data(), userScale).c_str();
                    const UICanvasResolved resolved = LayOutInWindow(*menu, window, mode, userScale);
                    const UIElement* rules = Element("HudMenu", "Rules");
                    const UIElement* newGame = Element("HudMenu", "NewGame");
                    ASSERT_TRUE(rules != nullptr && newGame != nullptr);
                    ASSERT_FALSE(rules->rect.IsEmpty()) << label;
                    EXPECT_GE(std::min(rules->rect.w, rules->rect.h) * resolved.scale / window.pixelDensity, minTouch - 0.01f) << label << ": a touch target as the user sees it";
                    EXPECT_FALSE(Intersects(rules->rect, newGame->rect)) << label << ": the Rules button touches New game";
                    EXPECT_GE(rules->rect.x, resolved.contentBounds.x - 0.01f) << label;
                    EXPECT_LE(rules->rect.x + rules->rect.w, resolved.contentBounds.x + resolved.contentBounds.w + 0.01f) << label;
                    EXPECT_LE(rules->rect.y + rules->rect.h, resolved.contentBounds.y + resolved.contentBounds.h + 0.01f) << label;
                    ++checked;
                }
            }
            if (window.HasInsets()) {
                return;  // with insets the buttons lie nearer the table, as the HUD does
            }
            LayOutInWindow(*menu, window, CanvasScaleMode::ScaleWithScreen, 1.0f);
            ExpectClearOfTable(ToView(Element("HudMenu", "Rules")->rect, *menu, window), layout, window.label.c_str());
        });
        EXPECT_EQ(checked, BECore::Tests::UILayoutMatrix().size() * 6);
    }

    TEST_F(MahjongTouchSizeTest, WhileThePagesAreOpenTheMenuIsHiddenAndItsButtonsLieUnderTheDimOfThePages) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(menu && menu->TakesInput());
        _game->OpenRules();
        Draw();
        auto rules = Canvas("Rules");
        ASSERT_TRUE(rules);
        EXPECT_TRUE(rules->TakesInput());
        EXPECT_FALSE(menu->TakesInput()) << "a hidden canvas takes no input";
        // In the design window the click at the place of either button is the dim's
        const UIElement* dim = Element("Rules", "Dim");
        ASSERT_NE(dim, nullptr);
        for (const char* name : {"Rules", "NewGame"}) {
            const UIElement* button = Element("HudMenu", name);
            ASSERT_NE(button, nullptr);
            EXPECT_GE(button->rect.x, dim->rect.x) << name;
            EXPECT_LE(button->rect.x + button->rect.w, dim->rect.x + dim->rect.w) << name;
            EXPECT_GE(button->rect.y, dim->rect.y) << name;
            EXPECT_LE(button->rect.y + button->rect.h, dim->rect.y + dim->rect.h) << name;
        }
        ClickElement("HudMenu", "NewGame");
        EXPECT_FALSE(_game->IsSetupOpen());
    }

    // ----- the seat choice -----

    TEST_F(MahjongTouchSizeTest, TheSeatChoiceKeepsItsRowsInsideThePlateAndTheSafeAreaAndEverySegmentIsATouchTargetInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        // The button beside Start, as it is when a saved match is on offer (the HUD test shows it by a real save)
        UICanvasComponent::SetElementVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Continue"_intern, true);
        auto setup = Canvas("Setup");
        ASSERT_TRUE(setup);
        EXPECT_EQ(setup->GetLayout().params.space, CanvasSpace::Screen) << "the seat choice is a Screen canvas";
        EXPECT_EQ(setup->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);
        EXPECT_GE(setup->GetLayout().params.minScale, 0.6f) << "a floor keeps a 56 unit control above 32 px";
        EXPECT_EQ(setup->GetLayout().params.maxScale, UICanvasScaleParams{}.maxScale) << "no cap of our own: the scale is in output pixels and does not know the density";
        const float minTouch = EngineTouchMinimum();

        const char* const rows[] = {"Title", "LanguageRow", "SeatRow0", "SeatRow1", "SeatRow2", "SeatRow3", "HandsRow", "HintsRow", "Start"};
        const char* const segments[] = {"LanguageEn",  "LanguageRu",    "Seat0Person", "Seat0Computer", "Seat1Person", "Seat1Computer", "Seat2Person", "Seat2Computer",
                                        "Seat3Person", "Seat3Computer", "Hands16",     "Hands8",        "HintsOn",     "HintsOff",      "Continue"};
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        size_t cases = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            for (const CanvasScaleMode mode : modes) {
                for (const float userScale : {1.0f, 1.5f}) {
                    const std::string label = Format("{} {} x{}", window.label.c_str(), EnumUtils<CanvasScaleMode>::ToString(mode).data(), userScale).c_str();
                    const UICanvasResolved resolved = LayOutInWindow(*setup, window, mode, userScale);
                    const UIElement* plate = Element("Setup", "Plate");
                    ASSERT_NE(plate, nullptr);
                    ASSERT_FALSE(plate->rect.IsEmpty()) << label;
                    const Rect& safe = resolved.contentBounds;
                    EXPECT_GE(plate->rect.x, safe.x - 0.01f) << label;
                    EXPECT_GE(plate->rect.y, safe.y - 0.01f) << label;
                    EXPECT_LE(plate->rect.x + plate->rect.w, safe.x + safe.w + 0.01f) << label;
                    EXPECT_LE(plate->rect.y + plate->rect.h, safe.y + safe.h + 0.01f) << label;

                    // The rows lie in the plate, one under the other (the title is gone in a short canvas)
                    const UIElement* previous = nullptr;
                    for (const char* name : rows) {
                        const UIElement* row = Element("Setup", name);
                        ASSERT_NE(row, nullptr) << name;
                        if (row->rect.IsEmpty()) {
                            EXPECT_STREQ(name, "Title") << label << ": only the title may give way";
                            continue;
                        }
                        EXPECT_GE(row->rect.y, plate->rect.y - 0.01f) << label << " " << name;
                        EXPECT_LE(row->rect.y + row->rect.h, plate->rect.y + plate->rect.h + 0.01f) << label << " " << name;
                        if (previous != nullptr) {
                            EXPECT_GE(row->rect.y, previous->rect.y + previous->rect.h - 0.01f) << label << " " << name << " overlaps the row above";
                        }
                        previous = row;
                    }
                    // Every segment lies in the plate and is a touch target as the user sees it
                    for (const char* name : segments) {
                        const UIElement* segment = Element("Setup", name);
                        ASSERT_NE(segment, nullptr) << name;
                        ASSERT_FALSE(segment->rect.IsEmpty()) << label << " " << name;
                        EXPECT_GE(segment->rect.x, plate->rect.x - 0.01f) << label << " " << name;
                        EXPECT_LE(segment->rect.x + segment->rect.w, plate->rect.x + plate->rect.w + 0.01f) << label << " " << name;
                        EXPECT_GE(std::min(segment->rect.w, segment->rect.h) * resolved.scale / window.pixelDensity, minTouch - 0.01f) << label << " " << name;
                    }
                    const UIElement* start = Element("Setup", "Start");
                    ASSERT_NE(start, nullptr);
                    EXPECT_GE(start->rect.h * resolved.scale / window.pixelDensity, minTouch - 0.01f) << label << " Start";
                    const UIElement* proceed = Element("Setup", "Continue");
                    ASSERT_NE(proceed, nullptr);
                    EXPECT_LE(proceed->rect.x + proceed->rect.w, start->rect.x + 0.01f) << label << ": Continue and Start do not overlap";
                    ++cases;
                }
            }
        });
        EXPECT_EQ(cases, BECore::Tests::UILayoutMatrix().size() * 6);
    }

    TEST_F(MahjongTouchSizeTest, TheSeatChoiceIsMainsAnatomyWhereThereIsRoomAndTheTitleGivesWayInAShortCanvas) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        auto setup = Canvas("Setup");
        ASSERT_TRUE(setup);

        // The design size: the plate of 664, the title band of 48, the rows of 64 and a Start of 64
        LayOutInWindow(*setup, DesignWindow());
        EXPECT_NEAR(Element("Setup", "Plate")->rect.h, 664.0f, 0.01f);
        EXPECT_NEAR(Element("Setup", "Title")->rect.h, 48.0f, 0.01f);
        for (const char* row : {"LanguageRow", "SeatRow0", "SeatRow1", "SeatRow2", "SeatRow3", "HandsRow", "HintsRow", "Start"}) {
            EXPECT_NEAR(Element("Setup", row)->rect.h, 64.0f, 0.01f) << row;
        }
        EXPECT_NEAR(Element("Setup", "SeatCaption0")->rect.w, 112.0f, 0.01f);

        // A canvas of 320 units (a 480 px window at the user scale 1.5): no title, narrower captions, the plate the whole safe width
        const MatrixWindow tight = WindowOf(480.0f, 480.0f);
        LayOutInWindow(*setup, tight, CanvasScaleMode::ConstantPixelSize, 1.5f);
        EXPECT_TRUE(Element("Setup", "Title")->rect.IsEmpty());
        EXPECT_LT(Element("Setup", "SeatCaption0")->rect.w, 112.0f);
        EXPECT_NEAR(Element("Setup", "Plate")->rect.w, setup->GetLayout().GetResolved().contentBounds.w, 0.01f);
        for (const char* row : {"LanguageRow", "SeatRow0", "HintsRow", "Start"}) {
            EXPECT_GT(Element("Setup", row)->rect.h, 0.0f) << row << ": nothing but the title is dropped";
        }
    }

    TEST_F(MahjongTouchSizeTest, TheSeatChoiceDimsTheWholeWindowAndTheNewGameButtonUnderItTakesNoClick) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        auto setup = Canvas("Setup");
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(setup && menu);
        ASSERT_TRUE(menu->TakesInput()) << "the HUD button is shown while the seat choice is open";

        // In every window the dim (the whole window, bars included) covers the New game button, which lies on a bar in a narrow window
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            LayOutInWindow(*setup, window);
            LayOutInWindow(*menu, window);
            const UIElement* dim = Element("Setup", "Dim");
            const UIElement* button = Element("HudMenu", "NewGame");
            ASSERT_TRUE(dim != nullptr && button != nullptr);
            const Rect dimOutput = setup->GetLayout().GetResolved().transform.ToOutput(dim->rect.x, dim->rect.y, dim->rect.w, dim->rect.h);
            const Rect buttonOutput = menu->GetLayout().GetResolved().transform.ToOutput(button->rect.x, button->rect.y, button->rect.w, button->rect.h);
            EXPECT_LE(dimOutput.x, 0.01f) << window.label.c_str();
            EXPECT_LE(dimOutput.y, 0.01f) << window.label.c_str();
            EXPECT_GE(dimOutput.x + dimOutput.w, window.outputWidth - 0.01f) << window.label.c_str();
            EXPECT_GE(dimOutput.y + dimOutput.h, window.outputHeight - 0.01f) << window.label.c_str();
            EXPECT_GE(buttonOutput.x, dimOutput.x - 0.01f) << window.label.c_str();
            EXPECT_LE(buttonOutput.x + buttonOutput.w, dimOutput.x + dimOutput.w + 0.01f) << window.label.c_str();
            EXPECT_GE(buttonOutput.y, dimOutput.y - 0.01f) << window.label.c_str();
            EXPECT_LE(buttonOutput.y + buttonOutput.h, dimOutput.y + dimOutput.h + 0.01f) << window.label.c_str();
        });

        // And a click on it reaches the modal canvas, not the button: the choice made so far stays
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "Seat2Person"));
        const SeatSetup pending = MahjongHudComponent::GetActive()->PendingSeats();
        ASSERT_EQ(pending[2], SeatKind::Person);
        ASSERT_TRUE(OnScreen("HudMenu", "NewGame"));
        ClickElement("HudMenu", "NewGame");
        EXPECT_TRUE(Canvas("Setup")->TakesInput());
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingSeats(), pending) << "New game would have opened the panel again from the seats of the game";
    }

    // ----- the Next hand button of the score table -----

    TEST_F(MahjongTouchSizeTest, TheNextHandButtonIsATouchTargetInEveryWindowAndTouchesNeitherThePlateNorATile) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->GetSession().RunComputers();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        Draw();
        auto plate = Canvas("HandOver");
        auto action = Canvas("HandOverAction");
        ASSERT_TRUE(plate && action);
        ASSERT_TRUE(plate->TakesInput() && action->TakesInput());
        EXPECT_EQ(action->GetLayout().params.space, CanvasSpace::Screen);
        EXPECT_EQ(action->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);
        EXPECT_GE(action->GetLayout().params.minScale, 0.6f) << "the floor that keeps the 56 unit button a touch target";
        EXPECT_EQ(EngineTouchMinimum(), kEngineTouchMinimum);

        const TableLayout layout = MakeWorstCaseTableLayout();
        size_t checked = 0;
        size_t withoutInsets = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            withoutInsets += window.HasInsets() ? 0u : 1u;
            // The plate (SceneView) and the button (Screen, ScaleWithScreen, user scale 1.0), both in view units of the table
            LayOutInWindow(*plate, window);
            const UIElement* plateElement = Element("HandOver", "Plate");
            const UIElement* headlineElement = Element("HandOver", "Headline");
            ASSERT_TRUE(plateElement != nullptr && headlineElement != nullptr);
            ASSERT_FALSE(plateElement->rect.IsEmpty()) << window.label.c_str();
            const Rect plateRect = ToView(plateElement->rect, *plate, window);
            const Rect headlineRect = ToView(headlineElement->rect, *plate, window);

            LayOutInWindow(*action, window, CanvasScaleMode::ScaleWithScreen, 1.0f);
            const UIElement* button = Element("HandOverAction", "NextHand");
            ASSERT_NE(button, nullptr);
            ASSERT_FALSE(button->rect.IsEmpty()) << window.label.c_str();
            const Rect buttonRect = ToView(button->rect, *action, window);
            // The button never touches the headline band. A top notch (7 % of the short side) pushes it down by the inset, and in a 16:9
            // window that is onto the top padding of the plate (the safe area's call, as for New game): then it stays inside the 16 units.
            EXPECT_FALSE(Intersects(buttonRect, headlineRect)) << window.label.c_str() << ": the button touches the headline";
            if (window.simulation == SafeAreaSimulation::TopNotch) {
                if (Intersects(buttonRect, plateRect)) {
                    EXPECT_LE(buttonRect.y + buttonRect.h, plateRect.y + kModalPadding + 0.01f) << window.label.c_str() << ": the button reaches into the plate";
                }
            } else {
                EXPECT_FALSE(Intersects(buttonRect, plateRect)) << window.label.c_str() << ": the button touches the plate";
            }

            // Inside the window (the validator says the same for every mode and user scale)
            const Rect output = action->GetLayout().GetResolved().transform.ToOutput(button->rect.x, button->rect.y, button->rect.w, button->rect.h);
            EXPECT_GE(output.x, -0.01f) << window.label.c_str();
            EXPECT_GE(output.y, -0.01f) << window.label.c_str();
            EXPECT_LE(output.x + output.w, window.outputWidth + 0.01f) << window.label.c_str();
            EXPECT_LE(output.y + output.h, window.outputHeight + 0.01f) << window.label.c_str();
            // A touch target as the user sees it (the same rule as New game, which has its corner)
            EXPECT_GE(std::min(output.w, output.h) / window.pixelDensity, EngineTouchMinimum() - 0.01f) << window.label.c_str();

            if (window.HasInsets()) {
                return;  // with insets the button lies nearer the table, as the HUD does
            }
            ExpectClearOfTable(buttonRect, layout, window.label.c_str());
            ++checked;
        });
        EXPECT_GT(checked, 0u);
        EXPECT_EQ(checked, withoutInsets) << "every window without insets was checked";
    }

    namespace {

        /// Whether the compact row, which keeps to the safe area and so lies above the bottom inset, can clear the table in this window: the bar
        /// under the table less the inset holds the row at its height there (`rowHeightUnits` of the laid-out `ActionRow`: a wrapped stack
        /// starts its lines at its top, so the row is as high as it is given, not as the lines it uses) at the user scale of the layout.
        bool RowCanClearTheTable(const MatrixWindow& window, float rowHeightUnits, float userScale = 1.0f) {
            const float bottomInset = window.HasInsets() ? window.outputHeight - (window.safeArea.y + window.safeArea.h) : 0.0f;
            return BarUnderTable(window.outputWidth, window.outputHeight) - bottomInset >= rowHeightUnits * CompactScale(window.outputWidth, window.outputHeight, userScale);
        }

        /// The windows where the rule shows the compact row and the safe area still lifts it onto the table: those with side insets (the only
        /// simulation with a bottom inset, 4 % of the short side), where the bar holds the row but not the row above the inset. The rule takes the
        /// bar without the inset (the same for a window with insets and without), and an engine change would fix it (the file's comment has it).
        /// The matrix has the 3:2 window of 720 x 480 (a bar of 37.5 px, an inset of 19.2 px, a line of 34.3 px) and the 1:1 windows of 480 and 720
        /// at the user scale 1.5; the row then lies on the viewer's hand by a few pixels, never by more than the inset.
        bool SafeAreaLiftsTheRowOntoTheTable(const MatrixWindow& window) {
            return window.simulation == SafeAreaSimulation::SideInsets;
        }

    }  // namespace

    // ----- the action buttons in the narrow windows -----

    class MahjongCompactRowTest : public MahjongTouchSizeTest {
    protected:
        /// Shows the buttons of `kinds` (with their pictures) in the compact canvas and no others, and the hint button, which ends every set
        /// (the hints offer it in a turn and in a claim window); the canvas is shown by hand.
        void ShowCompactButtons(UICanvasComponent& compact, const std::vector<ButtonKind>& kinds) {
            ASSERT_TRUE(compact.SetElementVisible("HintButton"_intern, true));
            for (int i = 0; i < kButtonCount; ++i) {
                const ButtonSpec& spec = SpecOf(static_cast<ButtonKind>(i));
                const bool shown = std::find(kinds.begin(), kinds.end(), static_cast<ButtonKind>(i)) != kinds.end();
                ASSERT_TRUE(compact.SetElementVisible(PoolString::Intern(spec.element), shown)) << spec.element;
                for (int t = 0; t < spec.maxTiles; ++t) {
                    ASSERT_TRUE(compact.SetElementVisible(PoolString::Intern(TileImageName(static_cast<ButtonKind>(i), t)), shown));
                }
            }
        }

        /// `rect` (in the units of the canvas) lies inside the output of `window` once the canvas has been laid out in it.
        static void ExpectInsideTheWindow(UICanvasComponent& canvas, const Rect& rect, const MatrixWindow& window, const std::string& label) {
            const Rect output = canvas.GetLayout().GetResolved().transform.ToOutput(rect.x, rect.y, rect.w, rect.h);
            EXPECT_GE(output.x, -0.01f) << label;
            EXPECT_GE(output.y, -0.01f) << label;
            EXPECT_LE(output.x + output.w, window.outputWidth + 0.01f) << label;
            EXPECT_LE(output.y + output.h, window.outputHeight + 0.01f) << label;
        }

        /// The buttons and pictures of `kinds`, and the hint button, in the compact canvas laid out in the window lie clear of the table and inside the window.
        void ExpectCompactRowClearOfTheTable(UICanvasComponent& compact, const MatrixWindow& window, const std::vector<ButtonKind>& kinds, const TableLayout& layout, float userScale = 1.0f) {
            LayOutInWindow(compact, window, CanvasScaleMode::ScaleWithScreen, userScale);
            // The whole row, all its lines: the stack is as high as it is given, and it lies clear of the table
            const UIElement* row = Element("ActionsCompact", "ActionRow");
            ASSERT_NE(row, nullptr);
            ExpectClearOfTable(ToView(row->rect, compact, window), layout, (std::string(window.label.c_str()) + " ActionRow").c_str());
            for (const ButtonKind kind : kinds) {
                const ButtonSpec& spec = SpecOf(kind);
                const UIElement* button = Element("ActionsCompact", spec.element);
                ASSERT_NE(button, nullptr) << spec.element;
                ASSERT_FALSE(button->rect.IsEmpty()) << window.label.c_str() << " " << spec.element;
                const std::string label = std::string(window.label.c_str()) + " " + spec.element;
                ExpectClearOfTable(ToView(button->rect, compact, window), layout, label.c_str());
                for (int t = 0; t < spec.maxTiles; ++t) {
                    const UIElement* picture = Element("ActionsCompact", TileImageName(kind, t).c_str());
                    ASSERT_NE(picture, nullptr);
                    ExpectClearOfTable(ToView(picture->rect, compact, window), layout, label.c_str());
                }
                // Inside the window (the validator says the same for every mode and user scale)
                ExpectInsideTheWindow(compact, button->rect, window, label);
            }
            // The hint button ends the row in every set
            const UIElement* hint = Element("ActionsCompact", "HintButton");
            ASSERT_NE(hint, nullptr);
            ASSERT_FALSE(hint->rect.IsEmpty()) << window.label.c_str() << " HintButton";
            const std::string hintLabel = std::string(window.label.c_str()) + " HintButton";
            ExpectClearOfTable(ToView(hint->rect, compact, window), layout, hintLabel.c_str());
            ExpectInsideTheWindow(compact, hint->rect, window, hintLabel);
        }
    };

    TEST_F(MahjongCompactRowTest, TheCompactActionButtonsLieOnTheBarUnderTheTableInsideTheWindowWhereTheRuleShowsThem) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto compact = Canvas("ActionsCompact");
        ASSERT_TRUE(compact);
        // Hidden in the file; shown by hand, and no draw after it (a draw would hide it again at the design size)
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "ActionsCompact"_intern, true));

        const TableLayout layout = MakeWorstCaseTableLayout();
        size_t checked = 0;
        size_t checkedAtOnePointFive = 0;
        size_t sideInsets = 0;
        size_t liftedOntoTheTable = 0;
        for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
            ASSERT_NO_FATAL_FAILURE(ShowCompactButtons(*compact, kinds));
            ForEachMatrixWindow([&](const MatrixWindow& window) {
                // The rule is asked at the user scale of the layout: a bigger scale makes the canvas smaller and the row higher
                for (const float userScale : {1.0f, 1.5f}) {
                    if (!IsCompactWindow(window, userScale)) {
                        continue;
                    }
                    if (window.simulation == SafeAreaSimulation::SideInsets) {
                        ++sideInsets;
                    }
                    // The row keeps to the safe area: it is checked wherever the bar less the bottom inset holds the row at its height there, and
                    // the one window where it does not is the known deviation of the file (see `SafeAreaLiftsTheRowOntoTheTable`)
                    LayOutInWindow(*compact, window, CanvasScaleMode::ScaleWithScreen, userScale);
                    const UIElement* row = Element("ActionsCompact", "ActionRow");
                    ASSERT_NE(row, nullptr);
                    if (!RowCanClearTheTable(window, row->rect.h, userScale)) {
                        ++liftedOntoTheTable;
                        EXPECT_TRUE(SafeAreaLiftsTheRowOntoTheTable(window)) << window.label.c_str() << " x" << userScale << ": a new window where the row lies on the table";
                        EXPECT_LE(row->rect.h * CompactScale(window.outputWidth, window.outputHeight, userScale), BarUnderTable(window.outputWidth, window.outputHeight))
                            << window.label.c_str() << " x" << userScale << ": the bar holds the row, so it lies on the table by the inset at the most";
                        continue;
                    }
                    ExpectCompactRowClearOfTheTable(*compact, window, kinds, layout, userScale);
                    ++checked;
                    if (userScale > 1.0f) {
                        ++checkedAtOnePointFive;
                    }
                }
            });
        }
        EXPECT_GT(checked, 0u);
        EXPECT_GT(checkedAtOnePointFive, 0u) << "the rule says compact in some windows at the user scale 1.5 too";
        EXPECT_GT(sideInsets, 0u);
        EXPECT_GT(liftedOntoTheTable, 0u);
    }

    TEST_F(MahjongCompactRowTest, TheRuleIsTheHeightOfTheRowTheFileLaysOutInEveryWindowOfTheMatrixAtBothUserScales) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto compact = Canvas("ActionsCompact");
        ASSERT_TRUE(compact);
        size_t compactWindows = 0;
        // The windows (and user scales) where the rule keeps `Actions` under the minimum, each with its number of simulations
        eastl::map<eastl::string, size_t> keptWindows;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            for (const float userScale : {1.0f, 1.5f}) {
                const UICanvasResolved resolved = LayOutInWindow(*compact, window, CanvasScaleMode::ScaleWithScreen, userScale);
                const UIElement* row = Element("ActionsCompact", "ActionRow");
                ASSERT_NE(row, nullptr);
                const std::string label = std::string(window.label.c_str()) + " x" + std::to_string(userScale);
                // The scale and the row height of the rule are those of the engine's layout of the file
                EXPECT_NEAR(resolved.scale, CompactScale(window.outputWidth, window.outputHeight, userScale), 0.0001f) << label;
                EXPECT_FLOAT_EQ(row->rect.h, CompactRowHeight(window.outputWidth / resolved.scale)) << label;
                // The verdict is the comparison of the bar with that row (the bar of the table, not of the safe area)
                const bool fitsTheBar = BarUnderTable(window.outputWidth, window.outputHeight) >= row->rect.h * resolved.scale;
                EXPECT_EQ(IsCompactWindow(window, userScale), ActionButtonsAreUnderTheMinimum(window) && fitsTheBar) << label;
                if (IsCompactWindow(window, userScale)) {
                    ++compactWindows;
                } else if (ActionButtonsAreUnderTheMinimum(window)) {
                    ++keptWindows[Format("{}x{} x{}", static_cast<int>(window.outputWidth), static_cast<int>(window.outputHeight), userScale > 1.0f ? "1.5" : "1.0").c_str()];
                }
            }
        });
        EXPECT_GT(compactWindows, 0u);
        // A new window that keeps `Actions` under the minimum has to be named here and in CLAUDE.md: 640 x 480 and 720 x 480 at the user
        // scale 1.5 (at 1.0 both are compact), each in its three simulations (no insets, a notch, side insets)
        const eastl::map<eastl::string, size_t> expectedKept{{"640x480 x1.5", 3u}, {"720x480 x1.5", 3u}};
        EXPECT_EQ(keptWindows, expectedKept);
    }

    TEST_F(MahjongCompactRowTest, InAWindowOutsideTheMatrixEitherActionsStaysOrTheCompactRowIsClearOfTheTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto compact = Canvas("ActionsCompact");
        auto actions = Canvas("Actions");
        ASSERT_TRUE(compact && actions);
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "ActionsCompact"_intern, true));
        const TableLayout layout = MakeWorstCaseTableLayout();
        // 600 x 420 and 480 x 360 have a bar of a line (41 and 45 px) but not of the row (72 px); 627 x 480 and 626 x 480 are either side of the
        // width where the row is one line (1043.91 units at the floor scale, 626.35 px; a bar of 63.7 and 63.9 px)
        const MatrixWindow windows[] = {WindowOf(731.0f, 411.0f), WindowOf(720.0f, 405.0f), WindowOf(700.0f, 420.0f), WindowOf(1000.0f, 400.0f), WindowOf(640.0f, 360.0f), WindowOf(640.0f, 428.0f),
                                        WindowOf(640.0f, 427.0f), WindowOf(640.0f, 480.0f), WindowOf(600.0f, 420.0f), WindowOf(480.0f, 360.0f),  WindowOf(500.0f, 400.0f), WindowOf(627.0f, 480.0f),
                                        WindowOf(626.0f, 480.0f), WindowOf(600.0f, 700.0f), WindowOf(480.0f, 700.0f), WindowOf(1000.0f, 700.0f)};
        MatrixWindow phone = WindowOf(2532.0f, 1170.0f);  // a landscape phone at density 3: 844 x 390 window units, a button of 30.3 units
        phone.pixelDensity = 3.0f;

        size_t compactWindows = 0;
        size_t actionsWindows = 0;
        auto check = [&](const MatrixWindow& window, float userScale) {
            const std::string label = std::string(window.label.c_str()) + " " + std::to_string(static_cast<int>(window.outputWidth)) + "x" + std::to_string(static_cast<int>(window.outputHeight)) +
                                      " x" + std::to_string(userScale);
            const UICanvasResolved resolved = LayOutInWindow(*compact, window, CanvasScaleMode::ScaleWithScreen, userScale);
            const float rowPixels = Element("ActionsCompact", "ActionRow")->rect.h * resolved.scale;
            if (IsCompactWindow(window, userScale)) {
                ++compactWindows;
                EXPECT_GE(BarUnderTable(window.outputWidth, window.outputHeight), rowPixels) << label << ": the bar holds the whole row";
                ASSERT_TRUE(RowCanClearTheTable(window, Element("ActionsCompact", "ActionRow")->rect.h, userScale)) << label;
                for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
                    ASSERT_NO_FATAL_FAILURE(ShowCompactButtons(*compact, kinds));
                    ExpectCompactRowClearOfTheTable(*compact, window, kinds, layout, userScale);
                }
                return;
            }
            // `Actions` stays: the canvas of the table is clear of every slot (its rect is in view units whatever the window) and the table
            // fills the height or the width, so nothing of the compact row is shown over the viewer's hand
            ++actionsWindows;
            // Either the buttons are big enough or the bar does not hold the row (a bar of a line is not enough: the row is as high as its width makes it)
            if (ActionButtonsAreUnderTheMinimum(window)) {
                EXPECT_LT(BarUnderTable(window.outputWidth, window.outputHeight), rowPixels) << label;
            }
            LayOutInWindow(*actions, window);
            for (int i = 0; i < kButtonCount; ++i) {
                const UIElement* button = Element("Actions", SpecOf(static_cast<ButtonKind>(i)).element);
                ASSERT_NE(button, nullptr);
                ExpectClearOfTable(ToView(button->rect, *actions, window), layout, label.c_str());
            }
        };
        for (const float userScale : {1.0f, 1.5f}) {
            for (const MatrixWindow& window : windows) {
                check(window, userScale);
            }
            check(phone, userScale);
        }

        // The windows are told apart as the rule says
        EXPECT_FALSE(IsCompactWindow(WindowOf(731.0f, 411.0f)));
        EXPECT_FALSE(IsCompactWindow(WindowOf(720.0f, 405.0f)));
        EXPECT_FALSE(IsCompactWindow(WindowOf(700.0f, 420.0f)));
        EXPECT_FALSE(IsCompactWindow(WindowOf(1000.0f, 400.0f)));
        EXPECT_FALSE(IsCompactWindow(phone));
        EXPECT_TRUE(IsCompactWindow(WindowOf(640.0f, 428.0f))) << "a bar of 34 px for a line of 33.6 px";
        EXPECT_FALSE(IsCompactWindow(WindowOf(640.0f, 427.0f))) << "a bar of 33.5 px";
        EXPECT_TRUE(IsCompactWindow(WindowOf(640.0f, 480.0f))) << "1066.7 units wide: one line of 33.6 px on a bar of 60 px";
        EXPECT_FALSE(IsCompactWindow(WindowOf(640.0f, 480.0f), 1.5f)) << "739 units wide: a row of 104 px on a bar of 60 px";
        EXPECT_FALSE(IsCompactWindow(WindowOf(600.0f, 420.0f)));
        EXPECT_FALSE(IsCompactWindow(WindowOf(480.0f, 360.0f)));
        EXPECT_FALSE(IsCompactWindow(WindowOf(500.0f, 400.0f)));
        EXPECT_TRUE(IsCompactWindow(WindowOf(627.0f, 480.0f)));
        EXPECT_FALSE(IsCompactWindow(WindowOf(626.0f, 480.0f)));
        EXPECT_TRUE(IsCompactWindow(WindowOf(650.0f, 500.0f)));
        EXPECT_FALSE(IsCompactWindow(WindowOf(650.0f, 500.0f), 1.5f));
        EXPECT_TRUE(IsCompactWindow(WindowOf(480.0f, 480.0f)));
        EXPECT_TRUE(IsCompactWindow(WindowOf(480.0f, 480.0f), 1.5f));
        EXPECT_TRUE(IsCompactWindow(WindowOf(480.0f, 700.0f)));
        EXPECT_GT(compactWindows, 0u);
        EXPECT_GT(actionsWindows, 0u);
    }

    TEST_F(MahjongTouchSizeTest, EveryWindowOfTheMatrixIsCompactExactlyWhereAButtonOfActionsIsBelowTheTouchMinimumButTheOneWhoseBarHoldsNoRow) {
        // Every window of the matrix is one or the other: the buttons of `Actions` are 56 units of the table's fit, the rule says where that is too
        // small and the bar under the table (480 px on the short side at the least) holds the row. At the user scale 1.0 every small window does
        // (the 4:3 window of 640 x 480 has a canvas of 1066.7 units, one line of 33.6 px on a bar of 60 px); at 1.5 the canvas is smaller and the
        // row two lines: `Actions` stays, with buttons of 28 px, in 640 x 480 (739 units, a row of 104 px on a bar of 60 px) and in the 3:2 window of
        // 720 x 480 (31.5 px; 783.8 units, a row of 110 px on a bar of 37.5 px)
        size_t compactWindows = 0;
        size_t roomyWindows = 0;
        size_t smallWindows = 0;
        size_t smallAtOnePointFive = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            const bool small = ActionButtonsAreUnderTheMinimum(window);
            const bool fourToThree = window.outputWidth == 640.0f && window.outputHeight == 480.0f;
            const bool threeToTwo = window.outputWidth == 720.0f && window.outputHeight == 480.0f;
            EXPECT_EQ(IsCompactWindow(window), small) << window.label.c_str();
            EXPECT_FALSE(ActionsStaysSmall(window, 1.0f)) << window.label.c_str();
            EXPECT_EQ(ActionsStaysSmall(window, 1.5f), fourToThree || threeToTwo) << window.label.c_str() << " x1.5";
            EXPECT_EQ(ActionsMayBeSmall(window), small) << window.label.c_str() << ": the touch size of Actions is left out exactly where its buttons are small";
            if (IsCompactWindow(window)) {
                ++compactWindows;
            } else if (small) {
                ++smallWindows;
            } else {
                ++roomyWindows;
            }
            if (ActionsStaysSmall(window, 1.5f)) {
                ++smallAtOnePointFive;
            }
        });
        EXPECT_GT(compactWindows, 0u);
        EXPECT_GT(roomyWindows, 0u);
        EXPECT_EQ(smallWindows, 0u) << "at the user scale 1.0 no small window keeps Actions";
        EXPECT_EQ(smallAtOnePointFive, 6u) << "640 x 480 and 720 x 480, each in the three simulations";
    }

    // ----- the whole scene -----

    namespace {

        /// The names of the interactive elements (those that take clicks: buttons, toggles, segments) of `element` and below.
        void CollectInteractive(const UIElement& element, std::set<std::string>& names) {
            if (element.interactive) {
                names.insert(std::string(element.name.ToStringView().data(), element.name.ToStringView().size()));
            }
            for (const auto& child : element.children) {
                CollectInteractive(*child, names);
            }
        }

        std::set<std::string> InteractiveOf(UICanvasComponent& canvas) {
            std::set<std::string> names;
            CollectInteractive(canvas.GetRoot(), names);
            return names;
        }

        /// The least floor on the scale of a `Screen` canvas that holds a control (56 units are then 33.6 px of the 32 the engine asks for).
        constexpr float kLeastTouchFloor = 0.6f;

    }  // namespace

    TEST_F(MahjongTouchSizeTest, EveryInteractiveElementOfTheSceneIsInAScreenCanvasWithAFloorOrInOneOfTheTwoSceneViewCanvasesTheCompactRowReplaces) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        size_t canvases = 0;
        size_t controls = 0;
        for (const auto& child : _manager->GetSceneByName("MahjongScene"_intern)->GetRootNode().GetChildren()) {
            const auto canvas = child->GetComponent<UICanvasComponent>();
            if (!canvas) {
                continue;
            }
            ++canvases;
            const std::string node(child->GetName().ToStringView().data(), child->GetName().ToStringView().size());
            const std::set<std::string> names = InteractiveOf(*canvas);
            controls += names.size();
            if (names.empty()) {
                continue;
            }
            canvas->UpdateLayout(kViewWidth, kViewHeight);  // the settings of the file reach the layout on its first update, shown or not
            const UICanvasScaleParams& params = canvas->GetLayout().params;
            // `Actions` and `HintsButton` are the SceneView canvases the component hides where `ActionsCompact` (a Screen canvas) replaces them
            if (node == "Actions" || node == "HintsButton") {
                EXPECT_EQ(params.space, CanvasSpace::SceneView) << node;
                continue;
            }
            EXPECT_EQ(params.space, CanvasSpace::Screen) << node << " holds " << *names.begin()
                                                         << ": a control of a SceneView canvas is as small as the table in a small window, so it belongs in a Screen canvas with a floor on its scale "
                                                         << "(like HudMenu or ActionsCompact), or in the compact row if it is an action button";
            EXPECT_GE(params.minScale, kLeastTouchFloor) << node << " needs a minScale of at least " << kLeastTouchFloor << ": " << *names.begin();
        }
        EXPECT_GE(canvases, 10u);
        EXPECT_GT(controls, 30u);
    }

    TEST_F(MahjongTouchSizeTest, TheCompactRowHoldsExactlyTheInteractiveElementsOfActionsAndOfTheHintButtonByName) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const auto actions = Canvas("Actions");
        const auto hints = Canvas("HintsButton");
        const auto compact = Canvas("ActionsCompact");
        ASSERT_TRUE(actions && hints && compact);
        std::set<std::string> expected = InteractiveOf(*actions);
        const std::set<std::string> hintNames = InteractiveOf(*hints);
        EXPECT_FALSE(hintNames.empty());
        expected.insert(hintNames.begin(), hintNames.end());
        EXPECT_EQ(InteractiveOf(*compact), expected) << "a button added to Actions or to HintsButton needs its copy in ActionsCompact, or the narrow windows lose it";
    }

}  // namespace BECore::Mahjong::Tests
