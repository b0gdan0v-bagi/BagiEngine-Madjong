#include "MatchTestSupport.h"

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <gtest/gtest.h>

/**
 * `ComputerLevel` (the names, the parser, the default) and the golden of the Normal player: the final scores of four whole matches,
 * written down from the player as it was before levels existed, so that "Normal is the player of `CLAUDE.md`" stays true for as long as
 * the test stays green. Every level plays like Normal until its profile exists. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kFirstGoldenSeed = 700;
        constexpr int kGoldenMatchCount = 4;
        constexpr int kWholeHands = 6;

        /// The final scores of the 16-hand match (`SettingsOf(0)`, flowers off) of each seed 700..703 played by four computers, taken
        /// from the unmodified tree before the level was added.
        constexpr SeatScores kGoldenScores[kGoldenMatchCount] = {
            {2780, 2550, 1910, 760},
            {2090, 1420, 2600, 1890},
            {1480, 2560, 2060, 1900},
            {1780, 2130, 1670, 2420},
        };

        /// Every action of `kWholeHands` hands played by a table, in order.
        std::vector<HandAction> PlayActions(ComputerTable& table, uint32_t seed) {
            std::vector<HandAction> actions;
            Match match;
            EXPECT_TRUE(match.Start(SettingsOf(0), seed).has_value());
            for (int hand = 0; hand < kWholeHands && match.GetPhase() == MatchPhase::InHand; ++hand) {
                while (match.Hand().GetPhase() != Phase::Finished) {
                    const StepResult step = table.Step(match.Hand());
                    EXPECT_TRUE(step.has_value());
                    if (!step) {
                        return actions;
                    }
                    actions.push_back(*step);
                }
                EXPECT_TRUE(match.EndHand().has_value());
            }
            return actions;
        }

    }  // namespace

    TEST(ComputerLevelTest, TheNamesRoundTripForEveryLevel) {
        EXPECT_EQ(EnumUtils<ComputerLevel>::Count(), static_cast<size_t>(kLevelCount));
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            const std::string_view name = LevelName(level);
            EXPECT_FALSE(name.empty());
            EXPECT_EQ(TryParseLevel(name), level) << name;
        }
        EXPECT_EQ(LevelName(ComputerLevel::Easy), "easy");
        EXPECT_EQ(LevelName(ComputerLevel::Normal), "normal");
        EXPECT_EQ(LevelName(ComputerLevel::Hard), "hard");
    }

    TEST(ComputerLevelTest, ParsingTakesTheLowercaseNamesOnly) {
        for (const char* text : {"Easy", "EASY", "", "hard ", " hard", "medium", "Normal", "norma", "normall", "0", "easy\n"}) {
            EXPECT_FALSE(TryParseLevel(text).has_value()) << "'" << text << "'";
        }
    }

    TEST(ComputerLevelTest, TheDefaultIsNormal) {
        EXPECT_EQ(kDefaultLevel, ComputerLevel::Normal);
        EXPECT_EQ(ComputerPlayer(1).Level(), ComputerLevel::Normal);
        EXPECT_EQ(ComputerTable(1).Level(), ComputerLevel::Normal);
    }

    TEST(ComputerLevelTest, TheTableGivesEverySeatItsLevelAndTheSeedsDoNotDependOnIt) {
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            EXPECT_EQ(ComputerTable(5, level).Level(), level);
            EXPECT_EQ(ComputerPlayer(5, level).Level(), level);
        }
        // The same table seed gives the same walls and tie-breaks at every level: the actions of a level that has no profile yet are
        // those of Normal (this line goes when the first profile does something).
        ComputerTable normal(31);
        const std::vector<HandAction> expected = PlayActions(normal, 31);
        ASSERT_FALSE(expected.empty());
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            ComputerTable table(31, level);
            EXPECT_EQ(PlayActions(table, 31), expected) << LevelName(level);
        }
    }

    TEST(ComputerLevelTest, ExplicitNormalPlaysLikeTheDefaultOnSixWholeHands) {
        for (uint32_t seed = 900; seed < 903; ++seed) {
            ComputerTable byDefault(seed);
            ComputerTable explicitNormal(seed, ComputerLevel::Normal);
            const std::vector<HandAction> expected = PlayActions(byDefault, seed);
            ASSERT_FALSE(expected.empty()) << "seed " << seed;
            EXPECT_EQ(PlayActions(explicitNormal, seed), expected) << "seed " << seed;
        }
    }

    TEST(ComputerLevelTest, NormalPlaysTheMatchesItPlayedBeforeLevelsExisted) {
        for (int index = 0; index < kGoldenMatchCount; ++index) {
            const uint32_t seed = kFirstGoldenSeed + static_cast<uint32_t>(index);
            Match match;
            ASSERT_TRUE(match.Start(SettingsOf(0), seed).has_value());
            ComputerTable table(seed, ComputerLevel::Normal);
            ASSERT_TRUE(table.PlayMatch(match).has_value());
            EXPECT_EQ(match.Scores(), kGoldenScores[index]) << "seed " << seed;
        }
    }

}  // namespace BECore::Mahjong::Tests
