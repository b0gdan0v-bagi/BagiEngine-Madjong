#include "MahjongTableTestSupport.h"

#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIToggle.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The canvases of the shipped scene that belong to the table (`Actions`, `Handoff`): the buttons of the component are the buttons of the
 * file with their action and value, the longest labels fit, both pass the size matrix, and the buttons lie where no tile of any game can.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kLongestSeat = "Seat 4 (North)";
        /// The height of an action button in view units: 32 px (the engine's touch minimum) from a window of 731 x 411 px.
        constexpr float kButtonHeight = 56.0f;
        /// The height in pixels of the smallest window the touch minimum is held from; the view is 1280 x 720 units, scaled to fit it.
        constexpr float kSmallestTouchWindowHeight = 411.0f;
        /// The table is a fixed design scaled to the window, so on the smallest windows of the matrix (480 px) a button is 21 px high:
        /// the matrix runs the buttons with this minimum, and the engine's own is held from the 731 px windows (the test of the height).
        constexpr float kSmallestWindowTouchMinimum = 20.0f;
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
        constexpr float kTileShape = 3.0f / 4.0f;
        constexpr float kShapeTolerance = 0.02f;
        /// A pile longer than any hand leaves (136 tiles, 52 in the hands at the least).
        constexpr int kMostDiscards = 100;
        /// The gap between the buttons of a row, and the width of a row of three buttons of 132 with two gaps (the row wraps at it).
        constexpr float kButtonGap = 8.0f;
        constexpr float kRowWidth = 3.0f * kButtonWidth + 2.0f * kButtonGap;

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

        bool SameColor(const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        const Color& PaletteColor(const char* role) {
            for (int i = 0; i < kUiPaletteSize; ++i) {
                if (eastl::string_view(kUiPaletteNames[i]) == role) {
                    return kUiPalette[i];
                }
            }
            ADD_FAILURE() << "no role " << role;
            return kUiPalette[0];
        }

    }  // namespace

    class MahjongTableScreenTest : public MahjongTableTestBase {
    protected:
        /// Shows or hides a button of the canvas `Actions` with all the pictures of its tiles.
        void SetShown(ButtonKind kind, bool shown) {
            const ButtonSpec& spec = SpecOf(kind);
            auto canvas = Canvas("Actions");
            ASSERT_TRUE(canvas);
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
        EXPECT_EQ(row->rect.w, kRowWidth) << "three buttons and two gaps fit a row";
        const UIElement* hint = Element("Actions", "Hint");
        ASSERT_NE(hint, nullptr);
        EXPECT_EQ(hint->rect.w, kRowWidth) << "the hint is as wide as the row it explains";
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
        for (const std::vector<ButtonKind>& kinds :
             {std::vector<ButtonKind>{ButtonKind::Win, ButtonKind::Declare, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2, ButtonKind::UpgradeKong},
              std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::ClaimKong, ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2,
                                      ButtonKind::Pass}}) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            ExpectPassesTheMatrix("Actions", kSmallestWindowTouchMinimum);
        }
    }

    TEST_F(MahjongTableScreenTest, EveryTilePictureLiesInsideItsButtonBelowTheWordAndKeepsTheShapeOfATile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        for (int i = 0; i < kButtonCount; ++i) {
            SetShown(static_cast<ButtonKind>(i), true);
        }
        Canvas("Actions")->UpdateLayout(kViewWidth, kViewHeight);

        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            const UIElement* button = Element("Actions", spec.element);
            ASSERT_NE(button, nullptr) << spec.element;
            const Rect& outer = button->rect;
            EXPECT_GE(outer.w, kButtonWidth) << spec.element;
            EXPECT_GE(outer.h, kButtonHeight) << spec.element;
            const UIElement* previous = nullptr;
            for (int t = 0; t < spec.maxTiles; ++t) {
                const std::string name = TileImageName(kind, t).c_str();
                const UIElement* picture = Element("Actions", name.c_str());
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

    TEST_F(MahjongTableScreenTest, EveryActionButtonIsATouchTargetFromAWindowOf731By411) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kButtonCount; ++i) {
            canvas->SetElementVisible(PoolString::Intern(SpecOf(static_cast<ButtonKind>(i)).element), true);
        }
        canvas->UpdateLayout(kViewWidth, kViewHeight);

        for (int i = 0; i < kButtonCount; ++i) {
            const UIElement* element = Element("Actions", SpecOf(static_cast<ButtonKind>(i)).element);
            ASSERT_NE(element, nullptr);
            EXPECT_GE(element->rect.h, kButtonHeight) << SpecOf(static_cast<ButtonKind>(i)).element;
            const float fit = kSmallestTouchWindowHeight / kViewHeight;  // the window of 731 x 411 shows the 1280 x 720 view at this scale
            EXPECT_GE(element->rect.h * fit, CoreManager::GetUIEnvironment().GetSettings().minTouchSize - 0.5f) << SpecOf(static_cast<ButtonKind>(i)).element;
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
        for (const std::vector<ButtonKind>& kinds :
             {std::vector<ButtonKind>{ButtonKind::Win, ButtonKind::Declare, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2, ButtonKind::UpgradeKong},
              std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::ClaimKong, ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2,
                                      ButtonKind::Pass}}) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            ExpectPassesTheMatrix("Actions", kSmallestWindowTouchMinimum);
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

    TEST_F(MahjongTableScreenTest, TheCoverFollowsTheModalAnatomy) {
        constexpr float kPlatePadding = 16.0f;
        constexpr float kSectionGap = 16.0f;
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        CoreManager::GetUIVars().Set("handoffSeat"_intern, eastl::string_view(kLongestSeat));
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Handoff"_intern, true);
        Draw();
        Canvas("Handoff")->UpdateLayout(kViewWidth, kViewHeight);

        const UIElement* plate = Element("Handoff", "Plate");
        const UIElement* text = Element("Handoff", "HandoffText");
        const UIElement* reveal = Element("Handoff", "Reveal");
        ASSERT_TRUE(plate != nullptr && text != nullptr && reveal != nullptr);
        EXPECT_TRUE(SameColor(plate->color, PaletteColor("surface")));
        EXPECT_TRUE(SameColor(text->color, PaletteColor("header")));
        EXPECT_TRUE(SameColor(reveal->color, PaletteColor("primary")));
        EXPECT_EQ(reveal->rect.h, 64.0f);
        EXPECT_EQ(text->rect.y, plate->rect.y + kPlatePadding);
        EXPECT_EQ(reveal->rect.y - (text->rect.y + text->rect.h), kSectionGap);
        EXPECT_EQ(reveal->rect.y + reveal->rect.h, plate->rect.y + plate->rect.h - kPlatePadding) << "the plate holds the band and the button and nothing more";
    }

    TEST_F(MahjongTableScreenTest, TheButtonsLieInTheFreeCornerOfTheTableAndCoverNoTile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        // The worst-case layout: every zone at its capacity, so no tile of any game can lie where a button does
        TableInput worst;
        worst.viewer = 0;
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
        const TableLayout layout = MakeTableLayout(worst);
        const UIElement* row = Element("Actions", "ActionRow");
        ASSERT_NE(row, nullptr);

        for (const TileSlot& slot : layout.slots) {
            const bool apart =
                slot.rect.x + slot.rect.w <= row->rect.x || slot.rect.x >= row->rect.x + row->rect.w || slot.rect.y + slot.rect.h <= row->rect.y || slot.rect.y >= row->rect.y + row->rect.h;
            EXPECT_TRUE(apart) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
        }

        // The row holds what is in it: the buttons of the fullest turn and of the fullest claim window lie inside the row
        // (the gap of the stack is also between its lines) and so clear of every slot
        for (const std::vector<ButtonKind>& kinds :
             {std::vector<ButtonKind>{ButtonKind::Win, ButtonKind::Declare, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2, ButtonKind::UpgradeKong},
              std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::ClaimKong, ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2,
                                      ButtonKind::Pass}}) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            Canvas("Actions")->UpdateLayout(kViewWidth, kViewHeight);
            for (const ButtonKind kind : kinds) {
                const UIElement* button = Element("Actions", SpecOf(kind).element);
                ASSERT_NE(button, nullptr) << SpecOf(kind).element;
                const Rect& r = button->rect;
                EXPECT_GE(r.x, row->rect.x) << SpecOf(kind).element;
                EXPECT_GE(r.y, row->rect.y) << SpecOf(kind).element;
                EXPECT_LE(r.x + r.w, row->rect.x + row->rect.w) << SpecOf(kind).element;
                EXPECT_LE(r.y + r.h, row->rect.y + row->rect.h) << SpecOf(kind).element << " lies below the row";
                for (const TileSlot& slot : layout.slots) {
                    const bool apart = slot.rect.x + slot.rect.w <= r.x || slot.rect.x >= r.x + r.w || slot.rect.y + slot.rect.h <= r.y || slot.rect.y >= r.y + r.h;
                    EXPECT_TRUE(apart) << SpecOf(kind).element << " covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                }
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
