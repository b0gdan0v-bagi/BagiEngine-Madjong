#include "MahjongButtonSupport.h"
#include "MahjongTableTestSupport.h"
#include "StringsTestSupport.h"

#include <BECore/UI/UIImageComponent.h>
#include <Mahjong/Components/MahjongHudComponent.h>

/**
 * The three round icon buttons of the HUD (`config/nodes/MahjongHudMenu.xml`): what each is made of, that its tooltip says its word in both languages (and `Leave room`
 * in a room, which `MahjongOnlineTableSceneTest` holds), and that `Rules` opens the pages from the table. Where they lie, their touch size and that they cover no tile are
 * held on the size matrix by `MahjongTouchSizeTest` and `MahjongHudTest`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kButtonSide = 56.0f;
        constexpr float kButtonRadius = kButtonSide * 0.5f;

        struct MenuButton {
            const char* name;
            const char* action;
            const char* sprite;
            const char* key;  ///< the string key of the tooltip (`NewGame` reads the variable `newGameWord`, which is `hud.newGame` outside a room)
        };

        constexpr MenuButton kMenuButtons[] = {
            {"Rules", "MahjongRules", "Icon_Rules", "hud.rules"},
            {"Options", "MahjongOptions", "Icon_Options", "hud.options"},
            {"NewGame", "MahjongNewGame", "Icon_NewGame", "hud.newGame"},
        };

    }  // namespace

    class MahjongHudMenuTest : public MahjongTableTestBase {};

    TEST_F(MahjongHudMenuTest, TheThreeButtonsAreRoundIconButtonsWithAnActionAndNoWord) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        for (const MenuButton& wanted : kMenuButtons) {
            SCOPED_TRACE(wanted.name);
            const UIElement* button = Element("HudMenu", wanted.name);
            ASSERT_NE(button, nullptr);
            EXPECT_EQ(button->rect.w, kButtonSide);
            EXPECT_EQ(button->rect.h, kButtonSide);
            EXPECT_EQ(button->box.cornerRadius.topLeft, kButtonRadius) << "round: half the side";
            EXPECT_EQ(button->box.cornerRadius.bottomRight, kButtonRadius);
            EXPECT_EQ(ButtonAction(button), PoolString::Intern(wanted.action));
            EXPECT_EQ(TextOf(button), nullptr) << "an icon button has no word that a size could cut";

            ASSERT_EQ(button->children.size(), 1u);
            const UIImageComponent* icon = button->children[0]->GetComponent<UIImageComponent>();
            ASSERT_NE(icon, nullptr);
            EXPECT_EQ(icon->sprite, PoolString::Intern(wanted.sprite));
            EXPECT_FALSE(button->children[0]->IsInteractive()) << "a click on the picture is a click on the button";
        }
    }

    TEST_F(MahjongHudMenuTest, TheTooltipOfEachButtonSaysItsWordInBothLanguages) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        std::vector<std::string> words;
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            for (const MenuButton& wanted : kMenuButtons) {
                const std::string tip = ShownTooltip(Element("HudMenu", wanted.name));
                const eastl::string expected = Tr(CoreManager::GetUIStrings(), wanted.key);
                EXPECT_EQ(tip, std::string(expected.c_str(), expected.size())) << wanted.name;
                EXPECT_NE(tip, wanted.key) << wanted.name << ": the key is in the table";
                words.push_back(tip);
            }
        });
        const std::set<std::string> distinct(words.begin(), words.end());
        EXPECT_EQ(distinct.size(), words.size()) << "six different words: three buttons, two languages";
    }

    TEST_F(MahjongHudMenuTest, TheRulesButtonOpensThePagesFromTheTableAndNotUnderThePanelsOrTwice) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->CloseSetup();
        Draw();
        ASSERT_TRUE(ClickElement("HudMenu", "Rules"));
        EXPECT_TRUE(_game->IsRulesOpen());
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_EQ(MahjongHudComponent::GetActive()->CurrentRulesPage(), 0);

        _game->CloseRules();
        _game->OpenSetup();
        EmitAction("MahjongRules");
        EXPECT_FALSE(_game->IsRulesOpen()) << "under the dim of Setup the button cannot be pressed";
        _game->CloseSetup();

        _game->OpenOnline();
        EmitAction("MahjongRules");
        EXPECT_FALSE(_game->IsRulesOpen()) << "nor under the online panel";
        _game->CloseOnline();

        _game->OpenOptions();
        EmitAction("MahjongRules");
        EXPECT_TRUE(_game->IsRulesOpen()) << "the page Options keeps its own button";
        EXPECT_FALSE(_game->IsOptionsOpen());
    }

}  // namespace BECore::Mahjong::Tests
