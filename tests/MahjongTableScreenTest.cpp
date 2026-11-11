#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Text/Utf8.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIToggle.h>
#include <Mahjong/Table/SeatName.h>

/**
 * The canvases of the shipped scene that belong to the table (`Actions`, `Handoff`): the buttons of the component are the buttons of the
 * file with their action and value, the words are keys of the string table, the longest labels fit in every language, both pass the size
 * matrix once per language, and the buttons lie where no tile of any game can.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The height of an action button of `Actions` in view units (the one place is `kActionButtonHeight`): 32 px, the engine's touch
        /// minimum, from a window of 732 x 412 px; below that the buttons are in `ActionsCompact`.
        constexpr float kButtonHeight = kActionButtonHeight;
        /// The smallest window that keeps `Actions`: one pixel above the boundary of `NeedsCompactActions` (731.4 x 411.4), whose
        /// aspect is 16:9.
        constexpr float kSmallestActionsWindowWidth = 732.0f;
        constexpr float kSmallestActionsWindowHeight = 412.0f;
        /// The height of the line of the word at the top of a button; the pictures of tiles lie below it.
        constexpr float kWordLineHeight = 24.0f;
        /// The width of an action button in view units.
        constexpr float kButtonWidth = 104.0f;
        /// The smallest tile picture in view units (the 3:4 of an atlas cell), so a tile can be told at the smallest window of the matrix.
        constexpr float kLeastTileWidth = 20.0f;
        constexpr float kLeastTileHeight = 26.0f;
        /// Output sizes the hint is asked in: the design size, a small window, the narrowest windows.
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}};
        /// The windows the words of the compact canvas are asked in: the design size, the windows below the rule (640 x 480, 480 x 480,
        /// 480 x 853: `NeedsCompactActions` is true there), two that are not and the widest (1707 x 480).
        constexpr Output kCompactOutputs[] = {{1280.0f, 720.0f}, {640.0f, 480.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};
        constexpr float kTileShape = 3.0f / 4.0f;
        constexpr float kShapeTolerance = 0.02f;
        /// The gap between the buttons of a row, and the width of a row of four buttons of 104 with three gaps (the row wraps at it).
        constexpr float kButtonGap = 8.0f;
        constexpr float kRowWidth = 4.0f * kButtonWidth + 3.0f * kButtonGap;

        /// The role of the colour of a button (an entry of `kUiPaletteNames`), by what the button does.
        const char* RoleOf(ButtonKind kind) {
            switch (kind) {
                case ButtonKind::Win:
                case ButtonKind::ClaimMahjong:
                    return "primary";
                case ButtonKind::ClaimPung:
                case ButtonKind::ClaimKong:
                case ButtonKind::ClaimChow0:
                case ButtonKind::ClaimChow1:
                case ButtonKind::ClaimChow2:
                    return "secondary";
                case ButtonKind::Pass:
                    return "danger";
                case ButtonKind::ConcealedKong0:
                case ButtonKind::ConcealedKong1:
                case ButtonKind::ConcealedKong2:
                case ButtonKind::UpgradeKong:
                case ButtonKind::Declare:
                    return "neutral";
            }
            return "";
        }

    }  // namespace

    class MahjongTableScreenTest : public MahjongTableTestBase {
    protected:
        /// The names of all the elements of the canvas on `node` that have one.
        std::set<std::string> ElementNamesOf(const char* node) const {
            std::set<std::string> names;
            const auto canvas = Canvas(node);
            EXPECT_TRUE(canvas) << node;
            std::vector<const UIElement*> pending;
            if (canvas) {
                pending.push_back(&canvas->GetRoot());
            }
            while (!pending.empty()) {
                const UIElement* element = pending.back();
                pending.pop_back();
                const std::string name = element->name.ToStringView().data();
                if (!name.empty()) {
                    names.insert(name);
                }
                for (const auto& child : element->children) {
                    pending.push_back(child.Get());
                }
            }
            return names;
        }

        /// Every button of the canvas on `node` is at least `buttonWidth` x `kButtonHeight` units and holds its pictures below the line of
        /// the word, side by side, in the shape of a tile, at least 20 x 26 units (laid out at the design size: a Screen canvas of the
        /// scene has the scale 1 there).
        void ExpectPicturesInTheirButtons(const char* node, float buttonWidth) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), true, node);
            }
            auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            ASSERT_TRUE(UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), PoolString::Intern(node), true));
            canvas->UpdateLayout(kViewWidth, kViewHeight);

            for (int i = 0; i < kButtonCount; ++i) {
                const ButtonKind kind = static_cast<ButtonKind>(i);
                const ButtonSpec& spec = SpecOf(kind);
                const UIElement* button = Element(node, spec.element);
                ASSERT_NE(button, nullptr) << spec.element;
                const Rect& outer = button->rect;
                EXPECT_GE(outer.w, buttonWidth) << spec.element;
                EXPECT_GE(outer.h, kButtonHeight) << spec.element;
                const UIElement* previous = nullptr;
                for (int t = 0; t < spec.maxTiles; ++t) {
                    const std::string name = TileImageName(kind, t).c_str();
                    const UIElement* picture = Element(node, name.c_str());
                    ASSERT_NE(picture, nullptr) << name;
                    const Rect& r = picture->rect;
                    EXPECT_GE(r.x, outer.x) << name;
                    EXPECT_LE(r.x + r.w, outer.x + outer.w) << name;
                    EXPECT_GE(r.y, outer.y + kWordLineHeight) << name << " lies below the line of the word";
                    EXPECT_LE(r.y + r.h, outer.y + outer.h) << name;
                    EXPECT_GE(r.w, kLeastTileWidth) << name;
                    EXPECT_GE(r.h, kLeastTileHeight) << name;
                    EXPECT_NEAR(r.w / r.h, kTileShape, kShapeTolerance) << name;
                    if (previous != nullptr) {
                        EXPECT_GE(r.x, previous->rect.x + previous->rect.w) << name << " does not overlap the tile before it";
                    }
                    previous = picture;
                }
            }
        }

        /// Shows or hides a button of the canvas on `node` (`Actions` or `ActionsCompact`) with all the pictures of its tiles.
        void SetShown(ButtonKind kind, bool shown, const char* node = "Actions") {
            const ButtonSpec& spec = SpecOf(kind);
            auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(spec.element), shown)) << spec.element;
            for (int t = 0; t < spec.maxTiles; ++t) {
                ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(TileImageName(kind, t)), shown)) << spec.element;
            }
        }

        /// The longest text (by code points, the measure of a line of text in any language) of `candidates`.
        static eastl::string Longest(const std::vector<eastl::string>& candidates) {
            eastl::string longest;
            size_t most = 0;
            for (const eastl::string& candidate : candidates) {
                if (Utf8::CountCodePoints(candidate) > most) {
                    most = Utf8::CountCodePoints(candidate);
                    longest = candidate;
                }
            }
            return longest;
        }
    };

    TEST_F(MahjongTableScreenTest, EveryButtonOfTheComponentIsInTheSceneFileWithItsActionAndValue) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            const UIElement* element = Element("Actions", spec.element);
            ASSERT_NE(element, nullptr) << spec.element;
            if (kind == ButtonKind::Declare) {
                // The declaration is a toggle: it emits the state it asks for, `on` or `off` (`kDeclareOn`, `kDeclareOff`)
                const auto* toggle = element->Cast<UIToggle>();
                ASSERT_NE(toggle, nullptr);
                EXPECT_EQ(toggle->action, PoolString::Intern(spec.action));
                EXPECT_EQ(toggle->onValue, PoolString::Intern(kDeclareOn.data()));
                EXPECT_EQ(toggle->offValue, PoolString::Intern(kDeclareOff.data()));
                EXPECT_EQ(toggle->bind, "declareNext"_intern);
                EXPECT_EQ(toggle->text, eastl::string("{#") + spec.wordKey + "}") << "the toggle says the word of the string table";
                continue;
            }
            const auto* button = element->Cast<UIButton>();
            ASSERT_NE(button, nullptr) << spec.element;
            EXPECT_EQ(button->action, PoolString::Intern(spec.action)) << spec.element;
            EXPECT_EQ(button->value, PoolString::Intern(spec.value)) << spec.element;
            EXPECT_EQ(button->text, eastl::string("{#") + spec.wordKey + "}") << spec.element << " says a word of the string table";
            // The pictures of its tiles: `maxTiles` hidden images named after the button, and no more
            int images = 0;
            for (const auto& child : button->children) {
                EXPECT_NE(child->Cast<UIImage>(), nullptr) << spec.element << " has only pictures as children";
                ++images;
            }
            EXPECT_EQ(images, spec.maxTiles) << spec.element;
            for (int t = 0; t < kMaxButtonTiles; ++t) {
                const std::string name = TileImageName(kind, t).c_str();
                EXPECT_EQ(Element("Actions", name.c_str()) != nullptr, t < spec.maxTiles) << name;
            }
        }
        EXPECT_NE(Element("Handoff", "Reveal"), nullptr);
        EXPECT_EQ(Element("Handoff", "Reveal")->Cast<UIButton>()->action, "MahjongReveal"_intern);
        EXPECT_EQ(Element("Handoff", "Reveal")->Cast<UIButton>()->text, eastl::string("{#cover.show}"));
        EXPECT_EQ(Element("Handoff", "HandoffText")->Cast<UILabel>()->text, eastl::string("{#cover.text}"));
    }

    TEST_F(MahjongTableScreenTest, EveryActionButtonHasTheColourOfItsRoleAndTheRowKeepsTheGapOfTheGrid) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            const UIElement* element = Element("Actions", spec.element);
            ASSERT_NE(element, nullptr) << spec.element;
            EXPECT_TRUE(SameColor(element->color, PaletteColor(RoleOf(kind)))) << spec.element << " is a " << RoleOf(kind) << " button";
        }

        const UIElement* row = Element("Actions", "ActionRow");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->style.stack.spacing, kButtonGap);
        ASSERT_TRUE(Canvas("Actions")->SetElementVisible("Hint"_intern, true));  // a hidden element has no rect
        Canvas("Actions")->UpdateLayout(kViewWidth, kViewHeight);
        EXPECT_EQ(row->rect.w, kRowWidth) << "four buttons and three gaps fit a row";
        const UIElement* hint = Element("Actions", "Hint");
        ASSERT_NE(hint, nullptr);
        EXPECT_TRUE(SameColor(hint->color, PaletteColor("scrim")));
    }

    TEST_F(MahjongTableScreenTest, EveryElementNameOfTheActionsCanvasIsUniqueAndNoLabelVariableIsLeft) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        std::set<std::string> names;
        std::vector<const UIElement*> pending{&canvas->GetRoot()};
        while (!pending.empty()) {
            const UIElement* element = pending.back();
            pending.pop_back();
            const std::string name = element->name.ToStringView().data();
            if (!name.empty()) {
                EXPECT_TRUE(names.insert(name).second) << "duplicate name " << name;
            }
            if (const auto* label = element->Cast<UILabel>()) {
                // Only the hint follows a variable; the words of the buttons are keys of the string table
                EXPECT_EQ(std::string(label->text.c_str()).find('{') == std::string::npos, name != "Hint") << name << " and its variable";
            }
            for (const auto& child : element->children) {
                pending.push_back(child.Get());
            }
        }
    }

    TEST_F(MahjongTableScreenTest, TheActionButtonsPassTheSizeMatrixInEveryLanguageWithTheirWordsAndPicturesAndEveryButtonIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);

        // Every button with all its pictures, once per language of the table (a language added to the file is covered without touching the test)
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
                for (int i = 0; i < kButtonCount; ++i) {
                    SetShown(static_cast<ButtonKind>(i), false);
                }
                for (const ButtonKind kind : kinds) {
                    SetShown(kind, true);
                }
                // The touch size is held where the rule keeps `Actions` (but for the windows of 640 x 480 and 720 x 480, see `ActionsMayBeSmall`); elsewhere `ActionsCompact` holds it (below)
                ExpectMatrixExceptTouchWhere(*canvas, "Actions", EngineTouchMinimum(), ActionsMayBeSmall);
                for (const Output& output : kOutputs) {
                    ExpectWordsFit("Actions", output.width, output.height, 0.0f);
                }
            }
        });
    }

    TEST_F(MahjongTableScreenTest, EveryTilePictureLiesInsideItsButtonBelowTheWordAndKeepsTheShapeOfATile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ExpectPicturesInTheirButtons("Actions", kButtonWidth);
    }

    TEST_F(MahjongTableScreenTest, EveryTilePictureOfTheCompactButtonsLiesInsideItsButtonBelowTheWordAndKeepsTheShapeOfATile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ExpectPicturesInTheirButtons("ActionsCompact", kCompactButtonWidth);
    }

    TEST_F(MahjongTableScreenTest, EveryActionButtonIsATouchTargetFromTheSmallestWindowThatKeepsTheActionsCanvas) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const float minTouch = EngineTouchMinimum();
        // The rule: this window keeps `Actions`, one pixel less does not
        ASSERT_FALSE(NeedsCompactActions(kSmallestActionsWindowWidth, kSmallestActionsWindowHeight, 1.0f, minTouch));
        // One pixel narrower is too small; the window is taller here, so the bar under the table holds the compact row
        ASSERT_TRUE(NeedsCompactActions(kSmallestActionsWindowWidth - 1.0f, 2000.0f, 1.0f, minTouch));

        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kButtonCount; ++i) {
            canvas->SetElementVisible(PoolString::Intern(SpecOf(static_cast<ButtonKind>(i)).element), true);
        }
        canvas->UpdateLayout(kViewWidth, kViewHeight);

        const float fit = FitScale(kSmallestActionsWindowWidth, kSmallestActionsWindowHeight);  // the scale of the table, and of this canvas, there
        for (int i = 0; i < kButtonCount; ++i) {
            const UIElement* element = Element("Actions", SpecOf(static_cast<ButtonKind>(i)).element);
            ASSERT_NE(element, nullptr);
            EXPECT_FLOAT_EQ(element->rect.h, kActionButtonHeight) << SpecOf(static_cast<ButtonKind>(i)).element << ": the scene file and TouchRegime.h agree";
            EXPECT_GE(element->rect.h * fit, minTouch) << SpecOf(static_cast<ButtonKind>(i)).element;
        }
    }

    TEST_F(MahjongTableScreenTest, InThePortraitViewTheButtonsAre68HighInOneRowInTheBandUnderTheWaitingRow) {
        // The portrait view is 720 wide: a button of kPortraitActionButtonHeight is a touch target from a window 339 px wide
        constexpr float kPortraitSmallestWindowWidth = 339.0f;
        constexpr float kBandTop = 924.0f;      // under the waiting row of `Hints` (y 836..916)
        constexpr float kBandBottom = 1028.0f;  // the plate of the viewer
        constexpr float kPortraitButtonWidth = 92.0f;
        constexpr float kPortraitDeclareWidth = 124.0f;
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const float minTouch = EngineTouchMinimum();
        ASSERT_FALSE(NeedsCompactActions(kPortraitSmallestWindowWidth, 2000.0f, 1.0f, minTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        ASSERT_TRUE(NeedsCompactActions(kPortraitSmallestWindowWidth - 1.0f, 2000.0f, 1.0f, minTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));

        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        const MatrixWindow window = DesignWindowOf(TableShape::Portrait);
        const TableLayout layout = MakeWorstCaseTableLayout(0, window);
        for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            LayOutInWindow(*canvas, window);
            const UIElement* row = Element("Actions", "ActionRow");
            ASSERT_NE(row, nullptr);
            EXPECT_GE(row->rect.x, 0.0f);
            EXPECT_LE(row->rect.x + row->rect.w, kPortraitViewWidth);
            EXPECT_GE(row->rect.y, kBandTop);
            EXPECT_LE(row->rect.y + row->rect.h, kBandBottom - kGridUnit);
            float width = 0.0f;
            for (const ButtonKind kind : kinds) {
                const UIElement* button = Element("Actions", SpecOf(kind).element);
                ASSERT_NE(button, nullptr);
                ASSERT_FALSE(button->rect.IsEmpty()) << SpecOf(kind).element;
                EXPECT_FLOAT_EQ(button->rect.h, kPortraitActionButtonHeight) << SpecOf(kind).element << ": the scene file and TouchRegime.h agree";
                EXPECT_FLOAT_EQ(button->rect.y, row->rect.y) << SpecOf(kind).element << ": one row";
                EXPECT_FLOAT_EQ(button->rect.w, kind == ButtonKind::Declare ? kPortraitDeclareWidth : kPortraitButtonWidth) << SpecOf(kind).element;
                EXPECT_GE(button->rect.x, row->rect.x);
                EXPECT_LE(button->rect.x + button->rect.w, row->rect.x + row->rect.w);
                ExpectClearOfTable(button->rect, layout, SpecOf(kind).element);
                width += button->rect.w;
            }
            EXPECT_LE(width + kButtonGap * static_cast<float>(kinds.size() - 1), row->rect.w) << "the set fits the row without wrapping";
        }
        EXPECT_GE(kPortraitActionButtonHeight * kPortraitSmallestWindowWidth / kPortraitViewWidth, minTouch) << "a touch target in the smallest window that keeps `Actions`";
    }

    TEST_F(MahjongTableScreenTest, InTheNarrowViewTheButtonsAreInTwoLinesRightOfTheBottomDiscardsAndTheHintLiesLeftOfThem) {
        constexpr float kNarrowButtonWidth = 88.0f;
        constexpr float kNarrowUpgradeWidth = 104.0f;  // "Add to pung" does not fit 88
        constexpr float kNarrowRowWidth = 4.0f * kNarrowButtonWidth + 3.0f * kButtonGap;
        constexpr float kNarrowRowLines = 2.0f;
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        ASSERT_TRUE(canvas->SetElementVisible("Hint"_intern, true));

        const MatrixWindow design = DesignWindowOf(TableShape::Narrow);
        const TableLayout layout = MakeWorstCaseTableLayout(0, design);
        ASSERT_EQ(layout.shape, TableShape::Narrow);
        const Rect& block = layout.zones.discards[0];  // the viewer's discards, the row's neighbour on the left
        for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            LayOutInWindow(*canvas, design);
            const UIElement* row = Element("Actions", "ActionRow");
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->rect.w, kNarrowRowWidth) << "four buttons and three gaps make a line";
            EXPECT_GE(row->rect.x, block.x + block.w + kPlatePadding) << "right of the bottom discards";
            EXPECT_LE(row->rect.x + row->rect.w, layout.viewWidth);
            EXPECT_GE(row->rect.y, layout.zones.wall.y + layout.zones.wall.h);
            // The row ends above the viewer's bonus tiles and melds (their top is the least y of the viewer's meld and bonus slots)
            float rowOfTilesTop = layout.viewHeight;
            for (const TileSlot& slot : layout.slots) {
                if (slot.seat == 0 && (slot.role == SlotRole::MeldTile || slot.role == SlotRole::BonusTile)) {
                    rowOfTilesTop = std::min(rowOfTilesTop, slot.rect.y);
                }
            }
            EXPECT_LE(row->rect.y + row->rect.h, rowOfTilesTop - kGridUnit);
            EXPECT_EQ(row->rect.h, kNarrowRowLines * kActionButtonHeight + (kNarrowRowLines - 1.0f) * kButtonGap) << "two lines of buttons";
            for (const ButtonKind kind : kinds) {
                const UIElement* button = Element("Actions", SpecOf(kind).element);
                ASSERT_NE(button, nullptr);
                ASSERT_FALSE(button->rect.IsEmpty()) << SpecOf(kind).element;
                EXPECT_FLOAT_EQ(button->rect.w, kind == ButtonKind::UpgradeKong ? kNarrowUpgradeWidth : kNarrowButtonWidth) << SpecOf(kind).element;
                EXPECT_FLOAT_EQ(button->rect.h, kActionButtonHeight) << SpecOf(kind).element << ": the scene file and TouchRegime.h agree";
                EXPECT_GE(button->rect.x, row->rect.x) << SpecOf(kind).element;
                EXPECT_LE(button->rect.x + button->rect.w, row->rect.x + row->rect.w) << SpecOf(kind).element;
                EXPECT_LE(button->rect.y + button->rect.h, row->rect.y + row->rect.h) << SpecOf(kind).element << " lies inside the two lines of the row";
            }
        }

        // The longest sentence in the hint, the fullest turn and window shown: every word of the canvas fits in every narrow window of every language
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::vector<eastl::string> sentences;
            for (int i = 0; i < kButtonCount; ++i) {
                sentences.push_back(HintOf(CoreManager::GetUIStrings(), static_cast<ButtonKind>(i)));
            }
            CoreManager::GetUIVars().Set("actionHint"_intern, eastl::string_view(Longest(sentences)));
            for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
                for (int i = 0; i < kButtonCount; ++i) {
                    SetShown(static_cast<ButtonKind>(i), false);
                }
                for (const ButtonKind kind : kinds) {
                    SetShown(kind, true);
                }
                const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*canvas, 0.0f, TableShape::Narrow);
                for (const std::string& line : cut) {
                    ADD_FAILURE() << line;
                }
            }
        });

        // In every narrow window (the touch size is left out where the buttons are small: `ActionsMayBeSmall`)
        ExpectMatrixExceptTouchWhere(*canvas, "Actions", EngineTouchMinimum(), ActionsMayBeSmall);
        for (const MatrixWindow& window : NarrowWindows()) {
            ASSERT_EQ(ShapeOf(window), TableShape::Narrow) << window.label.c_str();
            const TableLayout worst = MakeWorstCaseTableLayout(0, window);
            for (const ButtonKind kind : FullestButtonSets()[1]) {
                SetShown(kind, true);
            }
            LayOutOnTheTable({"Actions"}, window);
            for (const char* name : {"ActionRow", "Hint"}) {
                ExpectClearOfTable(Element("Actions", name)->rect, worst, window.label.c_str());
            }
            EXPECT_TRUE(Apart(Element("Actions", "Hint")->rect, Element("Actions", "ActionRow")->rect)) << window.label.c_str();
        }
    }

    // ----- the compact canvas -----

    TEST_F(MahjongTableScreenTest, TheCompactCanvasHoldsTheSameButtonsAsTheActionsCanvasWithTheSameActionsValuesWordsAndPictures) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const auto actions = Canvas("Actions");
        const auto compact = Canvas("ActionsCompact");
        ASSERT_TRUE(actions && compact);
        compact->UpdateLayout(kViewWidth, kViewHeight);  // the reflected fields of the component reach the layout on its first update
        EXPECT_EQ(compact->GetLayout().params.space, CanvasSpace::Screen);
        EXPECT_EQ(compact->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);
        // The scale settings that `CompactScale` (and so the rule) assumes are the ones of the file
        EXPECT_FLOAT_EQ(compact->GetLayout().params.referenceWidth, kCompactReferenceWidth);
        EXPECT_FLOAT_EQ(compact->GetLayout().params.referenceHeight, kCompactReferenceHeight);
        EXPECT_FLOAT_EQ(compact->GetLayout().params.match, 0.5f);
        EXPECT_FLOAT_EQ(compact->GetLayout().params.minScale, kCompactMinScale);
        // The buttons are the width and the row the spacing the variants below are counted with
        const UIElement* row = Element("ActionsCompact", "ActionRow");
        ASSERT_NE(row, nullptr);
        EXPECT_FLOAT_EQ(row->style.stack.spacing, kCompactSpacing);
        EXPECT_FLOAT_EQ(row->style.rect.height.value, kActionButtonHeight);
        for (const char* name : {"Win", "Pung", "Chow2", "Pass", "HintButton"}) {
            EXPECT_FLOAT_EQ(Element("ActionsCompact", name)->style.rect.width.value, kCompactButtonWidth) << name;
        }
        EXPECT_FLOAT_EQ(Element("ActionsCompact", "Declare")->style.rect.width.value, kCompactDeclareWidth);
        // The rule counts the buttons of the fullest window, and the hint button that the hints offer with it: the biggest set the canvases
        // have to hold at once
        size_t fullest = 0;
        for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
            fullest = std::max(fullest, kinds.size());
        }
        EXPECT_EQ(static_cast<size_t>(kCompactButtonCount), fullest + 1u);
        // The row ends with the hint button and then the wide `Declare` switch, so that the switch ends a line (the turn never needs more lines
        // than the claim window)
        ASSERT_GE(row->children.size(), 2u);
        EXPECT_EQ(row->children[row->children.size() - 2]->name, PoolString::Intern("HintButton"));
        EXPECT_EQ(row->children.back()->name, PoolString::Intern("Declare"));

        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            const UIElement* wide = Element("Actions", spec.element);
            const UIElement* narrow = Element("ActionsCompact", spec.element);
            ASSERT_TRUE(wide != nullptr && narrow != nullptr) << spec.element;
            EXPECT_TRUE(SameColor(narrow->color, wide->color)) << spec.element;
            if (kind == ButtonKind::Declare) {
                const auto* a = wide->Cast<UIToggle>();
                const auto* b = narrow->Cast<UIToggle>();
                ASSERT_TRUE(a != nullptr && b != nullptr);
                EXPECT_EQ(b->action, a->action);
                EXPECT_EQ(b->onValue, a->onValue);
                EXPECT_EQ(b->offValue, a->offValue);
                EXPECT_EQ(b->bind, a->bind);
                EXPECT_EQ(b->text, a->text);
                continue;
            }
            const auto* a = wide->Cast<UIButton>();
            const auto* b = narrow->Cast<UIButton>();
            ASSERT_TRUE(a != nullptr && b != nullptr) << spec.element;
            EXPECT_EQ(b->action, a->action) << spec.element;
            EXPECT_EQ(b->value, a->value) << spec.element;
            EXPECT_EQ(b->action, PoolString::Intern(spec.action)) << spec.element;
            EXPECT_EQ(b->value, PoolString::Intern(spec.value)) << spec.element;
            EXPECT_EQ(b->text, eastl::string("{#") + spec.wordKey + "}") << spec.element << " says a word of the string table";
            EXPECT_EQ(b->children.size(), a->children.size()) << spec.element;
            for (int t = 0; t < kMaxButtonTiles; ++t) {
                const std::string name = TileImageName(kind, t).c_str();
                EXPECT_EQ(Element("ActionsCompact", name.c_str()) != nullptr, t < spec.maxTiles) << name;
            }
        }
        // And nothing else: the same names, except the hint label that only the canvas with a pointer has and the hint button of the hints,
        // which is in the node `HintsButton` on the table and in the row here
        std::set<std::string> wideNames = ElementNamesOf("Actions");
        const std::set<std::string> narrowNames = ElementNamesOf("ActionsCompact");
        EXPECT_EQ(wideNames.erase("Hint"), 1u);
        wideNames.insert("HintButton");
        EXPECT_EQ(wideNames, narrowNames);
        // The hint button of the row is the one of the table: the action, the word and the colour; it has no pictures
        const UIElement* tableHint = Element("HintsButton", "HintButton");
        const UIElement* rowHint = Element("ActionsCompact", "HintButton");
        ASSERT_TRUE(tableHint != nullptr && rowHint != nullptr);
        const auto* tableHintButton = tableHint->Cast<UIButton>();
        const auto* rowHintButton = rowHint->Cast<UIButton>();
        ASSERT_TRUE(tableHintButton != nullptr && rowHintButton != nullptr);
        EXPECT_EQ(rowHintButton->action, PoolString::Intern("MahjongHintAdvice"));
        EXPECT_EQ(rowHintButton->action, tableHintButton->action);
        EXPECT_EQ(rowHintButton->text, tableHintButton->text);
        EXPECT_EQ(rowHintButton->text, "{#hints.adviseButton}");
        EXPECT_TRUE(SameColor(rowHint->color, PaletteColor("neutral")));
        EXPECT_TRUE(SameColor(rowHint->color, tableHint->color));
        EXPECT_TRUE(rowHint->children.empty());
        EXPECT_EQ(Element("ActionsCompact", "Hint"), nullptr) << "a touch screen has no hover";
    }

    TEST_F(MahjongTableScreenTest, TheHeightVariantsOfTheCompactRowAreTheLinesTheWidthNeedsRoundedUp) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const auto compact = Canvas("ActionsCompact");
        ASSERT_TRUE(compact);
        compact->UpdateLayout(kViewWidth, kViewHeight);
        const eastl::vector<UIVariant>& variants = compact->GetLayout().variants;

        // k buttons to a line need k * width + (k - 1) * spacing of the safe area, which is at least 82 % of the canvas. A canvas up to the width
        // where k buttons stop fitting holds k - 1 to a line, so the claim window's buttons make ceil(count / (k - 1)) lines, and a stack has
        // the height it is given. The variants run from the widest threshold to the narrowest (a later one wins); the file's own height is
        // the one line of a canvas wide enough for all of them. The table of `TouchRegime.h` (what the rule measures) is held to the file
        // and to this arithmetic, so the rule and the scene cannot drift.
        constexpr int kStopsFitting[] = {kCompactButtonCount, 4, 3, 2};
        ASSERT_EQ(variants.size(), std::size(kStopsFitting));
        ASSERT_EQ(std::size(kCompactRowVariants), std::size(kStopsFitting));
        EXPECT_FLOAT_EQ(Element("ActionsCompact", "ActionRow")->style.rect.height.value, kActionButtonHeight) << "one line in the file";
        EXPECT_FLOAT_EQ(CompactRowHeight(kCompactRowVariants[0].maxWidth + 1.0f), kActionButtonHeight);
        for (size_t i = 0; i < std::size(kStopsFitting); ++i) {
            const int fits = kStopsFitting[i];
            const int lines = (kCompactButtonCount + fits - 2) / (fits - 1);  // ceil(count / (fits - 1))
            const float neededSafe = static_cast<float>(fits) * kCompactButtonWidth + static_cast<float>(fits - 1) * kCompactSpacing;
            const float threshold = std::ceil(neededSafe / kCompactSafeShare * 100.0f) / 100.0f;
            EXPECT_NEAR(variants[i].when.maxWidth, threshold, 0.006f) << i << ": " << neededSafe << " / " << kCompactSafeShare;
            EXPECT_GE(variants[i].when.maxWidth, neededSafe / kCompactSafeShare) << i << ": rounded up, never down";
            ASSERT_EQ(variants[i].overrides.size(), 1u);
            EXPECT_EQ(variants[i].overrides[0].element, PoolString::Intern("ActionRow"));
            ASSERT_TRUE(variants[i].overrides[0].height.has_value());
            const float height = static_cast<float>(lines) * kActionButtonHeight + static_cast<float>(lines - 1) * kCompactSpacing;
            EXPECT_FLOAT_EQ(variants[i].overrides[0].height->value, height) << i;
            // The constants of the rule are the variants of the file
            EXPECT_FLOAT_EQ(kCompactRowVariants[i].maxWidth, variants[i].when.maxWidth) << i;
            EXPECT_FLOAT_EQ(kCompactRowVariants[i].height, variants[i].overrides[0].height->value) << i;
        }
    }

    TEST_F(MahjongTableScreenTest, TheCompactButtonsPassTheSizeMatrixInEveryLanguageWithTheEnginesTouchMinimumAndTheirWordsFit) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("ActionsCompact");
        ASSERT_TRUE(canvas);
        // Hidden in the file; shown by hand and not drawn after it (a draw would hide it again: the window is big)
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "ActionsCompact"_intern, true));

        const float minTouch = EngineTouchMinimum();
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
                for (int i = 0; i < kButtonCount; ++i) {
                    SetShown(static_cast<ButtonKind>(i), false, "ActionsCompact");
                }
                for (const ButtonKind kind : kinds) {
                    SetShown(kind, true, "ActionsCompact");
                }
                ASSERT_TRUE(canvas->SetElementVisible("HintButton"_intern, true)) << "the hint button ends every set";
                ExpectPassesTheMatrix("ActionsCompact", minTouch);
                for (const Output& output : kCompactOutputs) {
                    ExpectWordsFit("ActionsCompact", output.width, output.height, 0.0f);
                }
            }
        });
    }

    TEST_F(MahjongTableScreenTest, TheHintTakesNoInputLiesInTheViewClearOfTheButtonsAndHoldsTheLongestSentenceOfEveryLanguageInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        const UIElement* hint = Element("Actions", "Hint");
        ASSERT_NE(hint, nullptr);
        EXPECT_FALSE(OnScreen("Actions", "Hint")) << "hidden in the file";
        EXPECT_FALSE(hint->IsHitTestable()) << "a tooltip never takes a click";
        EXPECT_TRUE(hint->ignoreSafeArea);
        ASSERT_TRUE(canvas->SetElementVisible("Hint"_intern, true));

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::vector<eastl::string> sentences;
            for (int i = 0; i < kButtonCount; ++i) {
                sentences.push_back(HintOf(CoreManager::GetUIStrings(), static_cast<ButtonKind>(i)));
            }
            const eastl::string longest = Longest(sentences);
            EXPECT_LE(Utf8::CountCodePoints(longest), kMaxHintLength);
            CoreManager::GetUIVars().Set("actionHint"_intern, eastl::string_view(longest));
            // With the fullest turn and the fullest window under it, as the matrix test of the buttons has them
            for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
                for (int i = 0; i < kButtonCount; ++i) {
                    SetShown(static_cast<ButtonKind>(i), false);
                }
                for (const ButtonKind kind : kinds) {
                    SetShown(kind, true);
                }
                // The touch size is held where the rule keeps `Actions` (but for the windows of 640 x 480 and 720 x 480, see `ActionsMayBeSmall`); elsewhere `ActionsCompact` holds it (below)
                ExpectMatrixExceptTouchWhere(*canvas, "Actions", EngineTouchMinimum(), ActionsMayBeSmall);
                for (const Output& output : kOutputs) {
                    ExpectWordsFit("Actions", output.width, output.height, 0.0f);
                }
            }
        });

        // In the view of each shape the hint lies inside it, apart from the row it explains, and over no tile or plate of the table: a tooltip in the
        // gutter of the landscape table, in the free ground right of the viewer's discards in the portrait one, one grid unit clear of the plates
        // in every window of the table, the phone layouts included (`ActionsCompact` holds the buttons where `Actions` is hidden, and its `Hint` with them)
        ForEachTableWindow([&](const MatrixWindow& window) {
            if (IsCompactWindow(window)) {
                return;
            }
            LayOutOnTheTable({"Actions"}, window);
            const Rect plate = hint->rect;
            EXPECT_GE(plate.x, 0.0f) << window.label.c_str();
            EXPECT_GE(plate.y, 0.0f) << window.label.c_str();
            EXPECT_LE(plate.x + plate.w, ViewOf(window).width) << window.label.c_str();
            EXPECT_LE(plate.y + plate.h, ViewOf(window).height) << window.label.c_str();
            EXPECT_TRUE(Apart(plate, Element("Actions", "ActionRow")->rect)) << window.label.c_str() << ": the hint does not lie over the buttons it explains";
            const TableLayout layout = MakeWorstCaseTableLayout(0, window);
            for (const TileSlot& slot : layout.slots) {
                EXPECT_TRUE(Apart(slot.rect, plate)) << window.label.c_str() << ": the hint covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
            }
            const eastl::vector<Rect> plates = ZonePlates(layout);
            for (size_t i = 0; i < plates.size(); ++i) {
                EXPECT_TRUE(Apart(plates[i], Grown(plate, kGridUnit))) << window.label.c_str() << ": the hint is less than one grid unit from plate " << i;
            }
            for (size_t i = 0; i < layout.zones.seatPlates.size(); ++i) {
                EXPECT_TRUE(Apart(layout.zones.seatPlates[i], Grown(plate, kGridUnit))) << window.label.c_str() << ": the hint is less than one grid unit from the plate of seat " << i;
            }
        });
    }

    TEST_F(MahjongTableScreenTest, TheCoverPassesTheSizeMatrixInEveryLanguageWithTheLongestSeatName) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Handoff"_intern, true);

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            // The longest name any seat can have, from the real function: every seat with every wind
            std::vector<eastl::string> names;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (int wind = 0; wind < kSeatCount; ++wind) {
                    names.push_back(SeatName(CoreManager::GetUIStrings(), seat, static_cast<Wind>(wind)));
                }
            }
            CoreManager::GetUIVars().Set("handoffSeat"_intern, eastl::string_view(Longest(names)));

            ExpectPassesTheMatrix("Handoff", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Handoff", output.width, output.height, 0.0f);
            }
        });
    }

    TEST_F(MahjongTableScreenTest, TheCoverFollowsTheModalAnatomy) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Handoff"_intern, true);
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::vector<eastl::string> names;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (int wind = 0; wind < kSeatCount; ++wind) {
                    names.push_back(SeatName(CoreManager::GetUIStrings(), seat, static_cast<Wind>(wind)));
                }
            }
            CoreManager::GetUIVars().Set("handoffSeat"_intern, eastl::string_view(Longest(names)));
            Draw();
            Canvas("Handoff")->UpdateLayout(kViewWidth, kViewHeight);

            const UIElement* plate = Element("Handoff", "Plate");
            const UIElement* text = Element("Handoff", "HandoffText");
            const UIElement* reveal = Element("Handoff", "Reveal");
            ASSERT_TRUE(plate != nullptr && text != nullptr && reveal != nullptr);
            EXPECT_TRUE(SameColor(plate->color, PaletteColor("surface")));
            EXPECT_TRUE(SameColor(text->color, PaletteColor("header")));
            EXPECT_TRUE(SameColor(reveal->color, PaletteColor("primary")));
            EXPECT_EQ(reveal->rect.h, kMainButtonHeight);
            EXPECT_EQ(text->rect.y, plate->rect.y + kModalPadding);
            EXPECT_EQ(reveal->rect.y - (text->rect.y + text->rect.h), kModalSectionGap);
            EXPECT_EQ(reveal->rect.y + reveal->rect.h, plate->rect.y + plate->rect.h - kModalPadding) << "the plate holds the band and the button and nothing more";
        });
    }

    TEST_F(MahjongTableScreenTest, TheButtonsLieInTheFreeCornerOfTheTableAndCoverNoTile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        // In every window where `Actions` is shown (the desktop table, the phone ones and the wide ones, whose view puts the row a wing further
        // right): the row, and the buttons of the fullest turn and of the fullest claim window, lie inside it and so clear of every slot
        size_t windows = 0;
        ForEachTableWindow([&](const MatrixWindow& window) {
            if (IsCompactWindow(window)) {
                return;
            }
            const TableLayout layout = MakeWorstCaseTableLayout(0, window);
            for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
                for (int i = 0; i < kButtonCount; ++i) {
                    SetShown(static_cast<ButtonKind>(i), false);
                }
                for (const ButtonKind kind : kinds) {
                    SetShown(kind, true);
                }
                LayOutOnTheTable({"Actions"}, window);
                const UIElement* row = Element("Actions", "ActionRow");
                ASSERT_NE(row, nullptr);
                const std::string label = window.label.c_str();
                for (const TileSlot& slot : layout.slots) {
                    EXPECT_TRUE(Apart(slot.rect, row->rect)) << label << ": role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                }
                for (const ButtonKind kind : kinds) {
                    const UIElement* button = Element("Actions", SpecOf(kind).element);
                    ASSERT_NE(button, nullptr) << SpecOf(kind).element;
                    const Rect& r = button->rect;
                    EXPECT_GE(r.x, row->rect.x) << label << ": " << SpecOf(kind).element;
                    EXPECT_GE(r.y, row->rect.y) << label << ": " << SpecOf(kind).element;
                    EXPECT_LE(r.x + r.w, row->rect.x + row->rect.w) << label << ": " << SpecOf(kind).element;
                    EXPECT_LE(r.y + r.h, row->rect.y + row->rect.h) << label << ": " << SpecOf(kind).element << " lies below the row";
                    for (const TileSlot& slot : layout.slots) {
                        EXPECT_TRUE(Apart(slot.rect, r)) << label << ": " << SpecOf(kind).element << " covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                    }
                }
            }
            ++windows;
        });
        EXPECT_GT(windows, 0u);
    }

}  // namespace BECore::Mahjong::Tests
