#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/RulesText.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The rules screen of the shipped scene (`Components/MahjongHudComponent.h`, `Table/RulesText.h`): the `Rules` button of `HudMenu` opens
 * the modal pages `Rules` with their bar `RulesAction`, `Next` / `Back` turn them, `Close` closes them, the variables and the elements are
 * the page `FormatRulesPage` makes in each language, nothing moves behind the screen, and a new match or a control action closes it.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// Seat 0 waits on Ew; drawing 9p it can discard 9p and still wait.
        constexpr const char* kWaitingHand = "1s 2s 3s 4s 5s 6s 7m 8m 9m 2p 3p 4p Ew";
        constexpr float kLongWait = 10.0f;

        void Elapse(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

        std::string StdString(eastl::string_view text) {
            return std::string(text.data(), text.size());
        }

    }  // namespace

    class MahjongRulesTest : public MahjongTableTestBase {
    protected:
        /// Whether the modal pages are up: a canvas that is shown takes the input.
        bool RulesShown() const {
            const auto canvas = Canvas("Rules");
            return canvas && canvas->TakesInput();
        }

        bool BarShown() const {
            const auto canvas = Canvas("RulesAction");
            return canvas && canvas->TakesInput();
        }

        bool MenuShown() const {
            const auto canvas = Canvas("HudMenu");
            return canvas && canvas->TakesInput();
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }

        std::string Text(const char* node, const char* element) const {
            const UIElement* found = Element(node, element);
            const UILabel* label = found != nullptr ? found->Cast<UILabel>() : nullptr;
            if (label == nullptr && found != nullptr) {
                label = found->Cast<UIButton>();  // a button is a label by inheritance, but `Cast` is exact
            }
            return label != nullptr ? StdString(label->GetDisplayText()) : std::string("<none>");
        }

        std::string SpriteOf(const char* element) const {
            const auto* image = Element("Rules", element) != nullptr ? Element("Rules", element)->Cast<UIImage>() : nullptr;
            return image != nullptr ? std::string(image->sprite.ToStringView().data(), image->sprite.ToStringView().size()) : std::string("<none>");
        }

        /// The rectangles drawn in `color` by the last draw.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && call.color.r == color.r && call.color.g == color.g && call.color.b == color.b && call.color.a == color.a) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        MahjongHudComponent* Hud() const {
            return MahjongHudComponent::GetActive();
        }

        /// Opens the pages with the button of the HUD, as a person would.
        void PressRules() {
            Draw();
            ASSERT_TRUE(ClickElement("HudMenu", "Rules"));
            Draw();
        }

        /// A press on a button of the bar.
        void PressBar(const char* button) {
            ASSERT_TRUE(ClickElement("RulesAction", button)) << button;
            Draw();
        }

        /// The screen shows the page `page`: the variables and the elements are what `FormatRulesPage` and `RulesPagerText` make now.
        void ExpectPageShown(int page) {
            const UIStrings& strings = CoreManager::GetUIStrings();
            const RulesPage expected = FormatRulesPage(strings, page);
            EXPECT_EQ(Var("rulesTitle"), StdString(expected.title));
            EXPECT_EQ(Var("rulesPager"), StdString(RulesPagerText(strings, page)));
            EXPECT_EQ(Text("Rules", "Title"), StdString(expected.title));
            EXPECT_EQ(Text("Rules", "Pager"), StdString(RulesPagerText(strings, page)));
            for (int i = 0; i < kRulesMaxLines; ++i) {
                const bool has = i < static_cast<int>(expected.lines.size());
                const std::string name = "RulesLine" + std::to_string(i);
                EXPECT_EQ(OnScreen("Rules", name.c_str()), has) << "line " << i << " of page " << page;
                EXPECT_EQ(Var(("rulesLine" + std::to_string(i)).c_str()), has ? StdString(expected.lines[static_cast<size_t>(i)]) : std::string()) << "line " << i;
            }
            for (int i = 0; i < kRulesMaxItems; ++i) {
                const bool has = i < static_cast<int>(expected.items.size());
                const std::string name = "RulesItem" + std::to_string(i);
                EXPECT_EQ(OnScreen("Rules", name.c_str()), has) << "item " << i << " of page " << page;
                EXPECT_EQ(Var(("rulesItem" + std::to_string(i)).c_str()), has ? StdString(expected.items[static_cast<size_t>(i)]) : std::string()) << "item " << i;
                EXPECT_EQ(Text("Rules", name.c_str()), has ? StdString(expected.items[static_cast<size_t>(i)]) : std::string()) << "item " << i;
            }
            for (int row = 0; row < kRulesMaxItems / kRulesItemColumns; ++row) {
                const std::string name = "RulesItemRow" + std::to_string(row);
                EXPECT_EQ(OnScreen("Rules", name.c_str()), row * kRulesItemColumns < static_cast<int>(expected.items.size())) << name << " of page " << page;
            }
            for (int row = 0; row < kRulesMaxTileRows; ++row) {
                const bool hasRow = row < static_cast<int>(expected.tileRows.size());
                EXPECT_EQ(OnScreen("Rules", ("RulesTileRow" + std::to_string(row)).c_str()), hasRow) << "tile row " << row << " of page " << page;
                for (int column = 0; column < kRulesMaxRowTiles; ++column) {
                    const std::string name = "RulesTile" + std::to_string(row * kRulesMaxRowTiles + column);
                    const bool has = hasRow && column < static_cast<int>(expected.tileRows[static_cast<size_t>(row)].size());
                    EXPECT_EQ(OnScreen("Rules", name.c_str()), has) << name << " of page " << page;
                    if (has) {
                        EXPECT_EQ(SpriteOf(name.c_str()), std::string(SpriteName(expected.tileRows[static_cast<size_t>(row)][static_cast<size_t>(column)]).c_str())) << name;
                    }
                }
            }
            EXPECT_EQ(OnScreen("RulesAction", "RulesBack"), page > 0) << "Back is hidden on the first page";
            EXPECT_EQ(OnScreen("RulesAction", "RulesNext"), page + 1 < RulesPageCount()) << "Next is hidden on the last page";
            EXPECT_TRUE(OnScreen("RulesAction", "RulesClose"));
        }

        /// A match with the people of `seats`, then a fixed hand laid: seat 0 is East and holds `seat0Hand`.
        void StartWith(const SeatSetup& seats, const char* seat0Hand = kPlainHand) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(MakeModel(SpecOf(seat0Hand, "9p"), kSeat0));
            Draw();
        }
    };

    // ----- opening, turning and closing -----

    TEST_F(MahjongRulesTest, TheScreenIsHiddenAtTheStartAndTheButtonOfTheHudIsThere) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        EXPECT_FALSE(_game->IsRulesOpen());
        EXPECT_FALSE(RulesShown());
        EXPECT_FALSE(BarShown());
        EXPECT_TRUE(MenuShown());
        EXPECT_TRUE(OnScreen("HudMenu", "Rules"));
        EXPECT_TRUE(OnScreen("HudMenu", "NewGame"));
        EXPECT_EQ(Text("HudMenu", "Rules"), "Rules");
        EXPECT_EQ(Hud()->CurrentRulesPage(), 0);
    }

    TEST_F(MahjongRulesTest, TheButtonOpensTheFirstPageAndHidesTheMenu) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());

        EXPECT_TRUE(_game->IsRulesOpen());
        EXPECT_TRUE(RulesShown());
        EXPECT_TRUE(BarShown());
        EXPECT_FALSE(MenuShown()) << "the buttons of HudMenu lie on a bar outside the view that the dim covers";
        EXPECT_EQ(Hud()->CurrentRulesPage(), 0);
        EXPECT_FALSE(OnScreen("RulesAction", "RulesBack"));
        EXPECT_TRUE(OnScreen("RulesAction", "RulesNext"));
        ExpectPageShown(0);
    }

    TEST_F(MahjongRulesTest, NextGoesToTheLastPageAndBackReturns) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());

        for (int page = 1; page < RulesPageCount(); ++page) {
            ASSERT_NO_FATAL_FAILURE(PressBar("RulesNext"));
            EXPECT_EQ(Hud()->CurrentRulesPage(), page);
            ExpectPageShown(page);
        }
        EXPECT_FALSE(OnScreen("RulesAction", "RulesNext"));
        EmitAction("MahjongRulesNext");
        Draw();
        EXPECT_EQ(Hud()->CurrentRulesPage(), RulesPageCount() - 1) << "the page is clamped";

        for (int page = RulesPageCount() - 2; page >= 0; --page) {
            ASSERT_NO_FATAL_FAILURE(PressBar("RulesBack"));
            EXPECT_EQ(Hud()->CurrentRulesPage(), page);
            ExpectPageShown(page);
        }
        EmitAction("MahjongRulesBack");
        Draw();
        EXPECT_EQ(Hud()->CurrentRulesPage(), 0) << "the page is clamped";
    }

    TEST_F(MahjongRulesTest, CloseHidesThePagesAndShowsTheMenuAgainAndTheNextOpeningStartsOnTheFirstPage) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_NO_FATAL_FAILURE(PressBar("RulesNext"));
        ASSERT_EQ(Hud()->CurrentRulesPage(), 1);

        ASSERT_NO_FATAL_FAILURE(PressBar("RulesClose"));

        EXPECT_FALSE(_game->IsRulesOpen());
        EXPECT_FALSE(RulesShown());
        EXPECT_FALSE(BarShown());
        EXPECT_TRUE(MenuShown());

        ASSERT_NO_FATAL_FAILURE(PressRules());
        EXPECT_EQ(Hud()->CurrentRulesPage(), 0);
        ExpectPageShown(0);
    }

    TEST_F(MahjongRulesTest, EveryPageIsShownAsFormattedInBothLanguages) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ASSERT_TRUE(_game->SetLanguage(language));
            for (int page = 0; page < RulesPageCount(); ++page) {
                while (Hud()->CurrentRulesPage() < page) {
                    EmitAction("MahjongRulesNext");
                }
                while (Hud()->CurrentRulesPage() > page) {
                    EmitAction("MahjongRulesBack");
                }
                Draw();
                ASSERT_NO_FATAL_FAILURE(ExpectPageShown(page));
            }
        });
    }

    TEST_F(MahjongRulesTest, ALanguageSwitchWhileOpenRewordsThePageAtTheNextDraw) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_NO_FATAL_FAILURE(PressBar("RulesNext"));
        const std::string english = Var("rulesTitle");
        const std::string englishButton = Text("RulesAction", "RulesNext");
        ASSERT_FALSE(english.empty());

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();

        EXPECT_NE(Var("rulesTitle"), english);
        EXPECT_EQ(Hud()->CurrentRulesPage(), 1) << "the page stays";
        ExpectPageShown(1);
        EXPECT_EQ(Text("RulesAction", "RulesNext"), "Далее");
        EXPECT_NE(Text("RulesAction", "RulesNext"), englishButton);
        EXPECT_EQ(Text("HudMenu", "Rules"), "Правила");

        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Draw();
        EXPECT_EQ(Var("rulesTitle"), english);
    }

    TEST_F(MahjongRulesTest, AnUnchangedFrameSetsNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        const uint64_t version = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    // ----- what closes it, what it ignores -----

    TEST_F(MahjongRulesTest, ANewMatchAControlResetAndAControlActionCloseThePages) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_TRUE(RulesShown());
        ASSERT_TRUE(_game->StartMatch(11).has_value());
        Draw();
        EXPECT_FALSE(_game->IsRulesOpen());
        EXPECT_FALSE(RulesShown());
        EXPECT_FALSE(BarShown());
        EXPECT_TRUE(MenuShown());

        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_TRUE(RulesShown());
        Json options = Json::object();
        options["seed"] = 3;
        ASSERT_TRUE(control->Reset(options).has_value());
        Draw();
        EXPECT_FALSE(RulesShown());

        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_TRUE(RulesShown());
        const auto actions = control->GetActions();
        ASSERT_TRUE(actions.has_value());
        ASSERT_FALSE(actions->empty());
        EXPECT_TRUE(RulesShown()) << "listing does not enter the game";
        ASSERT_TRUE(control->PerformAction((*actions)[0]).has_value());
        Draw();
        EXPECT_FALSE(_game->IsRulesOpen());
        EXPECT_FALSE(RulesShown());
        EXPECT_FALSE(BarShown());
        EXPECT_TRUE(MenuShown());
    }

    TEST_F(MahjongRulesTest, AStaleEventWhileTheScreenIsClosedChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        for (const char* action : {"MahjongRulesNext", "MahjongRulesBack", "MahjongRulesClose"}) {
            EmitAction(action);
            Draw();
            EXPECT_FALSE(_game->IsRulesOpen()) << action;
            EXPECT_EQ(Hud()->CurrentRulesPage(), 0) << action;
            EXPECT_FALSE(RulesShown()) << action;
        }
        EXPECT_TRUE(MenuShown());

        // The same after the screen was closed by a control action: the page does not move under the next opening
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_NO_FATAL_FAILURE(PressBar("RulesNext"));
        _game->CloseRules();
        EmitAction("MahjongRulesNext");
        Draw();
        EXPECT_EQ(Hud()->CurrentRulesPage(), 0);
        EXPECT_FALSE(_game->IsRulesOpen());
    }

    TEST_F(MahjongRulesTest, TheButtonCannotBePressedWhileTheSeatChoiceIsOpen) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());

        EXPECT_TRUE(ClickElement("HudMenu", "Rules")) << "the click goes to the dim of Setup, which covers the whole window";
        Draw();
        EXPECT_FALSE(_game->IsRulesOpen());
        EXPECT_FALSE(RulesShown());

        EmitAction("MahjongRules");  // an event that says otherwise is out of date
        Draw();
        EXPECT_FALSE(_game->IsRulesOpen());
    }

    // ----- nothing moves behind it -----

    TEST_F(MahjongRulesTest, NoComputerStepsWhileThePagesAreOpenAndTheyGoOnAfterClose) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::Playing);
        ASSERT_NO_FATAL_FAILURE(PressRules());
        TableSession& session = _game->GetSession();
        const uint64_t version = session.Version();
        const std::string snapshot = Snapshot(Hand());

        for (int i = 0; i < 5; ++i) {
            Elapse(kLongWait);
            Draw();
        }

        EXPECT_EQ(session.Version(), version);
        EXPECT_EQ(Snapshot(Hand()), snapshot);

        ASSERT_NO_FATAL_FAILURE(PressBar("RulesClose"));
        Elapse(kComputerStepSeconds + 0.1f);
        Draw();
        EXPECT_GT(session.Version(), version) << "after Close the computers go on";
    }

    TEST_F(MahjongRulesTest, NoClickAndNoActionOfTheTableIsTakenWhileThePagesAreOpen) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        const uint64_t version = _game->GetSession().Version();
        const std::string snapshot = Snapshot(Hand());

        // The first tile of the row lies left of the buttons of the bar, so the click is the modal canvas's and no button's
        ASSERT_NE(HandSlot(0), nullptr);
        EXPECT_LT(HandSlot(0)->rect.x + HandSlot(0)->rect.w, Element("RulesAction", "RulesNext")->rect.x);
        EXPECT_TRUE(ClickHandTile(0)) << "the modal canvas takes the click";
        EXPECT_TRUE(_game->IsRulesOpen());
        EmitAction("MahjongDeclare", "on");
        EmitAction("MahjongPass");
        Draw();

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(Snapshot(Hand()), snapshot);
        EXPECT_FALSE(_table->DeclareNext()) << "an action event of the table is dropped";

        ASSERT_NO_FATAL_FAILURE(PressBar("RulesClose"));
        EmitAction("MahjongDeclare", "on");
        Draw();
        EXPECT_TRUE(_table->DeclareNext()) << "and taken again after Close";
    }

    TEST_F(MahjongRulesTest, TheHighlightClockOfTheTableStandsStillWhileThePagesAreOpen) {
        // Seat 1 has pungged the 5p of seat 0 and is to act: the set just laid blinks for `kClaimHighlightSeconds` of scene time
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Draw();
        Lay(OpenPungModel(kSeat1Hand));
        Draw();
        ASSERT_EQ(RectsOf(kClaimHighlight).size(), 1u) << "on at the moment of the move";

        _game->OpenRules();
        Elapse(kClaimHighlightSeconds + kLongWait);
        _game->CloseRules();
        Draw();

        EXPECT_EQ(RectsOf(kClaimHighlight).size(), 1u) << "no time passed while the pages were open";
        Elapse(kClaimHighlightSeconds + 0.1f);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "it runs out once the table is played again";
    }

    TEST_F(MahjongRulesTest, TheHintsGoWithThePagesAndComeBackOnClose) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Lay(MakeModel(SpecOf(kWaitingHand, "9p"), kSeat0));
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeDiscard(T("9p"), false)).has_value());
        Draw();
        ASSERT_TRUE(_game->HintsOn());
        EXPECT_TRUE(OnScreen("Hints", "WaitPanel")) << "a waiting hand shows its row during the computers' turns";

        ASSERT_NO_FATAL_FAILURE(PressRules());
        EXPECT_FALSE(OnScreen("Hints", "WaitPanel")) << "no waiting row under the dim of the pages";

        ASSERT_NO_FATAL_FAILURE(PressBar("RulesClose"));
        EXPECT_TRUE(OnScreen("Hints", "WaitPanel"));
    }

}  // namespace BECore::Mahjong::Tests
