#include <Mahjong/Table/TableLook.h>
#include <Mahjong/Table/UiPalette.h>
#include <gtest/gtest.h>

/**
 * The felt of the table (`Table/TableLook.h`): pure pixels, so no engine services are needed.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kWidth = 64;
        constexpr uint32_t kHeight = 36;

        /// The channel `channel` (0 red .. 3 alpha) of the pixel (x, y).
        int Pixel(const FeltPixels& felt, uint32_t x, uint32_t y, size_t channel) {
            return felt.rgba[(static_cast<size_t>(y) * felt.width + x) * 4 + channel];
        }

        int Brightness(const FeltPixels& felt, uint32_t x, uint32_t y) {
            return Pixel(felt, x, y, 0) + Pixel(felt, x, y, 1) + Pixel(felt, x, y, 2);
        }

        /// The brightness of the `side` x `side` pixels round (x, y): the grain averages out.
        int Patch(const FeltPixels& felt, uint32_t x, uint32_t y, uint32_t side) {
            int sum = 0;
            for (uint32_t dy = 0; dy < side; ++dy) {
                for (uint32_t dx = 0; dx < side; ++dx) {
                    sum += Brightness(felt, x + dx, y + dy);
                }
            }
            return sum;
        }

    }  // namespace

    TEST(TableLookTest, TheImageHasTheSizeAskedForAndEveryPixelIsOpaque) {
        const FeltPixels felt = MakeFeltPixels(kWidth, kHeight);

        EXPECT_EQ(felt.width, kWidth);
        EXPECT_EQ(felt.height, kHeight);
        ASSERT_EQ(felt.rgba.size(), static_cast<size_t>(kWidth) * kHeight * 4);
        for (size_t i = 3; i < felt.rgba.size(); i += 4) {
            ASSERT_EQ(felt.rgba[i], 255) << "pixel " << i / 4;
        }
    }

    TEST(TableLookTest, TheSameSizeGivesTheSameBytes) {
        EXPECT_EQ(MakeFeltPixels(kWidth, kHeight).rgba, MakeFeltPixels(kWidth, kHeight).rgba);
    }

    TEST(TableLookTest, TheImageIsSymmetricLeftToRightAndTopToBottom) {
        const FeltPixels felt = MakeFeltPixels(kWidth, kHeight);

        for (uint32_t y = 0; y < kHeight; ++y) {
            for (uint32_t x = 0; x < kWidth; ++x) {
                for (size_t channel = 0; channel < 4; ++channel) {
                    ASSERT_EQ(Pixel(felt, x, y, channel), Pixel(felt, kWidth - 1 - x, y, channel)) << x << "," << y;
                    ASSERT_EQ(Pixel(felt, x, y, channel), Pixel(felt, x, kHeight - 1 - y, channel)) << x << "," << y;
                }
            }
        }
    }

    TEST(TableLookTest, TheMiddleIsBrighterThanTheCornersAndTheEdgesAreInBetween) {
        const FeltPixels felt = MakeFeltPixels(kWidth, kHeight);
        constexpr uint32_t kSide = 4;

        const int middle = Patch(felt, kWidth / 2 - kSide / 2, kHeight / 2 - kSide / 2, kSide);
        const int corner = Patch(felt, 0, 0, kSide);
        const int sideMiddle = Patch(felt, 0, kHeight / 2 - kSide / 2, kSide);
        const int topMiddle = Patch(felt, kWidth / 2 - kSide / 2, 0, kSide);
        EXPECT_GT(middle, sideMiddle);
        EXPECT_GT(middle, topMiddle);
        EXPECT_GT(sideMiddle, corner);
        EXPECT_GT(topMiddle, corner);
    }

    TEST(TableLookTest, TheColourStaysGreenFeltWithAGrainNoStrongerThanTheLevelsSaid) {
        const FeltPixels felt = MakeFeltPixels(kWidth, kHeight);

        bool grain = false;
        for (uint32_t y = 0; y < kHeight; ++y) {
            for (uint32_t x = 1; x < kWidth / 2; ++x) {
                EXPECT_GT(Pixel(felt, x, y, 1), Pixel(felt, x, y, 0)) << "green over red";
                EXPECT_GT(Pixel(felt, x, y, 1), Pixel(felt, x, y, 2)) << "green over blue";
                grain = grain || Pixel(felt, x, y, 0) != Pixel(felt, x - 1, y, 0);
            }
        }
        EXPECT_TRUE(grain) << "the felt is not a flat colour";
        // The vignette is a whole step darker than the grain
        EXPECT_LT(Pixel(MakeFeltPixels(kWidth, kHeight), 0, 0, 1), static_cast<int>(kFelt.g));
    }

    TEST(TableLookTest, ASizeOfZeroGivesNoPixels) {
        EXPECT_TRUE(MakeFeltPixels(0, kHeight).rgba.empty());
        EXPECT_TRUE(MakeFeltPixels(kWidth, 0).rgba.empty());
        EXPECT_TRUE(MakeFeltPixels(0, 0).rgba.empty());
    }

    TEST(TableLookTest, ASingleRowOrColumnIsMadeWithoutError) {
        EXPECT_EQ(MakeFeltPixels(1, 1).rgba.size(), 4u);
        EXPECT_EQ(MakeFeltPixels(kWidth, 1).rgba.size(), static_cast<size_t>(kWidth) * 4);
    }

}  // namespace BECore::Mahjong::Tests
