#include <Mahjong/BuildInfo.h>
#include <gtest/gtest.h>

namespace BECore::Mahjong::Tests {

    namespace {

        StringView View(const String& text) {
            return StringView(text.c_str(), text.size());
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
        const StringView id = BuildId();
        ASSERT_FALSE(id.empty());
        if (id != "unknown") {
            EXPECT_GE(id.size(), 4u);
            EXPECT_EQ(id.find_first_not_of("0123456789abcdef"), StringView::npos) << ToStd(id);
        }
    }

}  // namespace BECore::Mahjong::Tests
