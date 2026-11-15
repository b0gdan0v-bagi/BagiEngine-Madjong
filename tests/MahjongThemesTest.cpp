#include <BECore/UI/UIThemes.h>
#include <Common/Printers.h>
#include <Common/UIThemeTestSupport.h>
#include <Mahjong/Table/UiPalette.h>
#include <gtest/gtest.h>
#include <tests/ShippedThemes.h>
#include <tests/UiStyleTestSupport.h>

// The shipped `config/UIThemes.xml`: whole and legal, with a token for every role of `UiPalette.h`.
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kClassic = "classic";

        Set<String> RoleNames() {
            Set<String> roles;
            for (const char* name : kUiPaletteNames) {
                roles.insert(String(name));
            }
            for (const char* name : kTablePaletteNames) {
                roles.insert(String(name));
            }
            return roles;
        }

        class MahjongThemesTest : public ::testing::Test {
        protected:
            void SetUp() override {
                problems = LoadShippedThemes(themes);
            }

            UIThemes themes;
            Vector<String> problems;
        };

    }  // namespace

    TEST_F(MahjongThemesTest, TheShippedFileLoadsWithNoProblem) {
        for (const String& problem : problems) {
            ADD_FAILURE() << problem.c_str();
        }
        EXPECT_FALSE(themes.GetThemes().empty());
    }

    TEST_F(MahjongThemesTest, TheDefaultThemeIsClassic) {
        EXPECT_EQ(themes.GetDefaultTheme(), PoolString::Intern(kClassic));
        EXPECT_EQ(themes.GetTheme(), PoolString::Intern(kClassic));
        EXPECT_TRUE(themes.HasTheme(PoolString::Intern(kClassic)));
    }

    TEST_F(MahjongThemesTest, TheTokensAreTheRolesOfThePaletteAndOfTheTableExactly) {
        const Set<String> roles = RoleNames();
        EXPECT_EQ(roles.size(), static_cast<size_t>(kUiPaletteSize + kTablePaletteSize)) << "a role is named twice";

        Set<String> tokens;
        for (const PoolString token : themes.GetTokens()) {
            tokens.insert(String(token.ToStringView()));
        }
        for (const String& role : roles) {
            EXPECT_TRUE(tokens.count(role) > 0) << "the role '" << role.c_str() << "' has no token in config/UIThemes.xml";
        }
        for (const String& token : tokens) {
            EXPECT_TRUE(roles.count(token) > 0) << "the token '" << token.c_str() << "' is no role of UiPalette.h";
        }
    }

    TEST_F(MahjongThemesTest, EveryRoleIsALegalTokenName) {
        for (const String& role : RoleNames()) {
            EXPECT_TRUE(UIThemes::IsValidToken(role)) << role.c_str();
        }
    }

    TEST_F(MahjongThemesTest, EveryThemeDefinesEveryToken) {
        ::BECore::Tests::ExpectThemesComplete(themes);
    }

    TEST_F(MahjongThemesTest, PaletteColorReadsTheClassicTheme) {
        const Optional<Color> surface = themes.Find("surface");
        ASSERT_TRUE(surface.has_value());
        EXPECT_TRUE(SameColor(PaletteColor("surface"), *surface));
    }

}  // namespace BECore::Mahjong::Tests
