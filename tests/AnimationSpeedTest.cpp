#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/Save/MatchSave.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/TileFlights.h>
#include <Mahjong/Table/UiPalette.h>
#include <gtest/gtest.h>

/**
 * The speed of the table (`Table/AnimationSpeed.h`): the factors and the scaled times, the ids, and the speed in the saved choices
 * (`MahjongPrefsSave`). The pace on the table itself is `MahjongTableSpeedTest`.
 */
namespace BECore::Mahjong::Tests {

    // A flight stays inside the pause of the computers at every speed: the factor scales both
    static_assert(kTileFlightSeconds < kComputerStepSeconds);

    TEST(AnimationSpeedTest, NormalIsTheTableAsItWasAndSlowIsLongerAndFasterIsShorter) {
        EXPECT_EQ(kDefaultAnimationSpeed, AnimationSpeed::Normal);
        EXPECT_FLOAT_EQ(SpeedFactor(AnimationSpeed::Normal), 1.0f);
        EXPECT_GT(SpeedFactor(AnimationSpeed::Slow), 1.0f);
        EXPECT_LT(SpeedFactor(AnimationSpeed::Fast), 1.0f);
        EXPECT_GT(SpeedFactor(AnimationSpeed::Fast), 0.0f);
        for (const float base : {kComputerStepSeconds, kTileFlightSeconds, kClaimHighlightSeconds, kClaimBlinkSeconds, kClaimLostSeconds}) {
            EXPECT_FLOAT_EQ(ScaledSeconds(base, AnimationSpeed::Normal), base);
            EXPECT_GT(ScaledSeconds(base, AnimationSpeed::Slow), base);
            EXPECT_LT(ScaledSeconds(base, AnimationSpeed::Fast), base);
        }
    }

    TEST(AnimationSpeedTest, EverySpeedIsScaledByItsOwnFactorAndTheStepsAreInOrderSlowestFirst) {
        float previous = 1.0e9f;
        for (const AnimationSpeed speed : kAllAnimationSpeeds) {
            EXPECT_FLOAT_EQ(ScaledSeconds(2.0f, speed), 2.0f * SpeedFactor(speed));
            EXPECT_LT(SpeedFactor(speed), previous) << "the speeds go from the slowest to the fastest";
            previous = SpeedFactor(speed);
            EXPECT_LT(ScaledSeconds(kTileFlightSeconds, speed), ScaledSeconds(kComputerStepSeconds, speed)) << SpeedText(speed);
        }
    }

    TEST(AnimationSpeedTest, TheIdsRoundTripAndAnyOtherTextIsNoSpeed) {
        for (const AnimationSpeed speed : kAllAnimationSpeeds) {
            const auto parsed = TryParseSpeed(SpeedText(speed));
            ASSERT_TRUE(parsed.has_value()) << SpeedText(speed);
            EXPECT_EQ(*parsed, speed);
        }
        for (const char* text : {"", "Slow", "NORMAL", "fast ", "1", "warp"}) {
            EXPECT_FALSE(TryParseSpeed(text).has_value()) << '"' << text << '"';
        }
    }

    TEST(AnimationSpeedTest, ThePrefsHoldTheSpeedAndACopyWithoutItReadsAsNormal) {
        for (const AnimationSpeed speed : kAllAnimationSpeeds) {
            const MahjongPrefsSave prefs = MakePrefs("en", true, kDefaultSeatSetup, kFullMatchHands, kDefaultLevel, speed);
            EXPECT_EQ(prefs.speed, eastl::string(SpeedText(speed).data(), SpeedText(speed).size()));
            const auto read = ReadPrefs(prefs);
            ASSERT_TRUE(read.has_value()) << (read ? "" : read.error().c_str());
            EXPECT_EQ(read->speed, speed);
        }
        // A file written before the setting has no such field: the field keeps its default, the speed is Normal
        const MahjongPrefsSave older;
        EXPECT_EQ(older.speed, "normal");
        ASSERT_TRUE(ReadPrefs(older).has_value());
        EXPECT_EQ(ReadPrefs(older)->speed, AnimationSpeed::Normal);
        EXPECT_TRUE(older == MakePrefs("en", true, kDefaultSeatSetup, kFullMatchHands, kDefaultLevel));
        EXPECT_FALSE(older == MakePrefs("en", true, kDefaultSeatSetup, kFullMatchHands, kDefaultLevel, AnimationSpeed::Fast));

        MahjongPrefsSave broken;
        broken.speed = "warp";
        const auto read = ReadPrefs(broken);
        ASSERT_FALSE(read.has_value());
        EXPECT_NE(std::string(read.error().c_str()).find("speed"), std::string::npos);
    }

}  // namespace BECore::Mahjong::Tests
