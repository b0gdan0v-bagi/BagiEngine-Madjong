#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <Mahjong/Components/MahjongHintsComponent.h>

/**
 * The hints of the shipped scene (`Components/MahjongHintsComponent.h`, `Table/Hints.h`). So far: the switch `Hints` On | Off of the
 * panel `Setup`, which the component answers and the game keeps (`MahjongGame::HintsOn()`).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kPanelTouchMinimum = 20.0f;  // the panel lies on the fixed design of the table, like the HUD
        constexpr float kMinControlHeight = 48.0f;
        constexpr float kButtonAir = 8.0f;
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}};

    }  // namespace

    class MahjongHintsTest : public MahjongTableTestBase {
    protected:
        static std::string HintsChoiceVar() {
            return Var("hintsChoice");
        }

        bool Selected(const char* segment) const {
            const UIElement* element = Element("Setup", segment);
            const auto* button = element != nullptr ? element->Cast<UIButton>() : nullptr;
            return button != nullptr && button->selected;
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }

        /// The panel open and drawn on the game as it is.
        void OpenPanel() {
            _game->OpenSetup();
            Draw();
            ASSERT_TRUE(SetupShown());
        }
    };

    // ----- the flag on the game -----

    TEST_F(MahjongHintsTest, HintsAreOnByDefaultAndTheComponentIsOnTheScene) {
        EXPECT_TRUE(_game->HintsOn());
        EXPECT_NE(MahjongHintsComponent::GetActive(), nullptr);
        MahjongGame fresh;
        EXPECT_TRUE(fresh.HintsOn());
        fresh.SetHintsOn(false);
        EXPECT_FALSE(fresh.HintsOn());
    }

    TEST_F(MahjongHintsTest, TheFlagIsNotAPartOfTheSettings) {
        const MahjongSettings before = _game->GetSettings();
        _game->SetHintsOn(false);
        const MahjongSettings after = _game->GetSettings();
        EXPECT_EQ(after.handsPerMatch, before.handsPerMatch);
        EXPECT_EQ(after.flowers, before.flowers);
        EXPECT_EQ(after.replayDrawnHand, before.replayDrawnHand);
        EXPECT_EQ(after.limit, before.limit);
        EXPECT_EQ(after.startingScore, before.startingScore);
    }

    // ----- the panel shows the flag -----

    TEST_F(MahjongHintsTest, ThePanelOpensOnTheFlagOfTheGameOn) {
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());
        EXPECT_EQ(HintsChoiceVar(), "on");
        EXPECT_TRUE(Selected("HintsOn"));
        EXPECT_FALSE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, ThePanelOpensOnTheFlagOfTheGameOff) {
        _game->SetHintsOn(false);
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());
        EXPECT_EQ(HintsChoiceVar(), "off");
        EXPECT_FALSE(Selected("HintsOn"));
        EXPECT_TRUE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, ThePanelHasTheRowWithTheWordsOfBothLanguages) {
        _game->Start();
        Draw();
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            const eastl::string on = Tr(CoreManager::GetUIStrings(), "setup.on");
            const eastl::string off = Tr(CoreManager::GetUIStrings(), "setup.off");
            const eastl::string caption = Tr(CoreManager::GetUIStrings(), "setup.hints");
            const auto text = [&](const char* element) {
                const auto* label = Element("Setup", element) != nullptr ? Element("Setup", element)->Cast<UILabel>() : nullptr;
                return label != nullptr ? std::string(label->GetDisplayText().data(), label->GetDisplayText().size()) : std::string("<none>");
            };
            const auto buttonText = [&](const char* element) {
                const auto* button = Element("Setup", element) != nullptr ? Element("Setup", element)->Cast<UIButton>() : nullptr;
                return button != nullptr ? std::string(button->GetDisplayText().data(), button->GetDisplayText().size()) : std::string("<none>");
            };
            EXPECT_EQ(text("HintsCaption"), std::string(caption.data(), caption.size()));
            EXPECT_EQ(buttonText("HintsOn"), std::string(on.data(), on.size()));
            EXPECT_EQ(buttonText("HintsOff"), std::string(off.data(), off.size()));
        });
    }

    // ----- the clicks -----

    TEST_F(MahjongHintsTest, AClickOnASegmentChangesTheGameAndTheVariableAtOnce) {
        _game->Start();
        Draw();

        ASSERT_TRUE(ClickElement("Setup", "HintsOff"));
        EXPECT_FALSE(_game->HintsOn()) << "no Start needed";
        EXPECT_EQ(HintsChoiceVar(), "off");
        Draw();
        EXPECT_TRUE(Selected("HintsOff"));
        EXPECT_FALSE(Selected("HintsOn"));
        EXPECT_TRUE(SetupShown()) << "the panel stays open";

        ASSERT_TRUE(ClickElement("Setup", "HintsOn"));
        EXPECT_TRUE(_game->HintsOn());
        EXPECT_EQ(HintsChoiceVar(), "on");
        Draw();
        EXPECT_TRUE(Selected("HintsOn"));
        EXPECT_FALSE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, AClickChangesNeitherTheMatchNorThePendingSetup) {
        _game->Start();
        Draw();
        const uint64_t version = _game->GetSession().Version();
        const SeatSetup seats = _game->GetSeats();
        const int hands = _game->GetSettings().handsPerMatch;

        ASSERT_TRUE(ClickElement("Setup", "HintsOff"));
        Draw();

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetSeats(), seats);
        EXPECT_EQ(_game->GetSettings().handsPerMatch, hands);
        EXPECT_TRUE(_game->IsSetupOpen());
    }

    TEST_F(MahjongHintsTest, AnyOtherValueAndAClosedPanelChangeNothing) {
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());

        for (const char* value : {"", "ON", "Off", "1", "0", "true", "false", "on ", " off", "yes", "no"}) {
            EmitAction("MahjongSetHints", value);
            EXPECT_TRUE(_game->HintsOn()) << "'" << value << "'";
            EXPECT_EQ(HintsChoiceVar(), "on") << "'" << value << "'";
        }

        _game->CloseSetup();
        Draw();
        ASSERT_FALSE(SetupShown());
        EmitAction("MahjongSetHints", "off");
        EXPECT_TRUE(_game->HintsOn()) << "the panel is closed";
        EXPECT_EQ(HintsChoiceVar(), "on");
    }

    TEST_F(MahjongHintsTest, TheVariableKeepsTheTruthWhenTheGameChangesTheFlag) {
        _game->Start();
        Draw();
        ASSERT_EQ(HintsChoiceVar(), "on");

        _game->SetHintsOn(false);  // not through the panel
        Draw();

        EXPECT_EQ(HintsChoiceVar(), "off");
        EXPECT_TRUE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, AnUnchangedPanelSetsNothingAgain) {
        _game->Start();
        Draw();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    // ----- the choice is kept -----

    TEST_F(MahjongHintsTest, TheChoiceIsKeptByStartNewGameResetAndAControlReset) {
        _game->Start();
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "HintsOff"));
        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();
        ASSERT_FALSE(SetupShown());
        EXPECT_FALSE(_game->HintsOn()) << "Start";

        ASSERT_TRUE(ClickElement("Hud", "NewGame"));
        Draw();
        ASSERT_TRUE(SetupShown());
        EXPECT_FALSE(_game->HintsOn()) << "New game";
        EXPECT_EQ(HintsChoiceVar(), "off");
        EXPECT_TRUE(Selected("HintsOff"));

        _game->Reset();
        EXPECT_FALSE(_game->HintsOn()) << "Reset()";
        ASSERT_TRUE(_game->StartMatch(5).has_value());
        EXPECT_FALSE(_game->HintsOn()) << "StartMatch";

        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        Json options = Json::object();
        options["seed"] = 3;
        ASSERT_TRUE(control->Reset(options).has_value());
        EXPECT_FALSE(_game->HintsOn()) << "a control reset leaves it alone";
        Draw();
        ASSERT_TRUE(ClickElement("Hud", "NewGame"));
        Draw();
        EXPECT_EQ(HintsChoiceVar(), "off");
    }

    TEST_F(MahjongHintsTest, TheControlStateIsTheSameWithHintsOnAndOff) {
        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        Json options = Json::object();
        options["seed"] = 11;

        _game->SetHintsOn(true);
        ASSERT_TRUE(control->Reset(options).has_value());
        const auto onState = control->GetState();
        const auto onActions = control->GetActions();
        ASSERT_TRUE(onState.has_value());
        ASSERT_TRUE(onActions.has_value());

        _game->SetHintsOn(false);
        ASSERT_TRUE(control->Reset(options).has_value());
        const auto offState = control->GetState();
        const auto offActions = control->GetActions();
        ASSERT_TRUE(offState.has_value());
        ASSERT_TRUE(offActions.has_value());

        // The version counts the steps of the session (two resets apart), the rest is the match and it is the same
        Json onRest = *onState;
        Json offRest = *offState;
        onRest.erase("version");
        offRest.erase("version");
        EXPECT_EQ(onRest.dump(), offRest.dump());
        EXPECT_EQ(onActions->dump(), offActions->dump());
    }

    // ----- the panel with the new row -----

    TEST_F(MahjongHintsTest, TheSetupPanelPassesTheSizeMatrixAndEveryWordFitsInEveryLanguage) {
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ExpectPassesTheMatrix("Setup", kPanelTouchMinimum);
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Setup", output.width, output.height, kButtonAir);
            }
            for (const char* segmentName : {"HintsOn", "HintsOff"}) {
                const UIElement* segment = Element("Setup", segmentName);
                ASSERT_NE(segment, nullptr) << segmentName;
                EXPECT_GE(segment->rect.h, kMinControlHeight) << segmentName;
            }
        });
    }

    TEST_F(MahjongHintsTest, ThePlateHoldsEveryRowOfThePanel) {
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());
        const UIElement* plate = Element("Setup", "Plate");
        ASSERT_NE(plate, nullptr);
        for (const char* name : {"Title", "LanguageRow", "SeatRow0", "SeatRow3", "HandsRow", "HintsRow", "Start"}) {
            const UIElement* row = Element("Setup", name);
            ASSERT_NE(row, nullptr) << name;
            EXPECT_GE(row->rect.y, plate->rect.y) << name;
            EXPECT_LE(row->rect.y + row->rect.h, plate->rect.y + plate->rect.h) << name;
        }
        const UIElement* hints = Element("Setup", "HintsRow");
        const UIElement* hands = Element("Setup", "HandsRow");
        const UIElement* start = Element("Setup", "Start");
        EXPECT_GE(hints->rect.y, hands->rect.y + hands->rect.h) << "the row lies after HandsRow";
        EXPECT_LE(hints->rect.y + hints->rect.h, start->rect.y) << "and before Start";
    }

}  // namespace BECore::Mahjong::Tests
