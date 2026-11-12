#include "ComputerTestSupport.h"
#include "MahjongTouchTestSupport.h"

#include <Events/SceneEvents.h>
#include <Mahjong/Table/SeatPlates.h>
#include <Mahjong/Table/TapSelect.h>

/**
 * The table in a landscape phone (`TableProfileFor`, `Table/TouchRegime.h`): the component builds its layout with the profile the rule
 * gives the window it is drawn in (the output size of the renderer, the pixel density of the UI environment, the engine's touch minimum),
 * so the viewer's hand is a touch target there and the table of a desktop window is the one it always was. The fixture's renderer stands
 * for the window (`SetWindow`); the windows are those of `MahjongTouchTestSupport.h`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seat 0 holds a hand that is far from waiting; as East it draws 9p, so it holds 14 tiles and is the one to discard.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";

    }  // namespace

    class MahjongTablePhoneTest : public MahjongTableTestBase {
    protected:
        /// A match with seat 0 holding a fixed hand (it is to discard), drawn in `window`.
        void StartIn(const MatrixWindow& window) {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            Lay(MakeModel(SpecOf(kPlainHand, "9p"), kSeat0));
            SetWindow(*_renderer, window);
            Draw();
        }

        /// A click at a point of the view as the window shows it: the point in output pixels.
        bool ClickViewPoint(const MatrixWindow& window, float x, float y) {
            const Rect output = TableFit(window).ToOutput(x, y, 0.0f, 0.0f);
            return Click(output.x, output.y);
        }
    };

    TEST_F(MahjongTablePhoneTest, EveryWindowDrawsTheTableOfTheProfileTheRuleGivesIt) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Lay(MakeModel(SpecOf(kPlainHand, "9p"), kSeat0));
        size_t phones = 0;
        size_t desktops = 0;
        ForEachTableWindow([&](const MatrixWindow& window) {
            SetWindow(*_renderer, window);
            Draw();
            const TableLayout expected = TableLayoutFor(window, InputOf(Hand()));
            EXPECT_EQ(_table->GetLayout().profile, TableProfileOf(window)) << window.label.c_str();
            EXPECT_TRUE(SameSlots(_table->GetLayout(), expected)) << window.label.c_str() << ": the component draws the layout of the rule's profile";
            (_table->GetLayout().profile.phone ? phones : desktops)++;
        });
        const size_t landscapePhones = static_cast<size_t>(eastl::count_if(PhoneWindows().begin(), PhoneWindows().end(), [](const MatrixWindow& window) { return IsLandscapeLike(ShapeOf(window)); }));
        EXPECT_GT(phones, landscapePhones - 1) << "the phone windows and the short windows of the matrix";
        EXPECT_GT(desktops, 0u) << "the large windows of the matrix keep the desktop table";
    }

    TEST_F(MahjongTablePhoneTest, ThePhoneHandIsATouchTargetInEveryPhoneWindow) {
        for (const MatrixWindow& window : PhoneWindows()) {
            if (ShapeOf(window) == TableShape::Portrait) {
                continue;  // the portrait table has no phone profile: its hand is one row, tapped (`MahjongTablePortraitTest`)
            }
            ASSERT_NO_FATAL_FAILURE(StartIn(window));
            const TableLayout& layout = _table->GetLayout();
            ASSERT_TRUE(layout.profile.phone) << window.label.c_str();
            const float scale = TableFit(window).scale;
            int tiles = 0;
            for (const TileSlot& slot : layout.slots) {
                if (slot.role != SlotRole::HandTile || slot.seat != layout.viewer) {
                    continue;
                }
                ++tiles;
                EXPECT_GE(slot.rect.w * scale / window.pixelDensity, EngineTouchMinimum() - 0.01f) << window.label.c_str() << " tile " << slot.index << " is a touch target";
            }
            EXPECT_EQ(tiles, kDealtTiles + 1) << window.label.c_str();
        }
    }

    TEST_F(MahjongTablePhoneTest, AClickOnTheMiddleOfATileOfAPhoneWindowDiscardsThatTile) {
        for (const MatrixWindow& window : PhoneWindows()) {
            const bool selects = UsesTapToSelect(false, window.outputWidth, window.outputHeight, window.pixelDensity, EngineTouchMinimum(), TableProfileOf(window).handTileW, ViewOf(window).width,
                                                 ViewOf(window).height);
            for (int index = 0; index < kDealtTiles + 1; ++index) {
                ASSERT_NO_FATAL_FAILURE(StartIn(window));
                ASSERT_EQ(_game->GetSession().Actor(), kSeat0);
                const Tile tile = Hand().GetHand(kSeat0).concealed[static_cast<size_t>(index)];
                const TileSlot* slot = HandSlot(index);
                ASSERT_NE(slot, nullptr) << window.label.c_str();
                const float x = slot->rect.x + slot->rect.w * 0.5f;
                const float y = slot->rect.y + slot->rect.h * 0.5f;
                EXPECT_FALSE(ClickViewPoint(window, x, y)) << "no canvas takes the click: the table does";
                if (selects) {
                    // A hand tile under the touch minimum (the one row of the portrait table in a phone): the first click selects, the second discards
                    ASSERT_TRUE(Hand().Discards().empty()) << window.label.c_str() << " tile " << index;
                    Draw();
                    EXPECT_EQ(_table->SelectedHandIndex(), index) << window.label.c_str();
                    EXPECT_FALSE(ClickViewPoint(window, x, y));
                }
                ASSERT_FALSE(Hand().Discards().empty()) << window.label.c_str() << " tile " << index;
                EXPECT_EQ(Hand().Discards().front().tile, tile) << window.label.c_str() << " tile " << index;
            }
        }
    }

    TEST_F(MahjongTablePhoneTest, TheWordsOfEveryPlateLieOnItInEveryWindow) {
        // The labels of `MahjongSeatPlates.xml` name the region of their plate (the bottom plate of the phone profile is lifted to end where
        // the tallest hand tile starts, and its region with it), so each must lie inside its plate, under the ring box of its emblem
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        ForEachTableWindow([&](const MatrixWindow& window) {
            LayOutOnTheTable({"SeatPlates"}, window);
            const TableLayout layout = MakeWorstCaseTableLayout(0, window);
            for (int position = 0; position < kSeatCount; ++position) {
                const UIElement* name = Element("SeatPlates", Format("PlateName{}", position).c_str());
                ASSERT_NE(name, nullptr) << position;
                const Rect& plate = layout.zones.seatPlates[SeatAtPosition(layout.viewer, position)];
                EXPECT_GE(name->rect.x, plate.x) << window.label.c_str() << " plate " << position;
                EXPECT_GE(name->rect.y, EmblemBox(plate, 1.0f).y + EmblemBox(plate, 1.0f).h) << window.label.c_str() << " plate " << position << ": under the emblem";
                EXPECT_LE(name->rect.x + name->rect.w, plate.x + plate.w) << window.label.c_str() << " plate " << position;
                EXPECT_LE(name->rect.y + name->rect.h, plate.y + plate.h) << window.label.c_str() << " plate " << position;
            }
        });
    }

    TEST_F(MahjongTablePhoneTest, ADesktopWindowDrawsTodaysTable) {
        MatrixWindow design;
        design.outputWidth = kViewWidth;
        design.outputHeight = kViewHeight;
        ASSERT_NO_FATAL_FAILURE(StartIn(design));
        EXPECT_EQ(_table->GetLayout().profile, TableProfile{});
        EXPECT_TRUE(SameSlots(_table->GetLayout(), MakeTableLayout(InputOf(Hand())))) << "1280 x 720 at density 1: the default layout";
        for (const TileSlot& slot : _table->GetLayout().slots) {
            if (slot.role == SlotRole::HandTile && slot.seat == _table->GetLayout().viewer) {
                EXPECT_EQ(slot.rect.w, kViewerHandTileW);
            }
        }
    }

    TEST_F(MahjongTablePhoneTest, AChangeOfWindowBetweenTwoDrawsStartsNoFlight) {
        UseFlights();
        MatrixWindow design;
        design.outputWidth = kViewWidth;
        design.outputHeight = kViewHeight;

        // Control: a discard between two draws in the same window flies
        ASSERT_NO_FATAL_FAILURE(StartIn(design));
        ASSERT_TRUE(ClickHandTile(0) == false);
        Draw();
        EXPECT_GT(_table->FlightCount(), 0u) << "a discard makes a flight";

        // The window gets the phone's size and density: a discard and the window change between the draws
        const MatrixWindow phone = PhoneWindows().front();
        ASSERT_NO_FATAL_FAILURE(StartIn(design));
        ASSERT_TRUE(ClickHandTile(0) == false);
        SetWindow(*_renderer, phone);
        Draw();
        EXPECT_EQ(_table->GetLayout().profile, TableProfileOf(phone));
        EXPECT_EQ(_table->FlightCount(), 0u) << "another size and profile: the tiles are in place";

        // The same output at another density: only the profile changes
        ASSERT_NO_FATAL_FAILURE(StartIn(design));
        ASSERT_TRUE(ClickHandTile(0) == false);
        MatrixWindow dense = design;
        dense.pixelDensity = 2.6f;
        SetWindow(*_renderer, dense);
        Draw();
        ASSERT_TRUE(_table->GetLayout().profile.phone) << "1280 x 720 pixels at density 2.6 are a phone window";
        EXPECT_EQ(_table->FlightCount(), 0u) << "another profile at the same output size starts no flight";
    }

}  // namespace BECore::Mahjong::Tests
