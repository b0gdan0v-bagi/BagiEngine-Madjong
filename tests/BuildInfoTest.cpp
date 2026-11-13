#include <Mahjong/BuildInfo.h>
#include <gtest/gtest.h>

/**
 * The origin of a match (`BuildInfo.h`): the platform by the build flag, the version of the game and the id of the build, which is
 * the short git hash of the configure or `unknown`. Pure: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        std::string_view View(const eastl::string& text) {
            return std::string_view(text.c_str(), text.size());
        }

    }  // namespace

    TEST(BuildInfoTest, TheBrowserBuildIsWebAndTheOtherIsDesktop) {
        EXPECT_EQ(MakeMatchOrigin(true).platform, MatchPlatform::Web);
        EXPECT_EQ(MakeMatchOrigin(false).platform, MatchPlatform::Desktop);
    }

    TEST(BuildInfoTest, BothBuildsCarryTheVersionAndTheBuildId) {
        for (const bool browser : {false, true}) {
            const MatchLogOrigin origin = MakeMatchOrigin(browser);
            EXPECT_EQ(View(origin.gameVersion), kGameVersion);
            EXPECT_EQ(View(origin.build), BuildId());
        }
        EXPECT_FALSE(kGameVersion.empty());
    }

    TEST(BuildInfoTest, TheNicknameIsInTheOriginOnlyWhenGiven) {
        for (const bool browser : {false, true}) {
            EXPECT_TRUE(MakeMatchOrigin(browser).nickname.empty());
        }
        const MatchLogOrigin named = MakeMatchOrigin(true, "Anna");
        EXPECT_EQ(named.nickname, "Anna");
        EXPECT_EQ(named.platform, MatchPlatform::Web);
        EXPECT_EQ(View(named.build), BuildId());
        EXPECT_EQ(View(named.gameVersion), kGameVersion);
        EXPECT_TRUE(MakeMatchOrigin(true, "").nickname.empty());
        EXPECT_FALSE(MakeMatchOrigin(true, "Anna") == MakeMatchOrigin(true));
    }

    TEST(BuildInfoTest, TheBuildIdIsAShortHashOrUnknown) {
        const std::string_view id = BuildId();
        ASSERT_FALSE(id.empty());
        if (id != "unknown") {
            EXPECT_GE(id.size(), 4u);
            EXPECT_EQ(id.find_first_not_of("0123456789abcdef"), std::string_view::npos) << id;
        }
    }

}  // namespace BECore::Mahjong::Tests
