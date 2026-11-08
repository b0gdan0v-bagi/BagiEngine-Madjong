#include "MahjongTableTestSupport.h"

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
        /// The height of an action button in view units: 32 px (the engine's touch minimum) from a window of 731 x 411 px.
        constexpr float kButtonHeight = 56.0f;
        /// The height in pixels of the smallest window the touch minimum is held from; the view is 1280 x 720 units, scaled to fit it.
        constexpr float kSmallestTouchWindowHeight = 411.0f;
        /// The table is a fixed design scaled to the window, so on the smallest windows of the matrix (480 px) a button is 21 px high:
        /// the matrix runs the buttons with this minimum, and the engine's own is held from the 731 px windows (the test of the height).
        constexpr float kSmallestWindowTouchMinimum = 20.0f;
        /// The height of the line of the word at the top of a button; the pictures of tiles lie below it.
        constexpr float kWordLineHeight = 24.0f;
        /// The smallest tile picture in view units (the 3:4 of an atlas cell), so a tile can be told at the smallest window of the matrix.
        constexpr float kButtonWidth = 132.0f;
        constexpr float kLeastTileWidth = 20.0f;
        constexpr float kLeastTileHeight = 26.0f;
        constexpr float kTileShape = 3.0f / 4.0f;
        constexpr float kShapeTolerance = 0.02f;
        /// A pile longer than any hand leaves (136 tiles, 52 in the hands at the least).
        constexpr int kMostDiscards = 100;

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
                const std::string name = TileImageName(static_cast<ButtonKind>(i), t).c_str();
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
                EXPECT_EQ(std::string(label->text.c_str()).find('{'), std::string::npos) << name << " has a variable in its text";
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
            const ButtonSpec& spec = SpecOf(static_cast<ButtonKind>(i));
            const UIElement* button = Element("Actions", spec.element);
            ASSERT_NE(button, nullptr) << spec.element;
            const Rect& outer = button->rect;
            EXPECT_GE(outer.w, kButtonWidth) << spec.element;
            EXPECT_GE(outer.h, kButtonHeight) << spec.element;
            const UIElement* previous = nullptr;
            for (int t = 0; t < spec.maxTiles; ++t) {
                const std::string name = TileImageName(static_cast<ButtonKind>(i), t).c_str();
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
    }

}  // namespace BECore::Mahjong::Tests
