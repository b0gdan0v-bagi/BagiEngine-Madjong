#include "MahjongButtonSupport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/UI/UITooltipComponent.h>
#include <CoreSDL/SDLEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>

/**
 * The way out of `Setup` (`config/nodes/MahjongSetup.xml`, `Components/MahjongHudComponent.h`): the close cross in the corner of the panel and the
 * Escape key close it and leave the game as it was: no match begun, no choice applied, the table behind the panel is where the player was.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        void PressKey(SDL_Keycode key, bool repeat = false) {
            SDLEvents::KeyDownEvent::Emit(key, SDL_KMOD_NONE, repeat);
        }

        /// What a close must not move: the choices the game keeps for the next match and the match on the table.
        struct GameFacts {
            SeatSetup seats;
            SeatLevels levels;
            int hands;
            uint64_t version;
            size_t starts;

            bool operator==(const GameFacts&) const = default;
        };

    }  // namespace

    class MahjongSetupCloseTest : public MahjongTableTestBase {
    protected:
        GameFacts FactsOfTheGame() const {
            return {_game->GetSeats(), _game->GetLevels(), _game->GetSettings().handsPerMatch, _game->GetSession().Version(), _game->GetSession().StartCount()};
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }

        bool OptionsShown() const {
            const auto canvas = Canvas("Options");
            return canvas && canvas->TakesInput();
        }
    };

    TEST_F(MahjongSetupCloseTest, TheCrossClosesThePanelAndLeavesTheGameAndItsChoicesAsTheyWere) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());
        const GameFacts before = FactsOfTheGame();

        // A pending choice that Start would have applied
        ASSERT_TRUE(ClickElement("Setup", "Seat2Person"));
        ASSERT_TRUE(ClickElement("Setup", "Hands8"));
        Draw();
        ASSERT_NE(MahjongHudComponent::GetActive()->PendingSeats(), before.seats);

        ASSERT_TRUE(ClickElement("Setup", "SetupClose"));
        Draw();

        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(SetupShown());
        EXPECT_FALSE(_game->IsTablePaused());
        EXPECT_EQ(FactsOfTheGame(), before) << "no match began and nothing the panel offered was applied";

        // The next visit starts from the choices of the game, not from the abandoned ones
        _game->OpenSetup();
        Draw();
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingSeats(), before.seats);
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingHands(), before.hands);
    }

    TEST_F(MahjongSetupCloseTest, TheCrossOfTheStartPanelGoesBackToTheTableDealtBehindIt) {
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());
        const GameFacts before = FactsOfTheGame();

        ASSERT_TRUE(ClickElement("Setup", "SetupClose"));
        Draw();

        EXPECT_FALSE(SetupShown());
        EXPECT_EQ(FactsOfTheGame(), before);
        EXPECT_TRUE(OnScreen("HudMenu", "NewGame")) << "the way back to the panel is there";
        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
        Draw();
        EXPECT_TRUE(SetupShown());
    }

    TEST_F(MahjongSetupCloseTest, EscapeDoesTheSameAsTheCross) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "Seat2Person"));
        Draw();
        const GameFacts before = FactsOfTheGame();

        PressKey(SDLK_ESCAPE);
        Draw();

        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(SetupShown());
        EXPECT_EQ(FactsOfTheGame(), before);
    }

    TEST_F(MahjongSetupCloseTest, OtherKeysAndAHeldEscapeCloseNothingAndEscapeWithNoPanelDoesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();

        PressKey(SDLK_RETURN);
        PressKey(SDLK_SPACE);
        PressKey(SDLK_ESCAPE, true);
        Draw();
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(SetupShown());

        _game->CloseSetup();
        _game->OpenRules();
        Draw();
        const GameFacts before = FactsOfTheGame();
        PressKey(SDLK_ESCAPE);
        Draw();
        EXPECT_TRUE(_game->IsRulesOpen()) << "the rules screen has its own Close";
        EXPECT_EQ(FactsOfTheGame(), before);
    }

    TEST_F(MahjongSetupCloseTest, EscapeClosesThePageOptionsThatTheHudButtonOpenedOverTheTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->CloseSetup();
        Draw();
        ASSERT_TRUE(ClickElement("HudMenu", "Options"));
        Draw();
        ASSERT_TRUE(OptionsShown());
        EXPECT_FALSE(_game->IsSetupOpen()) << "the page is not a part of the seat choice";

        PressKey(SDLK_ESCAPE);
        Draw();
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(OptionsShown());
        EXPECT_FALSE(_game->IsSetupOpen());
    }

    TEST_F(MahjongSetupCloseTest, TheHudButtonOptionsIsOutOfDateUnderThePanelAndTheRules) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        EmitAction("MahjongOptions");
        Draw();
        EXPECT_FALSE(_game->IsOptionsOpen()) << "the dim of the seat choice covers the HUD button";

        _game->CloseSetup();
        _game->OpenRules();
        EmitAction("MahjongOptions");
        EXPECT_FALSE(_game->IsOptionsOpen()) << "the rules screen hides the HUD button";
        EmitAction("MahjongSetupClose");
        EXPECT_TRUE(_game->IsRulesOpen()) << "nothing of the panel works while it is closed";
    }

    TEST_F(MahjongSetupCloseTest, TheCrossSitsAtTheRightEndOfTheTitleBandAndTheTitleStaysCentredInBothLanguages) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        for (const char* language : {"en", "ru"}) {
            SCOPED_TRACE(language);
            ASSERT_TRUE(_game->SetLanguage(PoolString::Intern(language)));
            Draw();
            const UIElement* band = Element("Setup", "TitleRow");
            const UIElement* cross = Element("Setup", "SetupClose");
            const UIElement* title = Element("Setup", "Title");
            ASSERT_TRUE(band != nullptr && cross != nullptr && title != nullptr);
            EXPECT_EQ(cross->rect.x + cross->rect.w, band->rect.x + band->rect.w);
            EXPECT_EQ(cross->rect.h, band->rect.h);
            EXPECT_EQ(cross->rect.w, cross->rect.h) << "a square";
            EXPECT_NEAR(title->rect.x + title->rect.w * 0.5f, band->rect.x + band->rect.w * 0.5f, 0.01f);

            ASSERT_TRUE(IsButton(cross));
            EXPECT_EQ(ButtonAction(cross), "MahjongSetupClose"_intern);
            const auto* tooltip = cross->GetComponent<UITooltipComponent>();
            EXPECT_TRUE(tooltip != nullptr && !tooltip->text.empty()) << "a cross has no word on it: the tooltip says what it does";
            const UIStrings& strings = CoreManager::GetUIStrings();
            EXPECT_NE(Tr(strings, "setup.close"), "setup.close");
        }
    }

}  // namespace BECore::Mahjong::Tests
