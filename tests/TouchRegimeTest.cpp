#include <Common/UILayoutMatrix.h>
#include <Mahjong/Table/TouchRegime.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TouchRegime.h` (pure logic): the one rule that picks the canvas of the action buttons, `Actions` (a `SceneView` canvas, scaled
 * with the table) or `ActionsCompact` (a `Screen` canvas on the bar under the table): compact where a button of `Actions` would be under
 * the touch minimum and the bar can hold one line of the compact row.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kMinTouch = 32.0f;
        constexpr float kDensity = 1.0f;

        bool Compact(float width, float height, float density = kDensity, float minTouch = kMinTouch, float userScale = 1.0f) {
            return NeedsCompactActions(width, height, density, minTouch, userScale);
        }

        /// The size of a button of `Actions` in window units, worked out here.
        float ButtonSize(float width, float height, float density = kDensity) {
            return 56.0f * std::min(width / kViewWidth, height / kViewHeight) / density;
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

    TEST(TouchRegimeTest, AButtonIs32PixelsFromTheFitOf0Point571AndTheRuleSaysSoWhereABarHoldsTheCompactRow) {
        // 56 units x fit = 32 px at fit 0.5714, a window of 731.4 x 411.4: above it `Actions`, below it the compact row where there is a bar
        EXPECT_FALSE(Compact(732.0f, 412.0f));
        EXPECT_FALSE(Compact(1280.0f, 720.0f));
        EXPECT_FALSE(Compact(853.0f, 480.0f));
        EXPECT_TRUE(Compact(480.0f, 480.0f));
        EXPECT_TRUE(Compact(480.0f, 853.0f)) << "a portrait window is width limited";
        EXPECT_TRUE(Compact(720.0f, 480.0f)) << "3:2, 56 x 0.5625 = 31.5 px, the bar is 37.5 px";
        // Width limited: 56 x 731/1280 = 31.98 px, 56 x 733/1280 = 32.07 px, however tall the window is
        EXPECT_TRUE(Compact(731.0f, 2000.0f));
        EXPECT_FALSE(Compact(733.0f, 2000.0f));
        // Height limited and wide: the table fills the height, there is no bar, so no compact row whatever the button size
        EXPECT_FALSE(Compact(3840.0f, 411.0f));
        EXPECT_FALSE(Compact(3840.0f, 412.0f));
    }

    TEST(TouchRegimeTest, AWindowAsHighAsTheTableKeepsActionsWhateverSizeTheButtonsHave) {
        // The compact row lies at the bottom of the window: with no bar under the table it would lie on the viewer's hand
        EXPECT_FALSE(Compact(731.0f, 411.0f)) << "a button is 31.97 px, the table fills the window";
        EXPECT_LT(ButtonSize(731.0f, 411.0f), kMinTouch);
        EXPECT_FALSE(Compact(720.0f, 405.0f)) << "16:9";
        EXPECT_FALSE(Compact(1000.0f, 400.0f)) << "short and wide: the bars are at the sides";
        EXPECT_LT(ButtonSize(1000.0f, 400.0f), kMinTouch);
        EXPECT_FALSE(Compact(640.0f, 360.0f));
        EXPECT_FALSE(Compact(700.0f, 420.0f)) << "a bar of 13 px";
        // A landscape phone at density 3: 2532 x 1170 pixels is 844 x 390 window units, a button 30.3 units, no bar
        EXPECT_LT(ButtonSize(2532.0f, 1170.0f, 3.0f), kMinTouch);
        EXPECT_FALSE(Compact(2532.0f, 1170.0f, 3.0f));
        EXPECT_NEAR(BarUnderTable(2532.0f, 1170.0f), 0.0f, 0.01f);
    }

    TEST(TouchRegimeTest, ATallWindowIsCompactAtADensityToo) {
        // A portrait phone at density 3: 1170 x 2532 pixels, a button 56 x 0.914 / 3 = 17 window units; the bar is 750 px
        EXPECT_TRUE(Compact(1170.0f, 2532.0f, 3.0f));
        EXPECT_TRUE(Compact(1080.0f, 1920.0f, 2.0f));
    }

    TEST(TouchRegimeTest, TheBarMustHoldOneLineOfTheCompactRowAtTheScaleOfThatCanvas) {
        // 640 wide: the table is 360 high at fit 0.5 (a button 28 px). Below a height of 518 the compact scale is its floor 0.6, a line 33.6 px,
        // and the bar is (height - 360) / 2: 34 px at 428, 33.5 px at 427
        EXPECT_FLOAT_EQ(CompactScale(640.0f, 428.0f), kCompactMinScale);
        EXPECT_FLOAT_EQ(BarUnderTable(640.0f, 428.0f), 34.0f);
        EXPECT_TRUE(Compact(640.0f, 428.0f));
        EXPECT_FLOAT_EQ(BarUnderTable(640.0f, 427.0f), 33.5f);
        EXPECT_FALSE(Compact(640.0f, 427.0f));
        EXPECT_TRUE(Compact(640.0f, 480.0f));
        // Where the compact scale rises above its floor the line is higher than 33.6 px: 1000 x 700 has a scale of 0.8715 (a line of 48.8 px)
        // and a bar of (700 - 562.5) / 2 = 68.75 px
        EXPECT_NEAR(CompactScale(1000.0f, 700.0f), 0.8715f, 0.0001f);
        EXPECT_TRUE(Compact(1000.0f, 700.0f, 1.0f, 48.0f)) << "a button of 43.75 px is under a minimum of 48";
        EXPECT_FALSE(Compact(1000.0f, 600.0f, 1.0f, 48.0f)) << "the bar of 18.75 px holds no line of 45 px";
    }

    TEST(TouchRegimeTest, TheUserScaleMakesTheCompactRowHigherAndSoNeedsABiggerBar) {
        // 640 x 440: the bar is 40 px; a line is 33.6 px at the user scale 1 and 46.5 px at 1.5
        EXPECT_TRUE(Compact(640.0f, 440.0f));
        EXPECT_TRUE(Compact(640.0f, 440.0f, kDensity, kMinTouch, 1.0f));
        EXPECT_FALSE(Compact(640.0f, 440.0f, kDensity, kMinTouch, 1.5f));
        EXPECT_NEAR(CompactScale(640.0f, 440.0f, 1.5f), 0.830f, 0.001f);
        // A user scale that is not positive counts as 1, as the engine's scaler does
        EXPECT_FLOAT_EQ(CompactScale(640.0f, 440.0f, 0.0f), CompactScale(640.0f, 440.0f, 1.0f));
        EXPECT_FLOAT_EQ(CompactScale(640.0f, 440.0f, -2.0f), CompactScale(640.0f, 440.0f, 1.0f));
    }

    TEST(TouchRegimeTest, TheCompactScaleIsTheScaleWithScreenOfTheReferenceSizeWithAFloor) {
        EXPECT_FLOAT_EQ(CompactScale(1280.0f, 720.0f), 1.0f);
        EXPECT_FLOAT_EQ(CompactScale(2560.0f, 1440.0f), 2.0f);
        EXPECT_NEAR(CompactScale(2560.0f, 720.0f), std::sqrt(2.0f), 1e-5f) << "the geometric mean of the width and height ratios";
        EXPECT_FLOAT_EQ(CompactScale(480.0f, 480.0f), kCompactMinScale) << "sqrt(0.375 x 0.667) = 0.5, below the floor";
        EXPECT_FLOAT_EQ(CompactScale(0.0f, 480.0f), kCompactMinScale);
        EXPECT_EQ(BarUnderTable(0.0f, 480.0f), 0.0f);
        EXPECT_FLOAT_EQ(BarUnderTable(480.0f, 853.0f), (853.0f - 270.0f) / 2.0f);
        EXPECT_NEAR(BarUnderTable(1280.0f, 720.0f), 0.0f, 0.01f);
        EXPECT_NEAR(BarUnderTable(3840.0f, 411.0f), 0.0f, 0.01f) << "bars at the sides, none under";
    }

    TEST(TouchRegimeTest, TheHeightOfAButtonIsTheOneOf56UnitsTheSceneFileHas) {
        EXPECT_EQ(kActionButtonHeight, 56.0f);
        // The rule is the comparison of that height with the minimum: a bigger minimum moves the boundary up
        EXPECT_FALSE(Compact(853.0f, 480.0f, kDensity, 32.0f));
        EXPECT_FALSE(Compact(853.0f, 480.0f, kDensity, 48.0f)) << "56 x 0.667 = 37 px, but the table fills the height of 480";
        EXPECT_TRUE(Compact(640.0f, 480.0f, kDensity, 32.0f));
        EXPECT_TRUE(Compact(853.0f, 600.0f, kDensity, 48.0f)) << "56 x 0.667 = 37 px, a bar of 60 px";
        EXPECT_FALSE(Compact(853.0f, 600.0f, kDensity, 37.0f));
    }

    TEST(TouchRegimeTest, TheDensityDividesTheSizeOfAButton) {
        // A window of 1280 x 720 pixels at density 2 is 640 x 360 window units: a button is 28 units there, but the table fills it
        EXPECT_FALSE(Compact(1280.0f, 720.0f, 1.0f));
        EXPECT_FALSE(Compact(1280.0f, 720.0f, 2.0f));
        EXPECT_FALSE(Compact(2560.0f, 1440.0f, 2.0f));
        EXPECT_FALSE(Compact(1280.0f, 720.0f, 1.5f)) << "56 / 1.5 = 37.3 units";
        // A window of 1280 x 1000 pixels at density 2 has a bar (140 px) and a button of 28 units
        EXPECT_TRUE(Compact(1280.0f, 1000.0f, 2.0f));
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
        // Every window of the matrix is 480 px or more on its short side, so each one below the touch size has a bar that holds the row
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
