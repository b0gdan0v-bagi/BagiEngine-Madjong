#include <Common/UILayoutMatrix.h>
#include <Mahjong/Table/TouchRegime.h>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

/**
 * `Table/TouchRegime.h` (pure logic): the one rule that picks the canvas of the action buttons, `Actions` (a `SceneView` canvas, scaled
 * with the table) or `ActionsCompact` (a `Screen` canvas on the bar under the table): compact where a button of `Actions` would be under
 * the touch minimum and the bar holds the compact row at its height at that canvas width (`CompactRowHeight`, the variants of the scene
 * file).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kMinTouch = 32.0f;
        constexpr float kDensity = 1.0f;

        bool Compact(float width, float height, float density = kDensity, float minTouch = kMinTouch, float userScale = 1.0f) {
            return NeedsCompactActions(width, height, density, minTouch, userScale);
        }

        /// The lines the buttons of these widths make in a row of `safeWidth` units, wrapped in order with `kCompactSpacing` between them as a
        /// wrapped stack does, and the height of that many lines.
        float WrappedHeight(const std::vector<float>& widths, float safeWidth) {
            int lines = 1;
            float used = 0.0f;
            for (const float width : widths) {
                if (used > 0.0f && used + kCompactSpacing + width > safeWidth) {
                    ++lines;
                    used = 0.0f;
                }
                used += (used > 0.0f ? kCompactSpacing : 0.0f) + width;
            }
            return static_cast<float>(lines) * kActionButtonHeight + static_cast<float>(lines - 1) * kCompactSpacing;
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

    TEST(TouchRegimeTest, TheBarMustHoldTheRowAtItsHeightAtThatCanvasWidth) {
        // 640 x 480: the table is 360 high at fit 0.5 (a button 28 px), the bar is 60 px. The scale is the floor 0.6, the canvas 1066.7 units wide,
        // above 1043.91, so the row is one line of 56 units (33.6 px) and fits: compact
        EXPECT_FLOAT_EQ(CompactScale(640.0f, 480.0f), kCompactMinScale);
        EXPECT_FLOAT_EQ(BarUnderTable(640.0f, 480.0f), 60.0f);
        EXPECT_FLOAT_EQ(CompactRowHeight(640.0f / kCompactMinScale), kActionButtonHeight);
        EXPECT_TRUE(Compact(640.0f, 480.0f)) << "a row of 33.6 px on a bar of 60 px";
        EXPECT_TRUE(Compact(640.0f, 440.0f)) << "a bar of 40 px";
        EXPECT_TRUE(Compact(640.0f, 428.0f)) << "a bar of 34 px holds the line of 33.6 px";
        EXPECT_FALSE(Compact(640.0f, 420.0f)) << "a bar of 30 px";
        // A canvas just wide enough for the row in one line (1043.91 units at the floor scale: 626.35 px): 627 x 480 has the bar of 63.7 px for
        // the line of 33.6 px; 626 x 480 is 1043.3 units wide, the row is 120 units (72 px) high and does not fit its bar of 63.9 px
        EXPECT_TRUE(Compact(627.0f, 480.0f));
        EXPECT_FALSE(Compact(626.0f, 480.0f));
        EXPECT_TRUE(Compact(480.0f, 480.0f)) << "800 units wide, a row of 72 px on a bar of 105 px";
        EXPECT_TRUE(Compact(720.0f, 480.0f)) << "1176 units wide: one line of 34.3 px on a bar of 37.5 px";
        // Where the compact scale rises above its floor the row is higher in pixels: 1000 x 700 has a scale of 0.8715 (a line of 48.8 px)
        // and a bar of (700 - 562.5) / 2 = 68.75 px
        EXPECT_NEAR(CompactScale(1000.0f, 700.0f), 0.8715f, 0.0001f);
        EXPECT_TRUE(Compact(1000.0f, 700.0f, 1.0f, 48.0f)) << "a button of 43.75 px is under a minimum of 48";
        EXPECT_FALSE(Compact(1000.0f, 600.0f, 1.0f, 48.0f)) << "the bar of 18.75 px holds no line of 45 px";
    }

    TEST(TouchRegimeTest, AWindowWhoseBarHoldsALineButNotTheRowKeepsActions) {
        // The row reaches as far up as its height: the stack starts its lines at its top, so a window with a bar of one line of 34 px (and
        // more) still gets `Actions` when the canvas is narrower than 1043.91 units and the row is 120 units high
        EXPECT_FALSE(Compact(600.0f, 420.0f)) << "a bar of 41 px, a row of 72 px";
        EXPECT_FALSE(Compact(480.0f, 360.0f)) << "a bar of 45 px";
        EXPECT_FALSE(Compact(500.0f, 400.0f)) << "a bar of 59 px";
        EXPECT_GE(BarUnderTable(500.0f, 400.0f), kActionButtonHeight * CompactScale(500.0f, 400.0f)) << "a line would fit, the row does not";
    }

    TEST(TouchRegimeTest, TheUserScaleMakesTheCanvasSmallerAndTheRowHigherSoItNeedsABiggerBar) {
        // 640 x 480 at the user scale 1.5: the scale is 0.866, the canvas 739 units, the row 120 units = 104 px on a bar of 60 px
        EXPECT_NEAR(CompactScale(640.0f, 480.0f, 1.5f), 0.866f, 0.001f);
        EXPECT_FALSE(Compact(640.0f, 480.0f, kDensity, kMinTouch, 1.5f));
        // 650 x 500 holds the one-line row at 1.0 and not at 1.5 (the canvas is 730 units, two lines, 120 x 0.89 = 107 px on a bar of 67 px)
        EXPECT_TRUE(Compact(650.0f, 500.0f, kDensity, kMinTouch, 1.0f));
        EXPECT_FALSE(Compact(650.0f, 500.0f, kDensity, kMinTouch, 1.5f));
        // 720 x 480 is compact at 1.0 and not at 1.5 (the canvas is 783.8 units wide, the row 120 units = 110 px on a bar of 37.5 px)
        EXPECT_FALSE(Compact(720.0f, 480.0f, kDensity, kMinTouch, 1.5f)) << "783.8 units wide: a row of 110 px on a bar of 37.5 px";
        // The tall and the square windows keep the compact row at both
        EXPECT_TRUE(Compact(480.0f, 480.0f, kDensity, kMinTouch, 1.0f));
        EXPECT_TRUE(Compact(480.0f, 480.0f, kDensity, kMinTouch, 1.5f)) << "640 units wide, 120 x 0.75 = 90 px on a bar of 105 px";
        EXPECT_TRUE(Compact(480.0f, 853.0f, kDensity, kMinTouch, 1.5f));
        // A user scale that is not positive counts as 1, as the engine's scaler does
        EXPECT_FLOAT_EQ(CompactScale(640.0f, 440.0f, 0.0f), CompactScale(640.0f, 440.0f, 1.0f));
        EXPECT_FLOAT_EQ(CompactScale(640.0f, 440.0f, -2.0f), CompactScale(640.0f, 440.0f, 1.0f));
    }

    TEST(TouchRegimeTest, TheRowHeightIsOneLineAboveTheFirstVariantAndTheHeightOfTheNarrowestVariantThatAppliesBelow) {
        EXPECT_FLOAT_EQ(CompactRowHeight(5000.0f), kActionButtonHeight);
        EXPECT_FLOAT_EQ(CompactRowHeight(1280.0f), kActionButtonHeight);
        for (size_t i = 0; i < std::size(kCompactRowVariants); ++i) {
            const CompactRowVariant& variant = kCompactRowVariants[i];
            EXPECT_FLOAT_EQ(CompactRowHeight(variant.maxWidth), variant.height) << i << ": the threshold itself is still the variant (rounded up, never down)";
            EXPECT_FLOAT_EQ(CompactRowHeight(variant.maxWidth - 0.01f), variant.height) << i;
            const float above = i == 0 ? kActionButtonHeight : kCompactRowVariants[i - 1].height;
            EXPECT_FLOAT_EQ(CompactRowHeight(variant.maxWidth + 0.01f), above) << i << ": just above the threshold the wider variant applies";
        }
        // The variants run from the widest to the narrowest and get higher
        for (size_t i = 1; i < std::size(kCompactRowVariants); ++i) {
            EXPECT_LT(kCompactRowVariants[i].maxWidth, kCompactRowVariants[i - 1].maxWidth);
            EXPECT_GT(kCompactRowVariants[i].height, kCompactRowVariants[i - 1].height);
        }
        // The narrower the canvas, the higher the row
        float last = 0.0f;
        for (float width = 1400.0f; width >= 250.0f; width -= 0.25f) {
            const float height = CompactRowHeight(width);
            EXPECT_GE(height, last) << width;
            last = height;
        }
    }

    TEST(TouchRegimeTest, TheRowHeightIsTheLinesTheButtonsNeedInTheLeastSafeAreaOfThatCanvasWidth) {
        // Worked out here from the numbers of the row: k buttons to a line = floor((share x width + spacing) / (button + spacing)), the lines
        // = ceil(count / k), the height = lines x 56 + (lines - 1) x spacing. No width of the loop is a threshold, so the rounding up of the
        // thresholds does not show
        for (float width = 250.0f; width <= 1400.0f; width += 0.25f) {
            const int perLine = static_cast<int>(std::floor((kCompactSafeShare * width + kCompactSpacing) / (kCompactButtonWidth + kCompactSpacing)));
            ASSERT_GE(perLine, 1) << width;
            const int lines = (kCompactButtonCount + perLine - 1) / perLine;
            const float expected = static_cast<float>(lines) * kActionButtonHeight + static_cast<float>(lines - 1) * kCompactSpacing;
            EXPECT_FLOAT_EQ(CompactRowHeight(width), expected) << width << ": " << perLine << " to a line";
        }
    }

    TEST(TouchRegimeTest, TheRowHeightHoldsTheFullestTurnAndTheFullestClaimWindowToo) {
        // The thresholds are counted for the eight buttons of 100 of the claim window (seven and the hint button); the fullest turn (a win,
        // three concealed kongs, the upgrade, the hint button and the declaration, which is wider and comes last) never needs more lines,
        // wrapped in the order of the file
        const std::vector<float> claim(static_cast<size_t>(kCompactButtonCount), kCompactButtonWidth);
        const std::vector<float> turn = {kCompactButtonWidth, kCompactButtonWidth, kCompactButtonWidth, kCompactButtonWidth, kCompactButtonWidth, kCompactButtonWidth, kCompactDeclareWidth};
        ASSERT_LE(turn.size(), claim.size());
        for (float width = 250.0f; width <= 1400.0f; width += 0.25f) {
            const float safe = kCompactSafeShare * width;
            EXPECT_GE(CompactRowHeight(width), WrappedHeight(claim, safe)) << width << ": the claim window";
            EXPECT_GE(CompactRowHeight(width), WrappedHeight(turn, safe)) << width << ": the turn";
        }
        // Every part of the two sets (a window shows the buttons it is offered, and wrapping is not monotone in general): never more lines
        for (const std::vector<float>* set : {&claim, &turn}) {
            for (unsigned mask = 1; mask < (1u << set->size()); ++mask) {
                std::vector<float> part;
                for (size_t i = 0; i < set->size(); ++i) {
                    if ((mask >> i & 1u) != 0u) {
                        part.push_back((*set)[i]);
                    }
                }
                for (float width = 250.0f; width <= 1400.0f; width += 1.0f) {
                    EXPECT_GE(CompactRowHeight(width), WrappedHeight(part, kCompactSafeShare * width)) << width << ": part " << mask << " of a set of " << set->size();
                }
            }
        }
        // And the claim window makes exactly that many lines, so the table is not taller than the buttons need: the widest canvas is one line
        EXPECT_FLOAT_EQ(WrappedHeight(claim, kCompactSafeShare * 1090.0f), kActionButtonHeight);
        EXPECT_FLOAT_EQ(CompactRowHeight(1090.0f), kActionButtonHeight);
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
        EXPECT_TRUE(Compact(480.0f, 480.0f, kDensity, 32.0f));
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

    TEST(TouchRegimeTest, TheWholeMatrixIsCompactExactlyWhere56TimesTheFitIsBelow32AtTheUserScale1) {
        // Every window of the matrix is 480 px or more on its short side. Each one below the touch size has a bar that holds the row at
        // its height at the user scale 1.0: one line (the canvas is 1043.91 units wide or more, e.g. 640 x 480 and 720 x 480) or several
        // lines on the tall bars of the square and portrait windows (480 x 480: two lines, 72 px on a bar of 105 px). The user scale 1.5
        // makes the canvas narrower and keeps `Actions` in 640 x 480 and 720 x 480, see `TheUserScaleMakes...`
        int compact = 0;
        int roomy = 0;
        for (const BECore::Tests::UIMatrixCase& matrixCase : BECore::Tests::UILayoutMatrix()) {
            const float width = matrixCase.context.outputWidth;
            const float height = matrixCase.context.outputHeight;
            const float fit = std::min(width / kViewWidth, height / kViewHeight);  // worked out here, not by FitScale
            const bool small = 56.0f * fit < kMinTouch;
            EXPECT_EQ(Compact(width, height), small) << matrixCase.label.c_str();
            if (small) {
                ++compact;
            } else {
                ++roomy;
            }
        }
        // Both kinds of window are in the matrix: some 480 px ones are compact (480 x 480), the full HD ones are not
        EXPECT_GT(compact, 0);
        EXPECT_GT(roomy, 0);
        EXPECT_TRUE(Compact(480.0f, 480.0f));
        EXPECT_FALSE(Compact(1920.0f, 1080.0f));
    }

    // The profile of the table: the hand tile of the desktop layout, or a phone profile with a larger one.

    namespace {

        /// The profile a window of `width` x `height` window units at `density` gets: the output is that many pixels per unit larger.
        TableProfile ProfileOfWindow(float width, float height, float density = kDensity) {
            return TableProfileFor(width * density, height * density, density, kMinTouch);
        }

        /// The hand tile of a profile in window units, worked out here.
        float HandTileInWindowUnits(const TableProfile& profile, float width, float height, float density) {
            return profile.handTileW * std::min(width * density / kViewWidth, height * density / kViewHeight) / density;
        }

    }  // namespace

    TEST(TouchRegimeTest, ADesktopWindowKeepsTheDesktopProfile) {
        for (const float density : {1.0f, 2.0f}) {
            EXPECT_FALSE(ProfileOfWindow(1280.0f, 720.0f, density).phone) << density;
            EXPECT_FALSE(ProfileOfWindow(1920.0f, 1080.0f, density).phone) << density;
        }
        EXPECT_EQ(ProfileOfWindow(1280.0f, 720.0f), TableProfile{});
        EXPECT_EQ(ProfileOfWindow(1920.0f, 1080.0f), TableProfile{});
        EXPECT_EQ(ProfileOfWindow(1280.0f, 720.0f).handTileW, kViewerHandTileW);
    }

    TEST(TouchRegimeTest, ALandscapePhoneGetsTheLargeHandWithATileOfAtLeastTheTouchMinimum) {
        // the window units of the phones of the screenshots (915 x 330, 780 x 300, 851 x 393) at the density 2.6 of those phones
        const float windows[][2] = {{915.0f, 330.0f}, {780.0f, 300.0f}, {851.0f, 393.0f}};
        for (const auto& window : windows) {
            const TableProfile profile = ProfileOfWindow(window[0], window[1], 2.6f);
            EXPECT_TRUE(profile.phone) << window[0] << " x " << window[1];
            EXPECT_GE(HandTileInWindowUnits(profile, window[0], window[1], 2.6f), kMinTouch - 0.01f) << window[0] << " x " << window[1];
            EXPECT_GE(profile.handTileW, kViewerHandTileW);
            EXPECT_LE(profile.handTileW, kPhoneHandTileMaxW);
        }
        // a tile of exactly the minimum where the maximum is not needed
        const TableProfile wide = ProfileOfWindow(915.0f, 330.0f, 2.6f);
        EXPECT_LT(wide.handTileW, kPhoneHandTileMaxW);
        EXPECT_NEAR(HandTileInWindowUnits(wide, 915.0f, 330.0f, 2.6f), kMinTouch, 0.01f);
        EXPECT_NEAR(ProfileOfWindow(780.0f, 300.0f, 2.6f).handTileW, 76.8f, 0.01f);
    }

    TEST(TouchRegimeTest, AWindowTooSmallToReachTheMinimumGetsTheWidestTileTheViewHolds) {
        for (const float density : {1.0f, 2.6f}) {
            const TableProfile profile = ProfileOfWindow(400.0f, 150.0f, density);
            EXPECT_TRUE(profile.phone);
            EXPECT_EQ(profile.handTileW, kPhoneHandTileMaxW);
            EXPECT_LT(HandTileInWindowUnits(profile, 400.0f, 150.0f, density), kMinTouch);
        }
    }

    TEST(TouchRegimeTest, TheProfileChangesWhereTheDesktopHandTileDropsUnderTheMinimum) {
        // 48 units x fit 0.6667 = 32 at 854 x 480 (the height limits); a pixel less in either direction is phone
        EXPECT_FALSE(ProfileOfWindow(854.0f, 480.0f).phone);
        EXPECT_TRUE(ProfileOfWindow(853.0f, 480.0f).phone) << "width limited: 31.99";
        EXPECT_TRUE(ProfileOfWindow(854.0f, 479.0f).phone);
        EXPECT_FALSE(ProfileOfWindow(2000.0f, 480.0f).phone);
        EXPECT_FALSE(ProfileOfWindow(1280.0f, 720.0f).phone);
        // just under the boundary the tile is only a little wider than the desktop one
        EXPECT_NEAR(ProfileOfWindow(853.0f, 480.0f).handTileW, kViewerHandTileW, 0.1f);
        // the density moves the boundary: at 2.6 the window units are 2.6 times smaller than the pixels
        EXPECT_TRUE(TableProfileFor(1280.0f, 720.0f, 2.0f, kMinTouch).phone) << "1280 x 720 pixels at 2 are a 640 x 360 window";
    }

    TEST(TouchRegimeTest, ASizeADensityOrAMinimumThatIsNotPositiveGetsTheDesktopProfile) {
        EXPECT_EQ(TableProfileFor(0.0f, 330.0f, 1.0f, kMinTouch), TableProfile{});
        EXPECT_EQ(TableProfileFor(915.0f, -1.0f, 1.0f, kMinTouch), TableProfile{});
        EXPECT_EQ(TableProfileFor(std::nanf(""), 330.0f, 1.0f, kMinTouch), TableProfile{});
        EXPECT_EQ(TableProfileFor(915.0f, 330.0f, 0.0f, kMinTouch), TableProfile{});
        EXPECT_EQ(TableProfileFor(915.0f, 330.0f, -2.0f, kMinTouch), TableProfile{});
        EXPECT_EQ(TableProfileFor(915.0f, 330.0f, std::nanf(""), kMinTouch), TableProfile{});
        EXPECT_EQ(TableProfileFor(915.0f, 330.0f, 1.0f, 0.0f), TableProfile{});
        EXPECT_EQ(TableProfileFor(915.0f, 330.0f, 1.0f, std::nanf("")), TableProfile{});
        EXPECT_TRUE(TableProfileFor(915.0f, 330.0f, 1.0f, kMinTouch).phone) << "the same window with a size that counts";
    }

    TEST(TouchRegimeTest, ThePortraitViewFitsIntoAnOutputByTheSmallerOfTheTwoScales) {
        // worked out here: output pixels over the 720 x 1280 view
        const struct {
            float width;
            float height;
            float fit;
        } outputs[] = {
            {1170.0f, 2532.0f, 1170.0f / 720.0f},  // 390 x 844 at density 3: width limited
            {720.0f, 1280.0f, 1.0f},               // 360 x 640 at density 2
            {768.0f, 1024.0f, 1024.0f / 1280.0f},  // 3:4: height limited
            {480.0f, 480.0f, 480.0f / 1280.0f},    // a square: height limited
        };
        for (const auto& output : outputs) {
            EXPECT_FLOAT_EQ(FitScale(output.width, output.height, kPortraitViewWidth, kPortraitViewHeight), output.fit) << output.width << " x " << output.height;
        }
        EXPECT_EQ(FitScale(720.0f, 0.0f, kPortraitViewWidth, kPortraitViewHeight), 0.0f);
        EXPECT_EQ(FitScale(720.0f, 1280.0f, 0.0f, kPortraitViewHeight), 0.0f) << "a view of no size";
        EXPECT_EQ(FitScale(720.0f, 1280.0f, kPortraitViewWidth, -1.0f), 0.0f);
        EXPECT_EQ(FitScale(1280.0f, 720.0f), FitScale(1280.0f, 720.0f, kViewWidth, kViewHeight)) << "the default view is the landscape one";
    }

    TEST(TouchRegimeTest, TheBarUnderThePortraitTableIsWhatTheViewLeavesOfTheHeight) {
        EXPECT_FLOAT_EQ(BarUnderTable(720.0f, 1280.0f, kPortraitViewWidth, kPortraitViewHeight), 0.0f);
        EXPECT_FLOAT_EQ(BarUnderTable(360.0f, 800.0f, kPortraitViewWidth, kPortraitViewHeight), (800.0f - 640.0f) / 2.0f) << "the table is 640 high at fit 0.5";
        EXPECT_FLOAT_EQ(BarUnderTable(768.0f, 1024.0f, kPortraitViewWidth, kPortraitViewHeight), 0.0f) << "height limited: the bars are at the sides";
        EXPECT_FLOAT_EQ(BarUnderTable(1280.0f, 800.0f), 40.0f) << "the landscape default";
    }

    TEST(TouchRegimeTest, AnActionButtonIs56UnitsInLandscapeAnd68InPortrait) {
        EXPECT_EQ(ActionButtonHeightFor(TableShape::Landscape), kActionButtonHeight);
        EXPECT_EQ(ActionButtonHeightFor(TableShape::Portrait), kPortraitActionButtonHeight);
        EXPECT_EQ(ActionButtonHeightFor(TableShape::Wide), kActionButtonHeight);
        EXPECT_EQ(kPortraitActionButtonHeight, 68.0f);
        EXPECT_EQ(std::fmod(kPortraitActionButtonHeight, 4.0f), 0.0f) << "a grid unit of 4";
    }

    TEST(TouchRegimeTest, APortraitPhoneReachesTheTouchMinimumWithAButtonOf68UnitsSoNoCompactRow) {
        // 390 x 844 at density 3: the fit is 1.625 over 3, a button of 68 x 0.5417 = 36.8 window units
        EXPECT_FALSE(NeedsCompactActions(1170.0f, 2532.0f, 3.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_GE(68.0f * (1170.0f / 720.0f) / 3.0f, kMinTouch);
        // 360 x 640 at density 2: a button of 34 window units
        EXPECT_FALSE(NeedsCompactActions(720.0f, 1280.0f, 2.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_GE(68.0f * 1.0f / 2.0f, kMinTouch);
        // 768 x 1024: a button of 54 units
        EXPECT_FALSE(NeedsCompactActions(768.0f, 1024.0f, 1.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        // the landscape rule would call the 390 x 844 phone compact: the button of 56 units is 30.4 window units
        EXPECT_TRUE(NeedsCompactActions(1170.0f, 2532.0f, 3.0f, kMinTouch));
    }

    TEST(TouchRegimeTest, APortraitWindowWhoseButtonIsSmallIsCompactOnlyWhereABarHoldsTheRow) {
        // 300 x 700 at density 1: fit 0.4167, a button of 28.3 units; the table is 533 high, the bar is 83.3 px; the canvas is 500 units
        // wide at the floor scale 0.6, the row 184 units (110 px): it does not fit the bar
        EXPECT_FALSE(NeedsCompactActions(300.0f, 700.0f, 1.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        // 480 x 480: the table fills the height (fit 0.375, a button of 25.5), no bar
        EXPECT_FALSE(NeedsCompactActions(480.0f, 480.0f, 1.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        // a tall window with a bar that holds the row: 640 x 1600 has fit 0.889 (a button of 60), so no; at a minimum of 64 it is compact:
        // the bar is (1600 - 1137.8) / 2 = 231 px, the canvas 1066.7 units, a line of 33.6 px
        EXPECT_FALSE(NeedsCompactActions(640.0f, 1600.0f, 1.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_TRUE(NeedsCompactActions(640.0f, 1600.0f, 1.0f, 64.0f, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_FALSE(NeedsCompactActions(0.0f, 1280.0f, 1.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
        EXPECT_FALSE(NeedsCompactActions(720.0f, 1280.0f, 0.0f, kMinTouch, 1.0f, kPortraitViewWidth, kPortraitViewHeight));
    }

    TEST(TouchRegimeTest, APortraitViewGetsTheDesktopProfileInAnyWindow) {
        const struct {
            float width;
            float height;
            float density;
        } windows[] = {{1170.0f, 2532.0f, 3.0f}, {720.0f, 1280.0f, 2.0f}, {768.0f, 1024.0f, 1.0f}, {480.0f, 480.0f, 1.0f}, {200.0f, 400.0f, 1.0f}};
        for (const auto& window : windows) {
            EXPECT_EQ(TableProfileFor(window.width, window.height, window.density, kMinTouch, kPortraitViewWidth, kPortraitViewHeight), TableProfile{}) << window.width << " x " << window.height;
        }
        // the same pixels with the landscape view default are a phone: the portrait view is what changes the answer
        EXPECT_TRUE(TableProfileFor(720.0f, 1280.0f, 2.0f, kMinTouch).phone);
    }

    // The wide view: the landscape table with a wing on each side, so the rules are the landscape ones over the wider view.

    TEST(TouchRegimeTest, TheWideViewFitsIntoAnOutputByTheSmallerOfTheTwoScalesAndLeavesNoBarUnderTheTableOfA21To9Window) {
        EXPECT_FLOAT_EQ(FitScale(1560.0f, 720.0f, kWideViewWidth, kViewHeight), 1.0f);
        EXPECT_FLOAT_EQ(FitScale(2560.0f, 1080.0f, kWideViewWidth, kViewHeight), 1.5f) << "21:9 at 2560 x 1080: the height limits";
        EXPECT_FLOAT_EQ(FitScale(915.0f, 330.0f, kWideViewWidth, kViewHeight), 330.0f / 720.0f) << "a landscape phone is wider than 19.5:9: the height limits";
        EXPECT_FLOAT_EQ(FitScale(2000.0f, 1000.0f, kWideViewWidth, kViewHeight), 2000.0f / 1560.0f) << "narrower than 19.5:9: the width of the wide view limits";
        EXPECT_FLOAT_EQ(BarUnderTable(2560.0f, 1080.0f, kWideViewWidth, kViewHeight), 0.0f);
        EXPECT_FLOAT_EQ(BarUnderTable(3440.0f, 1440.0f, kWideViewWidth, kViewHeight), 0.0f);
        EXPECT_FLOAT_EQ(BarUnderTable(1560.0f, 800.0f, kWideViewWidth, kViewHeight), 40.0f);
    }

    TEST(TouchRegimeTest, AWideWindowOfTheLandscapePhonesGetsThePhoneProfileWhereTheLandscapeRuleDoesWithinThreePercentOfTheFit) {
        const struct {
            float width;
            float height;
        } phones[] = {{915.0f, 330.0f}, {851.0f, 393.0f}, {780.0f, 300.0f}};
        for (const auto& phone : phones) {
            const float density = 2.6f;
            const float width = phone.width * density;
            const float height = phone.height * density;
            const TableProfile wide = TableProfileFor(width, height, density, kMinTouch, kWideViewWidth, kViewHeight);
            const TableProfile landscape = TableProfileFor(width, height, density, kMinTouch);
            EXPECT_TRUE(wide.phone) << phone.width << " x " << phone.height;
            EXPECT_TRUE(landscape.phone);
            EXPECT_NEAR(FitScale(width, height, kWideViewWidth, kViewHeight), FitScale(width, height), 0.03f * FitScale(width, height)) << phone.width << " x " << phone.height;
            EXPECT_NEAR(wide.handTileW, landscape.handTileW, 0.03f * landscape.handTileW) << phone.width << " x " << phone.height;
            EXPECT_GE(wide.handTileW, kViewerHandTileW);
            EXPECT_LE(wide.handTileW, kPhoneHandTileMaxW);
        }
    }

    TEST(TouchRegimeTest, AWideDesktopWindowKeepsTheDesktopProfile) {
        EXPECT_EQ(TableProfileFor(2560.0f, 1080.0f, 1.0f, kMinTouch, kWideViewWidth, kViewHeight), TableProfile{});
        EXPECT_EQ(TableProfileFor(3440.0f, 1440.0f, 1.0f, kMinTouch, kWideViewWidth, kViewHeight), TableProfile{});
        EXPECT_EQ(TableProfileFor(0.0f, 1080.0f, 1.0f, kMinTouch, kWideViewWidth, kViewHeight), TableProfile{});
    }

    TEST(TouchRegimeTest, TheCompactRuleOfAWideWindowIsTheLandscapeOneOverTheWiderView) {
        // 2560 x 1080 at density 1: the table fills the height (fit 1.5, a button of 84 units): no compact row
        EXPECT_FALSE(NeedsCompactActions(2560.0f, 1080.0f, 1.0f, kMinTouch, 1.0f, kWideViewWidth, kViewHeight));
        // a 21:9 window whose button is small has no bar under the table, so the row would lie on the hand: Actions stays
        EXPECT_FALSE(NeedsCompactActions(840.0f, 360.0f, 1.0f, kMinTouch, 1.0f, kWideViewWidth, kViewHeight));
        EXPECT_FLOAT_EQ(BarUnderTable(840.0f, 360.0f, kWideViewWidth, kViewHeight), 0.0f);
        // a window barely wide enough for the wide view with a bar: 1560 x 900 at fit 1 has bars of 90 px, a button of 56 units
        EXPECT_FALSE(NeedsCompactActions(1560.0f, 900.0f, 1.0f, kMinTouch, 1.0f, kWideViewWidth, kViewHeight));
        EXPECT_TRUE(NeedsCompactActions(1560.0f, 900.0f, 1.0f, 64.0f, 1.0f, kWideViewWidth, kViewHeight));
    }

}  // namespace BECore::Mahjong::Tests
