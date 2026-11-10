#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/TouchRegime.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TapSelect.h` (pure logic): the tap rule of the viewer's hand (select, confirm, move, clear, or discard at once), the rule that
 * picks it (`UsesTapToSelect`) and the rectangle of a raised tile.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kMinTouch = 32.0f;
        constexpr std::optional<int> kNone = std::nullopt;

        TapOutcome Out(TapAction action, int index = 0) {
            return TapOutcome{action, index};
        }

    }  // namespace

    TEST(TapSelectTest, WithTheRegimeOffAHitDiscardsAtOnceAndNoHitDoesNothing) {
        EXPECT_EQ(StepTap(kNone, 4, false), Out(TapAction::Discard, 4));
        EXPECT_EQ(StepTap(2, 4, false), Out(TapAction::Discard, 4));
        EXPECT_EQ(StepTap(4, 4, false), Out(TapAction::Discard, 4));
        EXPECT_EQ(StepTap(kNone, kNone, false), Out(TapAction::Nothing));
        EXPECT_EQ(StepTap(3, kNone, false), Out(TapAction::Nothing)) << "the one-click regime keeps no selection to clear";
    }

    TEST(TapSelectTest, TheFirstTapSelectsAndTheSecondOnTheSameTileDiscards) {
        EXPECT_EQ(StepTap(kNone, 5, true), Out(TapAction::Select, 5));
        EXPECT_EQ(StepTap(5, 5, true), Out(TapAction::Discard, 5));
        EXPECT_EQ(StepTap(0, 0, true), Out(TapAction::Discard, 0)) << "index 0 is a tile like any other";
    }

    TEST(TapSelectTest, ATapOnAnotherTileMovesTheSelection) {
        EXPECT_EQ(StepTap(5, 6, true), Out(TapAction::Select, 6));
        EXPECT_EQ(StepTap(5, 0, true), Out(TapAction::Select, 0));
    }

    TEST(TapSelectTest, ATapOnNothingClearsTheSelectionWhenThereIsOne) {
        EXPECT_EQ(StepTap(5, kNone, true), Out(TapAction::Clear));
        EXPECT_EQ(StepTap(0, kNone, true), Out(TapAction::Clear));
        EXPECT_EQ(StepTap(kNone, kNone, true), Out(TapAction::Nothing));
    }

    TEST(TouchRegimeTest, ATouchInputTurnsTheTapRuleOnInAnyWindow) {
        EXPECT_TRUE(UsesTapToSelect(true, 1920.0f, 1080.0f, 1.0f, kMinTouch));
        EXPECT_TRUE(UsesTapToSelect(true, 640.0f, 360.0f, 1.0f, kMinTouch));
        EXPECT_TRUE(UsesTapToSelect(true, 0.0f, 0.0f, 0.0f, kMinTouch)) << "a touch needs no size";
    }

    TEST(TouchRegimeTest, ASmallWindowTurnsTheTapRuleOnWithNoTouch) {
        EXPECT_TRUE(UsesTapToSelect(false, 640.0f, 360.0f, 1.0f, kMinTouch)) << "a hand tile is 24 units wide";
        EXPECT_TRUE(UsesTapToSelect(false, 800.0f, 450.0f, 1.0f, kMinTouch)) << "30 units";
        // the tile is 48 x fit: exactly the minimum at 853.33 units wide
        EXPECT_FALSE(UsesTapToSelect(false, 854.0f, 480.0f, 1.0f, kMinTouch));
        EXPECT_TRUE(UsesTapToSelect(false, 852.0f, 480.0f, 1.0f, kMinTouch));
        EXPECT_TRUE(UsesTapToSelect(false, 1280.0f, 720.0f, 2.0f, 40.0f)) << "the density counts: 24 window units";
    }

    TEST(TouchRegimeTest, ALargeWindowWithNoTouchKeepsTheOneClickDiscard) {
        EXPECT_FALSE(UsesTapToSelect(false, 1280.0f, 720.0f, 1.0f, kMinTouch));
        EXPECT_FALSE(UsesTapToSelect(false, 1920.0f, 1080.0f, 2.0f, kMinTouch));
    }

    TEST(TouchRegimeTest, APhoneProfileWhoseTileReachesTheMinimumKeepsTheOneClickDiscard) {
        // A landscape phone: the desktop tile is under the minimum, the profile's tile is made just wide enough
        constexpr float kWidth = 915.0f * 2.6f;
        constexpr float kHeight = 330.0f * 2.6f;
        const TableProfile profile = TableProfileFor(kWidth, kHeight, 2.6f, kMinTouch);
        ASSERT_TRUE(profile.phone);
        ASSERT_LT(profile.handTileW, kPhoneHandTileMaxW);
        EXPECT_TRUE(UsesTapToSelect(false, kWidth, kHeight, 2.6f, kMinTouch)) << "the desktop tile";
        EXPECT_FALSE(UsesTapToSelect(false, kWidth, kHeight, 2.6f, kMinTouch, profile.handTileW));
        EXPECT_TRUE(UsesTapToSelect(true, kWidth, kHeight, 2.6f, kMinTouch, profile.handTileW)) << "a touch still selects first";
    }

    TEST(TouchRegimeTest, NoSizeOrNoDensityIsNoSmallWindow) {
        EXPECT_FALSE(UsesTapToSelect(false, 0.0f, 360.0f, 1.0f, kMinTouch));
        EXPECT_FALSE(UsesTapToSelect(false, 640.0f, -1.0f, 1.0f, kMinTouch));
        EXPECT_FALSE(UsesTapToSelect(false, std::nanf(""), 360.0f, 1.0f, kMinTouch));
        EXPECT_FALSE(UsesTapToSelect(false, 640.0f, 360.0f, 0.0f, kMinTouch));
        EXPECT_FALSE(UsesTapToSelect(false, 640.0f, 360.0f, -1.0f, kMinTouch));
    }

    TEST(TapSelectTest, ARaisedTileMovesUpByTheRiseAtTheScaleOfTheLayout) {
        const Rect slot(100.0f, 600.0f, kViewerHandTileW, kViewerHandTileH);
        const Rect raised = RaisedRect(slot);
        EXPECT_FLOAT_EQ(raised.x, slot.x);
        EXPECT_FLOAT_EQ(raised.w, slot.w);
        EXPECT_FLOAT_EQ(raised.h, slot.h);
        EXPECT_FLOAT_EQ(raised.y, slot.y - kSelectedTileRise);
        const Rect doubled(200.0f, 1200.0f, 2.0f * kViewerHandTileW, 2.0f * kViewerHandTileH);
        EXPECT_FLOAT_EQ(RaisedRect(doubled).y, doubled.y - 2.0f * kSelectedTileRise);
        EXPECT_NEAR(kSelectedTileRise, kViewerHandTileH / 4.0f, 0.001f);
    }

    TEST(TouchRegimeTest, APortraitPhoneSelectsFirstBecauseTheHandTileIsUnderTheMinimum) {
        // 390 x 844 at density 3: the hand tile of 48 units is 48 x 1.625 / 3 = 26 window units
        constexpr float kWidth = 1170.0f;
        constexpr float kHeight = 2532.0f;
        EXPECT_LT(kViewerHandTileW * (kWidth / kPortraitViewWidth) / 3.0f, kMinTouch);
        EXPECT_TRUE(UsesTapToSelect(false, kWidth, kHeight, 3.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
        // 360 x 640 at density 2: 24 window units
        EXPECT_TRUE(UsesTapToSelect(false, 720.0f, 1280.0f, 2.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
    }

    TEST(TouchRegimeTest, APortraitWindowWhoseHandTileReachesTheMinimumKeepsTheOneClickDiscard) {
        // 768 x 1024 at density 1: fit 0.8, a tile of 38.4 units
        EXPECT_FALSE(UsesTapToSelect(false, 768.0f, 1024.0f, 1.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
        // the boundary is 48 x fit = 32 at fit 2/3: an output 480 wide
        EXPECT_FALSE(UsesTapToSelect(false, 481.0f, 1280.0f, 1.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_TRUE(UsesTapToSelect(false, 479.0f, 1280.0f, 1.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
        // the landscape default reads the same pixels differently: 768 x 1024 is width limited there, a tile of 28.8 units
        EXPECT_TRUE(UsesTapToSelect(false, 768.0f, 1024.0f, 1.0f, kMinTouch));
    }

    TEST(TouchRegimeTest, ATouchSelectsFirstInAPortraitWindowAndNoSizeIsStillNoSmallWindow) {
        EXPECT_TRUE(UsesTapToSelect(true, 1440.0f, 2560.0f, 1.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_FALSE(UsesTapToSelect(false, 0.0f, 2560.0f, 1.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_FALSE(UsesTapToSelect(false, 720.0f, 1280.0f, 0.0f, kMinTouch, kViewerHandTileW, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_FALSE(UsesTapToSelect(false, 720.0f, 1280.0f, 1.0f, kMinTouch, kViewerHandTileW, 0.0f, 0.0f)) << "a view of no size";
    }

}  // namespace BECore::Mahjong::Tests
