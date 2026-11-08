#include "MahjongTableTestSupport.h"

#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIToggle.h>

/**
 * The canvases of the shipped scene that belong to the table (`Actions`, `Handoff`): the buttons of the component are the buttons of the
 * file with their action and value, the longest labels fit, both pass the size matrix, and the buttons lie where no tile of any game can.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kLongestChow = "Chow 7-9s";
        constexpr const char* kLongestKong = "Kong 9m";
        constexpr const char* kLongestUpgrade = "Add 9m";
        constexpr const char* kLongestSeat = "Seat 4 (North)";
        /// The height of an action button in view units: 32 px (the engine's touch minimum) from a window of 731 x 411 px.
        constexpr float kButtonHeight = 56.0f;
        /// The height in pixels of the smallest window the touch minimum is held from; the view is 1280 x 720 units, scaled to fit it.
        constexpr float kSmallestTouchWindowHeight = 411.0f;
        /// The table is a fixed design scaled to the window, so on the smallest windows of the matrix (480 px) a button is 21 px high:
        /// the matrix runs the buttons with this minimum, and the engine's own is held from the 731 px windows (the test of the height).
        constexpr float kSmallestWindowTouchMinimum = 20.0f;
        /// A pile longer than any hand leaves (136 tiles, 52 in the hands at the least).
        constexpr int kMostDiscards = 100;

    }  // namespace

    class MahjongTableScreenTest : public MahjongTableTestBase {};

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
                continue;
            }
            const auto* button = element->Cast<UIButton>();
            ASSERT_NE(button, nullptr) << spec.element;
            EXPECT_EQ(button->action, PoolString::Intern(spec.action)) << spec.element;
            EXPECT_EQ(button->value, PoolString::Intern(spec.value)) << spec.element;
            if (spec.labelVar != nullptr) {
                EXPECT_EQ(button->text, eastl::string(Format("{{{}}}", spec.labelVar))) << "the label of " << spec.element << " is its variable";
            }
        }
        EXPECT_NE(Element("Handoff", "Reveal"), nullptr);
        EXPECT_EQ(Element("Handoff", "Reveal")->Cast<UIButton>()->action, "MahjongReveal"_intern);
    }

    TEST_F(MahjongTableScreenTest, TheActionButtonsPassTheSizeMatrixWithTheLongestLabelsAndEveryButtonIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        UIVars& vars = CoreManager::GetUIVars();
        for (const char* name : {"chow0", "chow1", "chow2"}) {
            vars.Set(PoolString::Intern(name), eastl::string_view(kLongestChow));
        }
        for (const char* name : {"kong0", "kong1", "kong2"}) {
            vars.Set(PoolString::Intern(name), eastl::string_view(kLongestKong));
        }
        vars.Set("upgrade"_intern, eastl::string_view(kLongestUpgrade));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);

        // The fullest turn (a win, a declaration, three kongs and the upgrade) and the fullest window (all claims, three chows, pass)
        for (const std::vector<const char*>& names : {std::vector<const char*>{"Win", "Declare", "ConcealedKong0", "ConcealedKong1", "ConcealedKong2", "UpgradeKong"},
                                                      std::vector<const char*>{"Mahjong", "Kong", "Pung", "Chow0", "Chow1", "Chow2", "Pass"}}) {
            for (int i = 0; i < kButtonCount; ++i) {
                canvas->SetElementVisible(PoolString::Intern(SpecOf(static_cast<ButtonKind>(i)).element), false);
            }
            for (const char* name : names) {
                ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(name), true)) << name;
            }
            ExpectPassesTheMatrix("Actions", kSmallestWindowTouchMinimum);
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
