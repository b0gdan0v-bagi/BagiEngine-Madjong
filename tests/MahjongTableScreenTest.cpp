#include "MahjongTouchTestSupport.h"

#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIToggle.h>

/**
 * The canvases of the shipped scene that belong to the table (`Actions`, `Handoff`): the buttons of the component are the buttons of the
 * file with their action and value, the longest labels fit, both pass the size matrix, and the buttons lie where no tile of any game can.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kLongestSeat = "Seat 4 (North)";
        /// The height of an action button of `Actions` in view units (the one place is `kActionButtonHeight`): 32 px, the engine's touch
        /// minimum, from a window of 732 x 412 px; below that the buttons are in `ActionsCompact`.
        constexpr float kButtonHeight = kActionButtonHeight;
        /// The smallest window that keeps `Actions`: one pixel above the boundary of `NeedsCompactActions` (731.4 x 411.4), whose
        /// aspect is 16:9.
        constexpr float kSmallestActionsWindowWidth = 732.0f;
        constexpr float kSmallestActionsWindowHeight = 412.0f;
        /// The width of a button of `ActionsCompact` in view units, the spacing of its row and the buttons of the fullest claim window; the
        /// variants of the file are counted from them (the comment above the node in the file has the working).
        constexpr float kCompactButtonWidth = 116.0f;
        constexpr float kCompactSpacing = 6.0f;
        constexpr int kCompactClaimButtons = 7;
        /// The part of the canvas that the safe area is at the least (side insets of the matrix), which a variant cannot see.
        constexpr float kLeastSafeShare = 0.82f;
        /// The height of the line of the word at the top of a button; the pictures of tiles lie below it.
        constexpr float kWordLineHeight = 24.0f;
        /// The width of an action button in view units.
        constexpr float kButtonWidth = 132.0f;
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

        bool SameColor(const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
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
                EXPECT_EQ(toggle->text, eastl::string(spec.word)) << "the toggle says its word";
                continue;
            }
            const auto* button = element->Cast<UIButton>();
            ASSERT_NE(button, nullptr) << spec.element;
            EXPECT_EQ(button->action, PoolString::Intern(spec.action)) << spec.element;
            EXPECT_EQ(button->value, PoolString::Intern(spec.value)) << spec.element;
            EXPECT_EQ(button->text, eastl::string(spec.word)) << "the button says its word, fixed text";
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
                // Only the hint follows a variable; the words of the buttons are fixed text
                EXPECT_EQ(std::string(label->text.c_str()).find('{') == std::string::npos, name != "Hint") << name << " and its variable";
            }
            for (const auto& child : element->children) {
                pending.push_back(child.Get());
            }
        }
    }

    TEST_F(MahjongTableScreenTest, TheActionButtonsPassTheSizeMatrixWithTheirWordsAndPicturesAndEveryButtonIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);

        // The fullest turn (a win, a declaration, three kongs and the upgrade) and the fullest window (all claims, three chows, pass), every
        // button with all its pictures. The words are fixed text in the file; the longest are `Add to pung`, `Mahjong!` and `Ready hand`.
        for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            // The touch size is held where the rule keeps `Actions`; elsewhere `ActionsCompact` holds it (below)
            ExpectMatrixExceptTouchWhere(*canvas, "Actions", CoreManager::GetUIEnvironment().GetSettings().minTouchSize, IsCompactWindow);
        }
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
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
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
        for (const char* name : {"Win", "Pung", "Chow2", "Pass"}) {
            EXPECT_FLOAT_EQ(Element("ActionsCompact", name)->style.rect.width.value, kCompactButtonWidth) << name;
        }

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
            EXPECT_EQ(b->text, eastl::string(spec.word)) << spec.element;
            EXPECT_EQ(b->children.size(), a->children.size()) << spec.element;
            for (int t = 0; t < kMaxButtonTiles; ++t) {
                const std::string name = TileImageName(kind, t).c_str();
                EXPECT_EQ(Element("ActionsCompact", name.c_str()) != nullptr, t < spec.maxTiles) << name;
            }
        }
        // And nothing else: the same names, except the hint label that only the canvas with a pointer has
        std::set<std::string> wideNames = ElementNamesOf("Actions");
        const std::set<std::string> narrowNames = ElementNamesOf("ActionsCompact");
        EXPECT_EQ(wideNames.erase("Hint"), 1u);
        EXPECT_EQ(wideNames, narrowNames);
        EXPECT_EQ(Element("ActionsCompact", "Hint"), nullptr) << "a touch screen has no hover";
    }

    TEST_F(MahjongTableScreenTest, TheHeightVariantsOfTheCompactRowAreTheLinesTheWidthNeedsRoundedUp) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const auto compact = Canvas("ActionsCompact");
        ASSERT_TRUE(compact);
        compact->UpdateLayout(kViewWidth, kViewHeight);
        const eastl::vector<UIVariant>& variants = compact->GetLayout().variants;

        // k buttons to a line need k * width + (k - 1) * spacing of the safe area, which is at least 82 % of the canvas. A canvas up to the width
        // where k buttons stop fitting holds k - 1 to a line, so the claim window's seven buttons make ceil(7 / (k - 1)) lines, and a stack has
        // the height it is given. The variants run from the widest threshold to the narrowest (a later one wins); the file's own height is
        // the one line of a canvas wide enough for all seven.
        constexpr int kStopsFitting[] = {7, 4, 3, 2};
        ASSERT_EQ(variants.size(), std::size(kStopsFitting));
        EXPECT_FLOAT_EQ(Element("ActionsCompact", "ActionRow")->style.rect.height.value, kActionButtonHeight) << "one line in the file";
        for (size_t i = 0; i < std::size(kStopsFitting); ++i) {
            const int fits = kStopsFitting[i];
            const int lines = (kCompactClaimButtons + fits - 2) / (fits - 1);  // ceil(7 / (fits - 1))
            const float neededSafe = static_cast<float>(fits) * kCompactButtonWidth + static_cast<float>(fits - 1) * kCompactSpacing;
            const float threshold = std::ceil(neededSafe / kLeastSafeShare * 100.0f) / 100.0f;
            EXPECT_NEAR(variants[i].when.maxWidth, threshold, 0.006f) << i << ": " << neededSafe << " / " << kLeastSafeShare;
            EXPECT_GE(variants[i].when.maxWidth, neededSafe / kLeastSafeShare) << i << ": rounded up, never down";
            ASSERT_EQ(variants[i].overrides.size(), 1u);
            EXPECT_EQ(variants[i].overrides[0].element, PoolString::Intern("ActionRow"));
            ASSERT_TRUE(variants[i].overrides[0].height.has_value());
            const float height = static_cast<float>(lines) * kActionButtonHeight + static_cast<float>(lines - 1) * kCompactSpacing;
            EXPECT_FLOAT_EQ(variants[i].overrides[0].height->value, height) << i;
        }
    }

    TEST_F(MahjongTableScreenTest, TheCompactButtonsPassTheSizeMatrixWithTheEnginesTouchMinimumAndTheirWordsFit) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("ActionsCompact");
        ASSERT_TRUE(canvas);
        // Hidden in the file; shown by hand and not drawn after it (a draw would hide it again: the window is big)
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "ActionsCompact"_intern, true));

        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false, "ActionsCompact");
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true, "ActionsCompact");
            }
            ExpectPassesTheMatrix("ActionsCompact", minTouch);
            for (const Output& output : kCompactOutputs) {
                ExpectWordsFit("ActionsCompact", output.width, output.height, 0.0f);
            }
        }
    }

    TEST_F(MahjongTableScreenTest, TheHintTakesNoInputLiesInTheViewClearOfTheButtonsAndHoldsTheLongestSentenceInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        const UIElement* hint = Element("Actions", "Hint");
        ASSERT_NE(hint, nullptr);
        EXPECT_FALSE(OnScreen("Actions", "Hint")) << "hidden in the file";
        EXPECT_FALSE(hint->IsHitTestable()) << "a tooltip never takes a click";
        EXPECT_TRUE(hint->ignoreSafeArea);

        eastl::string_view longest;
        for (int i = 0; i < kButtonCount; ++i) {
            const eastl::string_view sentence = HintOf(static_cast<ButtonKind>(i));
            if (sentence.size() > longest.size()) {
                longest = sentence;
            }
        }
        CoreManager::GetUIVars().Set("actionHint"_intern, longest);
        ASSERT_TRUE(canvas->SetElementVisible("Hint"_intern, true));
        // With the fullest turn and the fullest window under it, as the matrix test of the buttons has them
        for (const std::vector<ButtonKind>& kinds : FullestButtonSets()) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            ExpectMatrixExceptTouchWhere(*canvas, "Actions", CoreManager::GetUIEnvironment().GetSettings().minTouchSize, IsCompactWindow);
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Actions", output.width, output.height, 0.0f);
            }
        }

        canvas->UpdateLayout(kViewWidth, kViewHeight);
        const Rect& plate = hint->rect;
        EXPECT_GE(plate.x, 0.0f);
        EXPECT_GE(plate.y, 0.0f);
        EXPECT_LE(plate.x + plate.w, kViewWidth);
        EXPECT_LE(plate.y + plate.h, kViewHeight);
        const Rect& row = Element("Actions", "ActionRow")->rect;
        const bool apart = plate.x + plate.w <= row.x || plate.x >= row.x + row.w || plate.y + plate.h <= row.y || plate.y >= row.y + row.h;
        EXPECT_TRUE(apart) << "the hint does not lie over the buttons it explains";
    }

    TEST_F(MahjongTableScreenTest, TheCoverPassesTheSizeMatrixWithTheLongestSeatName) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        CoreManager::GetUIVars().Set("handoffSeat"_intern, eastl::string_view(kLongestSeat));
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Handoff"_intern, true);

        ExpectPassesTheMatrix("Handoff", CoreManager::GetUIEnvironment().GetSettings().minTouchSize);
    }

    TEST_F(MahjongTableScreenTest, TheButtonsLieInTheFreeCornerOfTheTableAndCoverNoTile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        // The worst-case layout: every zone at its capacity, so no tile of any game can lie where a button does
        const TableLayout layout = MakeWorstCaseTableLayout();
        const UIElement* row = Element("Actions", "ActionRow");
        ASSERT_NE(row, nullptr);

        for (const TileSlot& slot : layout.slots) {
            const bool apart =
                slot.rect.x + slot.rect.w <= row->rect.x || slot.rect.x >= row->rect.x + row->rect.w || slot.rect.y + slot.rect.h <= row->rect.y || slot.rect.y >= row->rect.y + row->rect.h;
            EXPECT_TRUE(apart) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
        }
    }

}  // namespace BECore::Mahjong::Tests
