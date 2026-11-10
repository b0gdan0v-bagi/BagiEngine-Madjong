#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

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
        constexpr float kButtonAir = 8.0f;
        constexpr float kMinControlHeight = 48.0f;
        /// Output sizes the words are asked in: the design size, a small window, the narrowest windows and a wide short one.
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};

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

        /// Turns the open pages to `page` by the actions of the bar and draws.
        void GoToPage(int page) {
            while (Hud()->CurrentRulesPage() < page) {
                EmitAction("MahjongRulesNext");
            }
            while (Hud()->CurrentRulesPage() > page) {
                EmitAction("MahjongRulesBack");
            }
            Draw();
            ASSERT_EQ(Hud()->CurrentRulesPage(), page);
        }

        /// The children of the plate that the page shows, in the order of the file (a hidden one has no rect).
        std::vector<const UIElement*> ShownBlocks() const {
            std::vector<const UIElement*> blocks;
            const UIElement* plate = Element("Rules", "Plate");
            if (plate != nullptr) {
                for (const auto& child : plate->children) {
                    if (!child->rect.IsEmpty()) {
                        blocks.push_back(child.Get());
                    }
                }
            }
            return blocks;
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

    TEST_F(MahjongRulesTest, TheMarksAndTheHintButtonGoWithThePagesAndComeBackOnClose) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Lay(MakeModel(SpecOf(kWaitingHand, "9p"), kSeat0));
        Draw();
        ASSERT_TRUE(_game->HintsOn());
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        const auto button = [this] {
            const auto canvas = Canvas("HintsButton");
            return canvas && canvas->TakesInput() && OnScreen("HintsButton", "HintButton");
        };
        EXPECT_EQ(RectsOf(kDeclareMark).size(), 2u) << "the 9p and the east wind declare the calling hand";
        EXPECT_TRUE(button()) << "the person has a choice to ask advice about";

        ASSERT_NO_FATAL_FAILURE(PressRules());
        EXPECT_TRUE(RectsOf(kDeclareMark).empty()) << "no marks under the dim of the pages";
        EXPECT_FALSE(button()) << "no hint button under the dim of the pages";

        ASSERT_NO_FATAL_FAILURE(PressBar("RulesClose"));
        EXPECT_EQ(RectsOf(kDeclareMark).size(), 2u);
        EXPECT_TRUE(button());
    }

    // ----- the size matrix and the word fit -----

    TEST_F(MahjongRulesTest, EveryPageInEveryLanguagePassesTheSizeMatrixAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ASSERT_TRUE(_game->SetLanguage(language));
            for (int page = 0; page < RulesPageCount(); ++page) {
                SCOPED_TRACE(page);
                ASSERT_NO_FATAL_FAILURE(GoToPage(page));
                ASSERT_NO_FATAL_FAILURE(ExpectPageShown(page));
                // The pages are text and pictures (no control), the bar holds the buttons: the engine's touch minimum for both
                ExpectPassesTheMatrix("Rules", EngineTouchMinimum());
                ExpectPassesTheMatrix("RulesAction", EngineTouchMinimum());
                for (const Output& output : kOutputs) {
                    ExpectWordsFit("Rules", output.width, output.height, kButtonAir);
                    ExpectWordsFit("RulesAction", output.width, output.height, kButtonAir);
                }
                // The same on the whole matrix, each window against the view of its shape (the plate is 94% of the 720 wide portrait view); the bar
                // is a Screen canvas, which the view does not touch
                const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("Rules"), kButtonAir);
                EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
                // The matrix leaves the layout as it ran last: draw again for the next page
                Draw();
            }
        });
    }

    TEST_F(MahjongRulesTest, TheBarsButtonsAreTouchTargetsOnThePagesWhereAllThreeAreShown) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_NO_FATAL_FAILURE(GoToPage(1));
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            ASSERT_TRUE(_game->SetLanguage(language));
            Draw();
            for (const char* button : {"RulesBack", "RulesNext", "RulesClose"}) {
                ASSERT_TRUE(OnScreen("RulesAction", button)) << button;
                EXPECT_GE(Element("RulesAction", button)->rect.h, kMinControlHeight) << button;
            }
        });
    }

    // ----- the plate holds every page, and the scene file is what the constants say -----

    TEST_F(MahjongRulesTest, EveryPageLiesInsideThePlateWithTheGapsOfTheAnatomyAndNoBlockOverlapsAnother) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        const UIElement* plate = Element("Rules", "Plate");
        ASSERT_NE(plate, nullptr);

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ASSERT_TRUE(_game->SetLanguage(language));
            for (int page = 0; page < RulesPageCount(); ++page) {
                SCOPED_TRACE(page);
                ASSERT_NO_FATAL_FAILURE(GoToPage(page));
                const RulesPage expected = FormatRulesPage(CoreManager::GetUIStrings(), page);
                const Rect& inner = plate->rect;
                const std::vector<const UIElement*> blocks = ShownBlocks();
                ASSERT_GE(blocks.size(), 2u) << "at least the title and the pager";
                EXPECT_EQ(Element("Rules", "Title"), blocks[0]);
                EXPECT_EQ(Element("Rules", "Pager"), blocks[1]);

                EXPECT_EQ(blocks.front()->rect.y, inner.y + kModalPadding) << "the padding of the plate";
                for (size_t i = 0; i < blocks.size(); ++i) {
                    const Rect& rect = blocks[i]->rect;
                    EXPECT_EQ(rect.x, inner.x + kModalPadding) << i;
                    EXPECT_EQ(rect.w, inner.w - 2.0f * kModalPadding) << i;
                    if (i > 0) {
                        EXPECT_EQ(GapBelow(blocks[i - 1]->rect, rect), kModalGap) << "block " << i << ": the gap of the plate";
                        EXPECT_FALSE(Intersects(blocks[i - 1]->rect, rect)) << "block " << i << " overlaps the one above";
                    }
                }
                const Rect& last = blocks.back()->rect;
                EXPECT_LE(last.y + last.h, inner.y + inner.h - kModalPadding + 0.01f) << "the last block ends inside the padding";
                // `PageHeight` is the height the scene really gives the page
                EXPECT_EQ(last.y + last.h - blocks.front()->rect.y, static_cast<float>(PageHeight(expected)));
                EXPECT_LE(PageHeight(expected), kRulesInnerHeight);

                // What a block holds lies in it, side by side without overlap
                for (const UIElement* block : blocks) {
                    const UIElement* previous = nullptr;
                    for (const auto& child : block->children) {
                        if (child->rect.IsEmpty()) {
                            continue;
                        }
                        EXPECT_GE(child->rect.x, block->rect.x - 0.01f) << child->name.ToStringView().data();
                        EXPECT_GE(child->rect.y, block->rect.y - 0.01f) << child->name.ToStringView().data();
                        EXPECT_LE(child->rect.x + child->rect.w, block->rect.x + block->rect.w + 0.01f) << child->name.ToStringView().data();
                        EXPECT_LE(child->rect.y + child->rect.h, block->rect.y + block->rect.h + 0.01f) << child->name.ToStringView().data();
                        if (previous != nullptr) {
                            EXPECT_FALSE(Intersects(previous->rect, child->rect)) << child->name.ToStringView().data() << " overlaps " << previous->name.ToStringView().data();
                        }
                        previous = child.Get();
                    }
                }
            }
        });
    }

    TEST_F(MahjongRulesTest, TheSceneFileHasExactlyTheElementsAndTheSizesTheConstantsSay) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        auto canvas = Canvas("Rules");
        ASSERT_TRUE(canvas);
        const auto count = [&](const char* prefix) {
            int found = 0;
            while (Element("Rules", Format("{}{}", prefix, found).c_str()) != nullptr) {
                ++found;
            }
            return found;
        };
        EXPECT_EQ(count("RulesLine"), kRulesMaxLines);
        EXPECT_EQ(count("RulesItem"), kRulesMaxItems) << "RulesItemRow0 does not match `RulesItem<digit>`, so this counts the labels";
        EXPECT_EQ(count("RulesItemRow"), kRulesMaxItems / kRulesItemColumns);
        EXPECT_EQ(count("RulesTileRow"), kRulesMaxTileRows);
        EXPECT_EQ(count("RulesTile"), kRulesMaxTileRows * kRulesMaxRowTiles);

        // Everything shown at once, to read the sizes the file gives
        for (const char* prefix : {"RulesLine", "RulesItemRow", "RulesItem", "RulesTileRow", "RulesTile"}) {
            for (int i = 0; Element("Rules", Format("{}{}", prefix, i).c_str()) != nullptr; ++i) {
                ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(Format("{}{}", prefix, i)), true));
            }
        }
        canvas->UpdateLayout(kViewWidth, kViewHeight);
        const UIElement* plate = Element("Rules", "Plate");
        ASSERT_NE(plate, nullptr);
        EXPECT_EQ(plate->rect.h, static_cast<float>(kRulesPlateHeight));
        EXPECT_EQ(kRulesPlatePadding, 16);
        EXPECT_EQ(kRulesBlockGap, 8);
        EXPECT_EQ(Element("Rules", "Title")->rect.h, static_cast<float>(kRulesTitleHeight));
        EXPECT_EQ(Element("Rules", "Pager")->rect.h, static_cast<float>(kRulesPagerHeight));
        EXPECT_EQ(Element("Rules", "RulesLine0")->rect.h, static_cast<float>(kRulesLineHeight));
        EXPECT_EQ(Element("Rules", "RulesItem0")->rect.h, static_cast<float>(kRulesItemHeight));
        EXPECT_EQ(Element("Rules", "RulesTile0")->rect.w, static_cast<float>(kRulesTileWidth));
        EXPECT_EQ(Element("Rules", "RulesTile0")->rect.h, static_cast<float>(kRulesTileHeight));
        EXPECT_EQ(Element("Rules", "RulesTileRow0")->rect.h, static_cast<float>(kRulesTileHeight));
        EXPECT_EQ(Element("Rules", "RulesItemRow0")->rect.h, static_cast<float>(kRulesItemHeight));
        // The fourteen tiles of a row fit its width at the widest line of the plate
        const UIElement* row = Element("Rules", "RulesTileRow0");
        for (int column = 0; column < kRulesMaxRowTiles; ++column) {
            const UIElement* tile = Element("Rules", Format("RulesTile{}", column).c_str());
            ASSERT_NE(tile, nullptr);
            EXPECT_LE(tile->rect.x + tile->rect.w, row->rect.x + row->rect.w + 0.01f) << "tile " << column;
        }
    }

    // ----- the anatomy -----

    TEST_F(MahjongRulesTest, ThePlateIsTheModalAnatomyAndTheBarsButtonsKeepTheirRolesAndSizes) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_NO_FATAL_FAILURE(GoToPage(1));
        auto canvas = Canvas("Rules");
        auto bar = Canvas("RulesAction");
        ASSERT_TRUE(canvas && bar);
        EXPECT_EQ(canvas->GetLayout().params.space, CanvasSpace::SceneView);
        EXPECT_EQ(bar->GetLayout().params.space, CanvasSpace::Screen) << "the buttons are on a Screen canvas: 64 units on the view are 24 px in the smallest window";
        EXPECT_GE(bar->GetLayout().params.minScale, 0.6f);
        canvas->UpdateLayout(kViewWidth, kViewHeight);
        bar->UpdateLayout(kViewWidth, kViewHeight);

        const UIElement* plate = Element("Rules", "Plate");
        ASSERT_NE(plate, nullptr);
        EXPECT_TRUE(SameColor(plate->color, PaletteColor("surface")));
        EXPECT_GE(plate->rect.y, kViewMargin);
        EXPECT_LE(plate->rect.y + plate->rect.h, kViewHeight - kViewMargin) << "the plate lies inside the view with a margin";
        EXPECT_GE(plate->rect.x, kViewMargin);
        EXPECT_LE(plate->rect.x + plate->rect.w, kViewWidth - kViewMargin);

        const UIElement* title = Element("Rules", "Title");
        ASSERT_NE(title, nullptr);
        EXPECT_TRUE(SameColor(title->color, PaletteColor("header")));
        EXPECT_EQ(title->rect.h, kModalTitleHeight);
        EXPECT_EQ(title->rect.x, plate->rect.x + kModalPadding);
        EXPECT_EQ(title->rect.y, plate->rect.y + kModalPadding);

        const UIElement* pager = Element("Rules", "Pager");
        ASSERT_NE(pager, nullptr);
        EXPECT_TRUE(SameColor(pager->Cast<UILabel>()->textColor, PaletteColor("textMuted")));
        EXPECT_EQ(GapBelow(title->rect, pager->rect), kModalGap);

        // The dim is the whole view, and it takes the clicks
        const UIElement* dim = Element("Rules", "Dim");
        ASSERT_NE(dim, nullptr);
        EXPECT_TRUE(SameColor(dim->color, PaletteColor("scrim")));
        EXPECT_TRUE(dim->blocksInput);
        EXPECT_EQ(dim->rect.x, 0.0f);
        EXPECT_EQ(dim->rect.y, 0.0f);
        EXPECT_EQ(dim->rect.w, kViewWidth);
        EXPECT_EQ(dim->rect.h, kViewHeight);

        // The bar: the anatomy of the bar of the score table, the three buttons in their roles at the size of a main button
        const UIElement* barElement = Element("RulesAction", "Bar");
        ASSERT_NE(barElement, nullptr);
        EXPECT_TRUE(SameColor(barElement->color, PaletteColor("surface")));
        EXPECT_EQ(barElement->rect.h, kMainButtonHeight + 2.0f * kModalGap);
        EXPECT_GE(GapBelow(plate->rect, barElement->rect), kModalSectionGap) << "the plate ends a section gap above the bar";
        const char* const roles[][2] = {{"RulesBack", "neutral"}, {"RulesNext", "primary"}, {"RulesClose", "neutral"}};
        const UIElement* left = nullptr;
        for (const auto& role : roles) {
            const UIElement* button = Element("RulesAction", role[0]);
            ASSERT_NE(button, nullptr) << role[0];
            EXPECT_TRUE(SameColor(button->color, PaletteColor(role[1]))) << role[0] << " is " << role[1];
            EXPECT_EQ(button->rect.h, kMainButtonHeight) << role[0];
            EXPECT_EQ(button->rect.y - barElement->rect.y, kModalGap) << role[0];
            EXPECT_EQ(barElement->rect.y + barElement->rect.h - (button->rect.y + button->rect.h), kModalGap) << role[0];
            if (left != nullptr) {
                EXPECT_EQ(button->rect.x - (left->rect.x + left->rect.w), kModalGap) << role[0] << ": the gap of 8 between the buttons";
            }
            left = button;
        }
    }

    TEST_F(MahjongRulesTest, ThePlateIsClearOfTheBarInEveryWindowOfTheMatrixWithoutInsets) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        auto plate = Canvas("Rules");
        auto bar = Canvas("RulesAction");
        ASSERT_TRUE(plate && bar);
        size_t checked = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            if (window.HasInsets()) {
                return;
            }
            LayOutInWindow(*plate, window);
            const UIElement* plateElement = Element("Rules", "Plate");
            ASSERT_NE(plateElement, nullptr);
            ASSERT_FALSE(plateElement->rect.IsEmpty()) << window.label.c_str();
            const Rect plateRect = ToView(plateElement->rect, *plate, window);
            LayOutInWindow(*bar, window, CanvasScaleMode::ScaleWithScreen, 1.0f);
            const UIElement* barElement = Element("RulesAction", "Bar");
            const UIElement* buttons = Element("RulesAction", "Buttons");
            ASSERT_TRUE(barElement != nullptr && buttons != nullptr);
            ASSERT_FALSE(barElement->rect.IsEmpty()) << window.label.c_str();
            EXPECT_FALSE(Intersects(ToView(barElement->rect, *bar, window), plateRect)) << window.label.c_str() << ": the bar touches the plate";
            EXPECT_FALSE(Intersects(ToView(buttons->rect, *bar, window), plateRect)) << window.label.c_str() << ": the buttons touch the plate";
            ++checked;
        });
        EXPECT_GT(checked, 0u);
    }

    TEST_F(MahjongRulesTest, TheActionsOfTheFourButtonsAreInTheFile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_NO_FATAL_FAILURE(GoToPage(1));
        const auto action = [&](const char* node, const char* element) {
            const UIElement* found = Element(node, element);
            const UIButton* button = found != nullptr ? found->Cast<UIButton>() : nullptr;
            return button != nullptr ? button->action : PoolString();
        };
        EXPECT_EQ(action("HudMenu", "Rules"), "MahjongRules"_intern);
        EXPECT_EQ(action("RulesAction", "RulesBack"), "MahjongRulesBack"_intern);
        EXPECT_EQ(action("RulesAction", "RulesNext"), "MahjongRulesNext"_intern);
        EXPECT_EQ(action("RulesAction", "RulesClose"), "MahjongRulesClose"_intern);
    }

    TEST_F(MahjongRulesTest, TheTilePicturesTakeNoInputAndThePlateHoldsNoControl) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto plate = Canvas("Rules");
        ASSERT_TRUE(plate);
        std::vector<const UIElement*> controls;
        CollectControls(plate->GetRoot(), controls);
        for (const UIElement* control : controls) {
            EXPECT_FALSE(control->interactive) << control->name.ToStringView().data() << ": the pages are text, the buttons are in RulesAction";
        }
    }

    TEST_F(MahjongRulesTest, ALanguageSwitchToRussianAndBackChangesTheTextOfEveryPage) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        for (int page = 0; page < RulesPageCount(); ++page) {
            SCOPED_TRACE(page);
            ASSERT_TRUE(_game->SetLanguage("en"_intern));
            ASSERT_NO_FATAL_FAILURE(GoToPage(page));
            const RulesPage english = FormatRulesPage(CoreManager::GetUIStrings(), page);
            const std::string englishTitle = Var("rulesTitle");

            ASSERT_TRUE(_game->SetLanguage("ru"_intern));
            Draw();
            const RulesPage russian = FormatRulesPage(CoreManager::GetUIStrings(), page);
            EXPECT_NE(Var("rulesTitle"), englishTitle);
            ASSERT_EQ(russian.lines.size(), english.lines.size());
            ASSERT_EQ(russian.items.size(), english.items.size());
            for (size_t i = 0; i < russian.lines.size(); ++i) {
                EXPECT_NE(russian.lines[i], english.lines[i]) << "line " << i;
            }
            ASSERT_NO_FATAL_FAILURE(ExpectPageShown(page));
        }
        ASSERT_TRUE(_game->SetLanguage("en"_intern));
    }

    TEST_F(MahjongRulesTest, WhileThePagesAreOpenTheButtonsOfTheHudTakeNoClick) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_NO_FATAL_FAILURE(PressRules());
        ASSERT_NO_FATAL_FAILURE(PressBar("RulesNext"));
        ASSERT_EQ(Hud()->CurrentRulesPage(), 1);
        ASSERT_FALSE(MenuShown()) << "HudMenu is hidden meanwhile";

        // The buttons keep the place they had; a click there goes to the dim of the pages, which takes it
        EXPECT_TRUE(ClickElement("HudMenu", "NewGame")) << "the dim of the pages takes the click";
        Draw();
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_TRUE(ClickElement("HudMenu", "Rules"));
        Draw();
        EXPECT_EQ(Hud()->CurrentRulesPage(), 1) << "the Rules button was not pressed again (it would have opened the first page)";
        EXPECT_TRUE(_game->IsRulesOpen());
    }

}  // namespace BECore::Mahjong::Tests
