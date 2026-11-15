#include <BECore/RefCounted/New.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIThemes.h>
#include <Mahjong/Components/ColorTokens.h>
#include <gtest/gtest.h>
#include <tests/UiStyleTestSupport.h>

// The colours a component sets in code: a token written once per change, resolved at the next draw; a mark a plain colour.
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr Color kFirst{10, 20, 30, 255};
        constexpr Color kSecond{40, 50, 60, 255};

        class ColorTokensTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ASSERT_TRUE(themes.Set(PoolString::Intern("one"), "first", kFirst));
                ASSERT_TRUE(themes.Set(PoolString::Intern("one"), "second", kSecond));
                element = New<UIElement>();
            }

            UIThemes themes;
            IntrusivePtr<UIElement> element;
        };

    }  // namespace

    TEST_F(ColorTokensTest, ATokenSetInCodeIsResolvedAtTheNextDrawEvenAfterTheElementResolvedOnce) {
        element->ResolveTheme(themes);

        SetColorToken(*element, element->color, "first"_intern);
        element->ResolveTheme(themes);

        EXPECT_EQ(element->color.token, "first"_intern);
        EXPECT_TRUE(SameColor(element->color, kFirst));
    }

    TEST_F(ColorTokensTest, TheSameRoleAgainWritesNothingAndLeavesTheResolvedColour) {
        SetColorToken(*element, element->color, "first"_intern);
        element->ResolveTheme(themes);
        element->color.r = 99;  // a mark of a write: a second one would resolve it back

        SetColorToken(*element, element->color, "first"_intern);
        element->ResolveTheme(themes);

        EXPECT_EQ(element->color.r, 99);
    }

    TEST_F(ColorTokensTest, AnotherRoleIsResolvedToItsColour) {
        SetColorToken(*element, element->color, "first"_intern);
        element->ResolveTheme(themes);

        SetColorToken(*element, element->color, "second"_intern);
        element->ResolveTheme(themes);

        EXPECT_TRUE(SameColor(element->color, kSecond));
    }

    TEST_F(ColorTokensTest, APlainColourDropsTheTokenAndStaysThroughASwitchOfTheTheme) {
        SetColorToken(*element, element->color, "first"_intern);
        element->ResolveTheme(themes);

        SetPlainColor(element->color, Color{1, 2, 3, 4});
        ASSERT_TRUE(themes.Set(PoolString::Intern("one"), "first", kSecond));
        element->ResolveTheme(themes);

        EXPECT_TRUE(element->color.token.Empty());
        EXPECT_TRUE(SameColor(element->color, Color{1, 2, 3, 4}));
    }

}  // namespace BECore::Mahjong::Tests
