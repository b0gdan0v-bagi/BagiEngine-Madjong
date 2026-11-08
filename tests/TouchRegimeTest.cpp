#include <Common/UILayoutMatrix.h>
#include <Mahjong/Table/TouchRegime.h>
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TouchRegime.h` (pure logic): the one rule that picks the canvas of the action buttons, `Actions` (a `SceneView` canvas, scaled
 * with the table) or `ActionsCompact` (a `Screen` canvas), by the size a button of `Actions` would have on screen.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kMinTouch = 32.0f;
        constexpr float kDensity = 1.0f;

        bool Compact(float width, float height, float density = kDensity, float minTouch = kMinTouch) {
            return NeedsCompactActions(width, height, density, minTouch);
        }

    }  // namespace

    TEST(TouchRegimeTest, TheFitIsTheSmallerOfTheTwoScalesAndZeroForNoSize) {
        EXPECT_FLOAT_EQ(FitScale(1280.0f, 720.0f), 1.0f);
        EXPECT_FLOAT_EQ(FitScale(2560.0f, 1440.0f), 2.0f);
        EXPECT_FLOAT_EQ(FitScale(640.0f, 720.0f), 0.5f) << "width limited";
        EXPECT_FLOAT_EQ(FitScale(1920.0f, 360.0f), 0.5f) << "height limited";
        EXPECT_EQ(FitScale(0.0f, 720.0f), 0.0f);
        EXPECT_EQ(FitScale(1280.0f, -1.0f), 0.0f);
        EXPECT_EQ(FitScale(std::nanf(""), 720.0f), 0.0f);
    }

    TEST(TouchRegimeTest, AButtonIs32PixelsFromTheFitOf0Point571AndTheRuleSaysSo) {
        // 56 units x fit = 32 px at fit 0.5714, a window of 731.4 x 411.4: the rule is the plain comparison
        EXPECT_FALSE(Compact(732.0f, 412.0f));
        EXPECT_FALSE(Compact(1280.0f, 720.0f));
        EXPECT_FALSE(Compact(853.0f, 480.0f));
        EXPECT_TRUE(Compact(731.0f, 411.0f)) << "56 x 0.5708 = 31.97 px, below 32";
        EXPECT_TRUE(Compact(730.0f, 410.0f));
        EXPECT_TRUE(Compact(480.0f, 480.0f));
        EXPECT_TRUE(Compact(480.0f, 853.0f)) << "a portrait window is width limited";
        // Width limited: 56 x 731/1280 = 31.98 px, 56 x 733/1280 = 32.07 px, however tall the window is
        EXPECT_TRUE(Compact(731.0f, 2000.0f));
        EXPECT_FALSE(Compact(733.0f, 2000.0f));
        // Height limited, however wide it is
        EXPECT_TRUE(Compact(3840.0f, 411.0f));
        EXPECT_FALSE(Compact(3840.0f, 412.0f));
    }

    TEST(TouchRegimeTest, TheHeightOfAButtonIsTheOneOf56UnitsTheSceneFileHas) {
        EXPECT_EQ(kActionButtonHeight, 56.0f);
        // The rule is the comparison of that height with the minimum: a bigger minimum moves the boundary up
        EXPECT_FALSE(Compact(853.0f, 480.0f, kDensity, 32.0f));
        EXPECT_TRUE(Compact(853.0f, 480.0f, kDensity, 48.0f)) << "56 x 0.667 = 37 px";
        EXPECT_FALSE(Compact(853.0f, 480.0f, kDensity, 37.0f));
    }

    TEST(TouchRegimeTest, TheDensityDividesTheSizeOfAButton) {
        // A window of 1280 x 720 pixels at density 2 is 640 x 360 window units: a button is 28 units there
        EXPECT_FALSE(Compact(1280.0f, 720.0f, 1.0f));
        EXPECT_TRUE(Compact(1280.0f, 720.0f, 2.0f));
        EXPECT_FALSE(Compact(2560.0f, 1440.0f, 2.0f));
        EXPECT_FALSE(Compact(1280.0f, 720.0f, 1.5f)) << "56 / 1.5 = 37.3 units";
    }

    TEST(TouchRegimeTest, NoSizeOrNoDensityIsNotCompactSoTheFirstFrameShowsActions) {
        EXPECT_FALSE(Compact(0.0f, 0.0f));
        EXPECT_FALSE(Compact(0.0f, 720.0f));
        EXPECT_FALSE(Compact(480.0f, 0.0f));
        EXPECT_FALSE(Compact(-480.0f, 480.0f));
        EXPECT_FALSE(Compact(480.0f, 480.0f, 0.0f));
        EXPECT_FALSE(Compact(480.0f, 480.0f, -1.0f));
        EXPECT_FALSE(Compact(std::nanf(""), 480.0f));
        EXPECT_FALSE(Compact(480.0f, 480.0f, std::nanf("")));
    }

    TEST(TouchRegimeTest, TheWholeMatrixIsCompactExactlyWhere56TimesTheFitIsBelow32) {
        int compact = 0;
        int roomy = 0;
        for (const BECore::Tests::UIMatrixCase& matrixCase : BECore::Tests::UILayoutMatrix()) {
            const float width = matrixCase.context.outputWidth;
            const float height = matrixCase.context.outputHeight;
            const float fit = std::min(width / kViewWidth, height / kViewHeight);  // worked out here, not by FitScale
            const bool expected = 56.0f * fit < kMinTouch;
            EXPECT_EQ(Compact(width, height), expected) << matrixCase.label.c_str();
            expected ? ++compact : ++roomy;
        }
        // Both kinds of window are in the matrix: the 480 px ones are compact, the full HD ones are not
        EXPECT_GT(compact, 0);
        EXPECT_GT(roomy, 0);
        EXPECT_TRUE(Compact(480.0f, 480.0f));
        EXPECT_FALSE(Compact(1920.0f, 1080.0f));
    }

}  // namespace BECore::Mahjong::Tests
