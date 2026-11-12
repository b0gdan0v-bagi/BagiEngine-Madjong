#include "CoachCardTestSupport.h"
#include "ComputerTestSupport.h"
#include "MahjongButtonSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UIImage.h>
#include <Mahjong/Components/MahjongHintsComponent.h>
#include <Mahjong/Game/ComputerExplain.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Table/Hints.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The hints of the shipped scene (`Components/MahjongHintsComponent.h`, `Table/Hints.h`): the switch `Hints` On | Off of the page
 * `Options`, which the component answers and the game keeps (`MahjongGame::HintsOn()`), and the waiting row in the top left corner. Fixed
 * hands (`FixedWall.h`) are laid into the hand in progress; the pure helper has its own test (`HintsTest`).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kMinControlHeight = 48.0f;
        constexpr float kButtonAir = 8.0f;
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}};

        // The hands of the waiting row (the same shapes as in `HintsTest`); East is the person at seat 0, holds the 13 tiles and draws one more.
        constexpr const char* kSingleWait = "1s 2s 3s 4s 5s 6s 7m 8m 9m 2p 3p 4p Ew";    // waits on Ew
        constexpr const char* kTwoSidedWait = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew";  // waits on 4p and 7p
        constexpr const char* kOrphansWait = "1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd";   // waits on all thirteen
        constexpr const char* kPlainEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";     // far from waiting
        constexpr int kWaitCells = kMaxWaitingTiles;
        constexpr int kMostDiscards = 100;

        /// The worst-case table, as `MahjongTableScreenTest` lays it out: every zone at its capacity.
        TableInput WorstCaseInput() {
            TableInput worst;
            worst.viewer = 0;
            worst.concealedCount.fill(kMaxConcealedTiles);
            worst.bonusCount.fill(kMaxBonusTiles);
            Meld kong;
            kong.kind = MeldKind::Kong;
            kong.first = T("1p");
            for (auto& melds : worst.melds) {
                melds.assign(kMaxMelds, kong);
            }
            for (int i = 0; i < kMostDiscards; ++i) {
                worst.discardSeats.push_back(i % kSeatCount);
            }
            worst.wallRemaining = 2 * kWallRingCells;
            return worst;
        }

        /// What the canvas shows: the notation of each tile of a shown cell and its count text.
        using Row = std::vector<std::pair<std::string, std::string>>;

    }  // namespace

    class MahjongHintsTest : public MahjongTableTestBase {
    protected:
        static std::string HintsChoiceVar() {
            return Var("hintsChoice");
        }

        bool Selected(const char* segment) const {
            const UIElement* element = Element("Options", segment);
            const auto* button = element != nullptr ? element : nullptr;
            return button != nullptr && button->selected;
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }

        bool OptionsShown() const {
            const auto canvas = Canvas("Options");
            return canvas && canvas->TakesInput();
        }

        /// A match with the people of `seats`, then East (seat 0) holding `waiting` and drawing `drawn`; `liveAfterDeal` shortens the wall.
        void StartEast(const char* waiting, const char* drawn, const SeatSetup& seats = kDefaultSeatSetup, std::optional<int> liveAfterDeal = std::nullopt) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            FixedWallSpec spec = SpecOf(waiting, drawn);
            spec.liveAfterDeal = liveAfterDeal;
            Lay(MakeModel(spec));
            Draw();
        }

        /// The model was changed behind the session's back (`Lay`), which the component cannot see: a flip of the flag makes it look again.
        void Refresh() {
            const bool flag = _game->HintsOn();
            _game->SetHintsOn(!flag);
            Draw();
            _game->SetHintsOn(flag);
            Draw();
        }

        bool Discard(const char* tile, bool declare = false) {
            return _game->GetSession().PerformPerson(HandAction::MakeDiscard(T(tile), declare)).has_value();
        }

        static std::string CellName(const char* prefix, int index) {
            return std::string(prefix) + std::to_string(index);
        }

        /// The cells that are on screen, in order.
        Row ShownRow() const {
            Row row;
            for (int i = 0; i < kWaitCells; ++i) {
                if (!OnScreen("Hints", CellName("WaitCell", i).c_str())) {
                    continue;
                }
                const auto* image = Element("Hints", CellName("WaitTile", i).c_str())->Cast<UIImage>();
                const std::string sprite = image != nullptr ? std::string(image->sprite.ToStringView().data(), image->sprite.ToStringView().size()) : "<none>";
                row.emplace_back(sprite.substr(sprite.find('_') + 1), Var(CellName("hintCount", i).c_str()));
            }
            return row;
        }

        /// The row the pure helper gives for the viewer's own view now.
        Row ExpectedRow(Seat viewer) const {
            Row row;
            for (const WaitEntry& entry : WaitingTiles(MakeTableView(Hand(), viewer))) {
                row.emplace_back(std::string(ToString(entry.tile)), std::to_string(entry.unseen));
            }
            return row;
        }

        bool PanelShown() const {
            return OnScreen("Hints", "WaitPanel");
        }

        /// The button on the table: the canvas `HintsButton` is the one on screen (a hidden canvas keeps the rects of its last layout) and the
        /// button has a rect.
        bool ButtonShown() const {
            const auto canvas = Canvas("HintsButton");
            return canvas && canvas->TakesInput() && OnScreen("HintsButton", "HintButton");
        }

        /// The button in the row of `ActionsCompact`, the canvas of the narrow windows.
        bool CompactButtonShown() const {
            const auto canvas = Canvas("ActionsCompact");
            return canvas && canvas->TakesInput() && OnScreen("ActionsCompact", "HintButton");
        }

        bool AdviceShown() const {
            return OnScreen("Hints", "AdviceText");
        }

        /// The rectangles drawn in `color`, in the order of the draw calls.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && call.color.r == color.r && call.color.g == color.g && call.color.b == color.b && call.color.a == color.a) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        /// The marks under the hand: for each slot of the viewer's hand whose tile is in `tiles`, the bar the component should draw.
        std::vector<Rect> ExpectedBars(const eastl::array<bool, kTileIdCount>& tiles) const {
            std::vector<Rect> bars;
            const PlayerHand& held = Hand().GetHand(_table->GetLayout().viewer);
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == SlotRole::HandTile && slot.seat == _table->GetLayout().viewer && tiles[held.concealed[slot.index].Id()]) {
                    bars.push_back(Rect(slot.rect.x, slot.rect.y + slot.rect.h, slot.rect.w, 0.0f));  // the bar starts below the tile
                }
            }
            return bars;
        }

        /// The view of the viewer (or of `seat`) now.
        TableView ViewOf(Seat seat) const {
            return MakeTableView(Hand(), seat);
        }

        void Press() {
            EmitAction("MahjongHintAdvice");
            Draw();
        }

        std::string AdviceVar() const {
            return Var("hintAdvice");
        }

        /// The sentence the pure helper says for `seat`'s view now, in the current language.
        std::string SentenceFor(Seat seat) const {
            const TableView view = ViewOf(seat);
            const std::optional<HandAction> advice = AdviceFor(view);
            const eastl::string text = advice ? AdviceText(CoreManager::GetUIStrings(), view, *advice) : eastl::string();
            return std::string(text.data(), text.size());
        }

        /// A match where the person at seat 1 is asked about a 5p that East discarded; `seat1` is its hand, East holds a plain hand.
        void StartWindow(const char* seat1, const char* discard) {
            constexpr SeatSetup personAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
            ASSERT_NO_FATAL_FAILURE(Start(personAtSeat1));
            FixedWallSpec spec = SpecOf(kPlainEast, discard);
            spec.hands[kSeat1] = seat1;
            Lay(MakeModel(spec));
            ASSERT_TRUE(_game->GetMatch().Hand().Discard(kSeat0, T(discard)).has_value());
            Refresh();
            ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
            ASSERT_EQ(_game->GetSession().Actor(), kSeat1);
        }

        /// Variable `index` of the card of the coach (`CardVarNames`: 0 the title, 1 the head, then the cells of the rows and the lines under them).
        std::string WhyVar(size_t index) const {
            return CardVars()[index];
        }

        bool WhyShown() const {
            return OnScreen("HintsWhy", "CoachCard");
        }

        /// The variables of the card: what `MakeAdviceCard` says for `seat`'s view now, in the current language, with the tally of the game.
        std::vector<std::string> ExpectedWhy(Seat seat) const {
            const TableView view = ViewOf(seat);
            const std::optional<Explanation> explained = ExplainAdviceFor(view);
            if (!explained.has_value()) {
                return CardTexts(HintCard{});
            }
            const UIStrings& strings = CoreManager::GetUIStrings();
            const TableSession& session = _game->GetSession();
            const eastl::string tally = CardTallyText(strings, _game->GetCoach().HandTallyOf(HandKeyOf(session)), _game->GetCoach().MatchTallyOf(session.StartCount()));
            return CardTexts(MakeAdviceCard(strings, view, *explained, tally));
        }

        std::vector<std::string> WhyVars() const {
            return CardVars();
        }

        bool NoWhyText() const {
            return NoCardText();
        }

        /// The card open: the mode Coach and the switch on, on the table as it is.
        void OpenWhy() {
            _game->SetHintsMode(HintsMode::Coach);
            Draw();
            if (!WhyShown()) {
                Press();  // the switch may be on from an earlier table of the test
            }
            ASSERT_TRUE(WhyShown());
            ASSERT_FALSE(AdviceShown()) << "in Coach the sentence is the title of the card";
        }

        /// The panel open and drawn on the game as it is.
        void OpenPanel() {
            _game->OpenSetup();
            Draw();
            ASSERT_TRUE(SetupShown());
        }

        /// The page `Options` (where the hints row is) open over the table, as the HUD button opens it, and drawn on the game as it is.
        void OpenPage() {
            _game->CloseSetup();
            _game->OpenOptions();
            Draw();
            ASSERT_TRUE(OptionsShown());
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

    // ----- the page shows the flag -----

    TEST_F(MahjongHintsTest, ThePageOpensOnTheFlagOfTheGameOn) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        EXPECT_EQ(HintsChoiceVar(), "hints");
        EXPECT_TRUE(Selected("HintsHints"));
        EXPECT_FALSE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, ThePageOpensOnTheFlagOfTheGameOff) {
        _game->SetHintsOn(false);
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        EXPECT_EQ(HintsChoiceVar(), "off");
        EXPECT_FALSE(Selected("HintsHints"));
        EXPECT_TRUE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, ThePageOpensOnTheCoachAndTheThreeModesApplyTheHintsExceptOff) {
        _game->SetHintsMode(HintsMode::Coach);
        EXPECT_TRUE(_game->HintsOn());
        EXPECT_TRUE(_game->CoachOn());
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        EXPECT_EQ(HintsChoiceVar(), "coach");
        EXPECT_TRUE(Selected("HintsCoach"));
        EXPECT_FALSE(Selected("HintsHints"));
        EXPECT_FALSE(Selected("HintsOff"));
        for (const HintsMode mode : {HintsMode::Off, HintsMode::Hints, HintsMode::Coach}) {
            _game->SetHintsMode(mode);
            EXPECT_EQ(HintsApply(_game->HintsOn(), false, _game->GetSession()), mode != HintsMode::Off) << HintsModeText(mode).data();
            EXPECT_EQ(_game->GetCoach().Enabled(), mode == HintsMode::Coach);
            EXPECT_EQ(_game->CoachOn(), mode == HintsMode::Coach);
        }
        _game->SetHintsOn(true);
        EXPECT_EQ(_game->GetHintsMode(), HintsMode::Hints) << "SetHintsOn(true) is Hints";
        _game->SetHintsOn(false);
        EXPECT_EQ(_game->GetHintsMode(), HintsMode::Off);
    }

    TEST_F(MahjongHintsTest, ThePageHasTheRowWithTheWordsOfBothLanguages) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            const eastl::string on = Tr(CoreManager::GetUIStrings(), "options.hintsHints");
            const eastl::string coach = Tr(CoreManager::GetUIStrings(), "options.hintsCoach");
            const eastl::string off = Tr(CoreManager::GetUIStrings(), "setup.off");
            const eastl::string caption = Tr(CoreManager::GetUIStrings(), "setup.hints");
            const auto text = [&](const char* element) {
                const auto* label = Element("Options", element) != nullptr ? Element("Options", element)->Cast<UILabel>() : nullptr;
                return label != nullptr ? std::string(label->GetDisplayText().data(), label->GetDisplayText().size()) : std::string("<none>");
            };
            const auto buttonText = [&](const char* element) { return DisplayText(Element("Options", element), "<none>"); };
            EXPECT_EQ(text("HintsCaption"), std::string(caption.data(), caption.size()));
            EXPECT_EQ(buttonText("HintsHints"), std::string(on.data(), on.size()));
            EXPECT_EQ(buttonText("HintsOff"), std::string(off.data(), off.size()));
            EXPECT_EQ(buttonText("HintsCoach"), std::string(coach.data(), coach.size()));
        });
    }

    // ----- the clicks -----

    TEST_F(MahjongHintsTest, AClickOnASegmentChangesTheGameAndTheVariableAtOnce) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());

        ASSERT_TRUE(ClickElement("Options", "HintsOff"));
        EXPECT_FALSE(_game->HintsOn()) << "no Start needed";
        EXPECT_EQ(HintsChoiceVar(), "off");
        Draw();
        EXPECT_TRUE(Selected("HintsOff"));
        EXPECT_FALSE(Selected("HintsHints"));
        EXPECT_TRUE(OptionsShown()) << "the page stays open";

        ASSERT_TRUE(ClickElement("Options", "HintsHints"));
        EXPECT_TRUE(_game->HintsOn());
        EXPECT_EQ(HintsChoiceVar(), "hints");
        Draw();
        EXPECT_TRUE(Selected("HintsHints"));
        EXPECT_FALSE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, AClickChangesNeitherTheMatchNorThePendingSetup) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        const uint64_t version = _game->GetSession().Version();
        const SeatSetup seats = _game->GetSeats();
        const int hands = _game->GetSettings().handsPerMatch;

        ASSERT_TRUE(ClickElement("Options", "HintsOff"));
        Draw();

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetSeats(), seats);
        EXPECT_EQ(_game->GetSettings().handsPerMatch, hands);
        EXPECT_FALSE(_game->IsSetupOpen());
    }

    TEST_F(MahjongHintsTest, AnyOtherValueAndAClosedPageChangeNothing) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());

        for (const char* value : {"", "ON", "Off", "on", "Coach", "hints ", "1", "0", "true", "false", "on ", " off", "yes", "no"}) {
            EmitAction("MahjongSetHints", value);
            EXPECT_TRUE(_game->HintsOn()) << "'" << value << "'";
            EXPECT_EQ(HintsChoiceVar(), "hints") << "'" << value << "'";
        }

        _game->CloseOptions();
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());
        ASSERT_FALSE(OptionsShown());
        EmitAction("MahjongSetHints", "off");
        EXPECT_TRUE(_game->HintsOn()) << "Setup has no hints row";
        EXPECT_EQ(HintsChoiceVar(), "hints");

        _game->CloseSetup();
        Draw();
        ASSERT_FALSE(SetupShown());
        EmitAction("MahjongSetHints", "off");
        EXPECT_TRUE(_game->HintsOn()) << "both panels are closed";
        EXPECT_EQ(HintsChoiceVar(), "hints");
    }

    TEST_F(MahjongHintsTest, TheVariableKeepsTheTruthWhenTheGameChangesTheFlag) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ASSERT_EQ(HintsChoiceVar(), "hints");

        _game->SetHintsOn(false);  // not through the page
        Draw();

        EXPECT_EQ(HintsChoiceVar(), "off");
        EXPECT_TRUE(Selected("HintsOff"));
    }

    TEST_F(MahjongHintsTest, AnUnchangedPageSetsNothingAgain) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        const uint64_t version = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    // ----- the choice is kept -----

    TEST_F(MahjongHintsTest, TheChoiceIsKeptByStartNewGameResetAndAControlReset) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ASSERT_TRUE(ClickElement("Options", "HintsOff"));
        ASSERT_TRUE(ClickElement("Options", "Back"));
        Draw();
        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();
        ASSERT_FALSE(SetupShown());
        EXPECT_FALSE(_game->HintsOn()) << "Start";

        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
        Draw();
        ASSERT_TRUE(SetupShown());
        EXPECT_FALSE(_game->HintsOn()) << "New game";
        _game->CloseSetup();
        _game->OpenOptions();
        Draw();
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
        ASSERT_TRUE(ClickElement("HudMenu", "Options"));
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

    // ----- the page with the row -----

    TEST_F(MahjongHintsTest, ThePagePassesTheSizeMatrixAndEveryWordFitsInEveryLanguage) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ExpectPassesTheMatrix("Options", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Options", output.width, output.height, kButtonAir);
            }
            for (const char* segmentName : {"HintsOff", "HintsHints", "HintsCoach"}) {
                const UIElement* segment = Element("Options", segmentName);
                ASSERT_NE(segment, nullptr) << segmentName;
                EXPECT_GE(segment->rect.h, kMinControlHeight) << segmentName;
            }
        });
    }

    TEST_F(MahjongHintsTest, ThePlateHoldsEveryRowOfThePage) {
        _game->Start();
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        const UIElement* plate = Element("Options", "Plate");
        ASSERT_NE(plate, nullptr);
        for (const char* name : {"Title", "LanguageRow", "HintsRow", "SpeedRow", "Back"}) {
            const UIElement* row = Element("Options", name);
            ASSERT_NE(row, nullptr) << name;
            EXPECT_GE(row->rect.y, plate->rect.y) << name;
            EXPECT_LE(row->rect.y + row->rect.h, plate->rect.y + plate->rect.h) << name;
        }
        const UIElement* hints = Element("Options", "HintsRow");
        const UIElement* language = Element("Options", "LanguageRow");
        const UIElement* back = Element("Options", "Back");
        EXPECT_GE(hints->rect.y, language->rect.y + language->rect.h) << "the row lies after LanguageRow";
        EXPECT_LE(hints->rect.y + hints->rect.h, back->rect.y) << "and before Back";
    }

    // ----- the waiting row -----

    TEST_F(MahjongHintsTest, TheRowIsHiddenInTheFileAndWhileTheHandHasFourteenTiles) {
        EXPECT_FALSE(PanelShown());
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        EXPECT_FALSE(PanelShown()) << "14 tiles in the hand: nothing waits yet";
        EXPECT_TRUE(ShownRow().empty());
        for (int i = 0; i < kWaitCells; ++i) {
            EXPECT_FALSE(OnScreen("Hints", CellName("WaitCell", i).c_str())) << i;
        }
    }

    TEST_F(MahjongHintsTest, ASingleWaitShowsOneCellWithItsUnseenCopies) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        ASSERT_TRUE(Discard("9p"));
        Draw();

        EXPECT_TRUE(PanelShown());
        EXPECT_TRUE(OnScreen("Hints", "WaitCaption"));
        EXPECT_EQ(ShownRow(), (Row{{"Ew", "3"}})) << "the viewer holds one of the four east winds";
        EXPECT_EQ(ShownRow(), ExpectedRow(kSeat0));
    }

    TEST_F(MahjongHintsTest, ATwoSidedWaitShowsBothTilesAndTheCountsFollowThePile) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kTwoSidedWait, "4p"));
        ASSERT_TRUE(Discard("4p"));
        Draw();

        // The 4p lies in the pile: three unseen copies of it, four of the 7p
        EXPECT_EQ(ShownRow(), (Row{{"4p", "3"}, {"7p", "4"}}));
        EXPECT_EQ(ShownRow(), ExpectedRow(kSeat0));
    }

    TEST_F(MahjongHintsTest, ThirteenOrphansShowThirteenCellsInTheOrderOfTheIds) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kOrphansWait, "Ew"));
        ASSERT_TRUE(Discard("Ew"));
        Draw();

        const Row row = ShownRow();
        ASSERT_EQ(row.size(), static_cast<size_t>(kWaitCells));
        int previous = -1;
        for (const auto& [tile, count] : row) {
            const std::optional<Tile> parsed = TryParse(tile);
            ASSERT_TRUE(parsed.has_value()) << tile;
            EXPECT_GT(static_cast<int>(parsed->Id()), previous) << "ascending ids";
            previous = parsed->Id();
            EXPECT_EQ(count, tile == "Ew" ? "2" : "3") << tile << ": the east wind lies in the pile too";
        }
        EXPECT_EQ(row, ExpectedRow(kSeat0));
    }

    TEST_F(MahjongHintsTest, TheRowShowsDuringTheComputersTurnsAndAfterTheDeclaration) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        ASSERT_TRUE(Discard("9p", true)) << "the 13 that stay wait: the calling hand is declared";
        Draw();
        ASSERT_FALSE(_game->GetSession().ActorIsPerson()) << "a computer acts now";
        EXPECT_EQ(ShownRow(), (Row{{"Ew", "3"}}));

        // The computers play on; the row is whatever the pure helper says for the viewer after each step
        for (int step = 0; step < 4 && _game->GetSession().GetPhase() == SessionPhase::Playing && !_game->GetSession().ActorIsPerson(); ++step) {
            ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
            Draw();
            EXPECT_EQ(ShownRow(), ExpectedRow(kSeat0)) << "step " << step;
        }
    }

    TEST_F(MahjongHintsTest, NothingShowsWithHintsOffWhileSetupIsOpenOrForASpectator) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        ASSERT_TRUE(Discard("9p"));
        Draw();
        ASSERT_EQ(ShownRow().size(), 1u);

        _game->SetHintsOn(false);
        Draw();
        EXPECT_FALSE(PanelShown()) << "hints off";
        EXPECT_TRUE(ShownRow().empty());
        _game->SetHintsOn(true);
        Draw();
        EXPECT_EQ(ShownRow().size(), 1u) << "and back on";

        _game->OpenSetup();
        Draw();
        EXPECT_FALSE(PanelShown()) << "the seat choice is open";
        _game->CloseSetup();
        Draw();
        EXPECT_EQ(ShownRow().size(), 1u);

        // A spectator: four computers, no viewer, so no hint however the hand stands
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p", kAllComputers));
        ASSERT_TRUE(_game->GetSession().Viewer() == kNoSeat);
        EXPECT_TRUE(_game->GetMatch().Hand().Discard(kSeat0, T("9p")).has_value());
        Refresh();
        EXPECT_EQ(WaitingTiles(MakeTableView(Hand(), kSeat0)).size(), 1u) << "the hand does wait";
        EXPECT_FALSE(PanelShown());
    }

    TEST_F(MahjongHintsTest, UnderTheCoverNothingShowsAndTheRowIsTheViewersOwn) {
        const SeatSetup twoPeople{SeatKind::Person, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p", twoPeople));
        ASSERT_TRUE(_game->GetSession().HandoffPending()) << "two people: the screen waits for East";
        EXPECT_FALSE(PanelShown());
        _game->GetSession().Reveal();
        ASSERT_TRUE(Discard("9p"));
        Draw();
        EXPECT_EQ(ShownRow(), (Row{{"Ew", "3"}}));

        // The computer of seat 1 plays, then seat 2 (a person) is to act: the screen waits for that person
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
        Draw();
        ASSERT_TRUE(_game->GetSession().HandoffPending());
        EXPECT_TRUE(CoverShown());
        EXPECT_FALSE(PanelShown()) << "the row is the first person's and the cover is up";

        _game->GetSession().Reveal();
        Draw();
        ASSERT_FALSE(_game->GetSession().HandoffPending());
        ASSERT_EQ(_game->GetSession().Viewer(), kSeat2);
        EXPECT_EQ(ShownRow(), ExpectedRow(kSeat2)) << "the row of the person who looks, not of seat 0";
        EXPECT_NE(ShownRow(), (Row{{"Ew", "3"}}));
    }

    TEST_F(MahjongHintsTest, NothingShowsWhenTheHandOrTheMatchIsOver) {
        // A short live wall: East's discard ends the hand as drawn while its 13 tiles wait
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p", kDefaultSeatSetup, 1));
        ASSERT_TRUE(Discard("9p"));
        Draw();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        EXPECT_EQ(WaitingTiles(MakeTableView(Hand(), kSeat0)).size(), 1u) << "the hand does wait";
        EXPECT_FALSE(PanelShown());

        // A whole match played to its end by a person who takes the first listed action
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        TableSession& session = _game->GetSession();
        for (int step = 0; step < 20000 && session.GetPhase() != SessionPhase::MatchOver; ++step) {
            if (session.GetPhase() == SessionPhase::HandOver) {
                ASSERT_TRUE(session.NextHand().has_value());
            } else if (session.ActorIsPerson()) {
                const auto legal = Hand().LegalActions(session.Actor());
                ASSERT_FALSE(legal.empty());
                ASSERT_TRUE(session.PerformPerson(legal.front()).has_value());
            } else {
                ASSERT_TRUE(session.StepComputer().has_value());
            }
        }
        ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);
        Draw();
        EXPECT_FALSE(PanelShown());
    }

    TEST_F(MahjongHintsTest, TheRowFollowsTheTableThroughWholeHands) {
        // The screen shows what the pure helper says for the viewer after every action, and nothing when hints do not apply
        int framesWithRow = 0;
        for (const uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
            SCOPED_TRACE(seed);
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup, seed));
            TableSession& session = _game->GetSession();
            ComputerPlayer person(seed);  // the person plays as the computer would, so that its hand waits in many frames
            for (int step = 0; step < kActionLimit && session.GetPhase() == SessionPhase::Playing; ++step) {
                Draw();
                if (HintsApply(true, false, session)) {
                    EXPECT_EQ(ShownRow(), ExpectedRow(session.Viewer())) << "step " << step;
                    framesWithRow += ShownRow().empty() ? 0 : 1;
                } else {
                    EXPECT_TRUE(ShownRow().empty()) << "step " << step;
                }
                if (session.ActorIsPerson()) {
                    const std::optional<HandAction> action = person.Decide(Hand(), session.Actor());
                    ASSERT_TRUE(action.has_value());
                    ASSERT_TRUE(session.PerformPerson(*action).has_value());
                } else {
                    ASSERT_TRUE(session.StepComputer().has_value());
                }
            }
        }
        EXPECT_GT(framesWithRow, 0) << "a waiting hand is on screen at some point of the hands";
    }

    TEST_F(MahjongHintsTest, NoVariableHoldsAnythingOfAnotherSeat) {
        // Two tables that differ only in the hands of the other seats and in the live wall after the viewer's draw
        const auto run = [&](const char* seat1, const char* seat2, const char* seat3, const std::string& live) {
            Row row;
            std::vector<std::string> counts;
            [&] {
                ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
                FixedWallSpec spec;
                spec.hands[kSeat0] = kTwoSidedWait;
                spec.hands[kSeat1] = seat1;
                spec.hands[kSeat2] = seat2;
                spec.hands[kSeat3] = seat3;
                spec.live = live;
                Lay(MakeModel(spec));
                Draw();
                ASSERT_TRUE(Discard("4p"));
                Draw();
                row = ShownRow();
                for (int i = 0; i < kWaitCells; ++i) {
                    counts.push_back(Var(CellName("hintCount", i).c_str()));
                }
            }();
            return std::make_pair(row, counts);
        };
        const auto plain = run(kQuiet1, kQuiet2, kQuiet3, "4p Rd Gd Wd");
        // Seat 1 holds three of the 7p and seat 3 two of the 4p: hidden from the viewer, so nothing may change
        const auto loaded = run("7p 7p 7p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", kQuiet2, "4p 4p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd Rd", "4p Gd Rd Wd");
        ASSERT_EQ(plain.first.size(), 2u);
        EXPECT_EQ(plain.first, loaded.first);
        EXPECT_EQ(plain.second, loaded.second);
    }

    TEST_F(MahjongHintsTest, AnUnchangedTableSetsNothingAgain) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kTwoSidedWait, "4p"));
        ASSERT_TRUE(Discard("4p"));
        Draw();
        ASSERT_EQ(ShownRow().size(), 2u);
        const uint64_t version = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    TEST_F(MahjongHintsTest, TheCaptionFollowsTheLanguage) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        ASSERT_TRUE(Discard("9p"));
        Draw();
        std::vector<std::string> seen;
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            const eastl::string expected = Tr(CoreManager::GetUIStrings(), "hints.waitCaption");
            const auto* caption = Element("Hints", "WaitCaption") != nullptr ? Element("Hints", "WaitCaption")->Cast<UILabel>() : nullptr;
            ASSERT_NE(caption, nullptr);
            EXPECT_EQ(std::string(caption->GetDisplayText().data(), caption->GetDisplayText().size()), std::string(expected.data(), expected.size()));
            seen.emplace_back(expected.data(), expected.size());
        });
        ASSERT_GE(seen.size(), 2u);
        EXPECT_NE(seen[0], seen[1]);
    }

    // ----- the geometry of the canvas -----

    namespace {

        /// The corner of the table the hints live in (`config/nodes/MahjongHints.xml`): the waiting row and the strip under it, left of the top
        /// seat's plate, clear of the centre panel. Design units.
        const Rect kHintsCorner(96.0f, 8.0f, 336.0f, 140.0f);

        /// The band of the portrait table the hints live in: between the viewer's discards and the viewer's seat plate, the width of the view
        /// less the margin of 12 each side, down to the row of the name of the selected tile. View units.
        const Rect kPortraitHintsBand(12.0f, 836.0f, 696.0f, 188.0f);

        /// The corner of the narrow table the hints live in: left of the top seat's hand and melds (x 336), over the left seat's melds (y 180).
        const Rect kNarrowHintsCorner(8.0f, 8.0f, 320.0f, 168.0f);

        /// Where the hints live in the view of this shape: in the wide view the corner of the landscape table, a wing in from the side.
        Rect HintsAreaOf(TableShape shape) {
            switch (shape) {
                case TableShape::Portrait:
                    return kPortraitHintsBand;
                case TableShape::Wide:
                    return Rect(kHintsCorner.x + kWideWingWidth, kHintsCorner.y, kHintsCorner.w, kHintsCorner.h);
                case TableShape::Narrow:
                    return kNarrowHintsCorner;
                default:
                    return kHintsCorner;
            }
        }

        /// The centre panel of the layout of this shape: the inside of the wall ring, which nothing of the hints may touch.
        Rect CenterPanel(TableShape shape = TableShape::Landscape) {
            return MakeTableLayout(TableInput{}, DesignWidthOf(shape), DesignHeightOf(shape)).zones.centerPanel;
        }

        bool Inside(const Rect& inner, const Rect& outer) {
            return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
        }

    }  // namespace

    TEST_F(MahjongHintsTest, TheFullRowPassesTheSizeMatrixAndEveryWordFitsInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kOrphansWait, "Ew"));
        ASSERT_TRUE(Discard("Ew"));
        Draw();
        ASSERT_EQ(ShownRow().size(), static_cast<size_t>(kWaitCells));

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            // The canvas has no interactive element (the button is in `HintsButton`), so the engine's minimum holds in every window
            ExpectPassesTheMatrix("Hints", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Hints", output.width, output.height, 0.0f);
            }
            // The same on the whole matrix, each window against the view of its shape (the portrait variant of the file)
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("Hints"), 0.0f);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

    TEST_F(MahjongHintsTest, ThePanelLiesInTheCornerClearOfTheCentrePanelAndOfEveryTileOfTheWorstCaseTableAndTakesNoInput) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kOrphansWait, "Ew"));
        ASSERT_TRUE(Discard("Ew"));
        Draw();
        ASSERT_EQ(ShownRow().size(), static_cast<size_t>(kWaitCells));
        for (const TableShape shape : {TableShape::Landscape, TableShape::Portrait, TableShape::Wide, TableShape::Narrow}) {
            const MatrixWindow window = DesignWindowOf(shape);
            SCOPED_TRACE(window.label.c_str());
            LayOutOnTheTable({"Hints"}, window);

            const UIElement* panel = Element("Hints", "WaitPanel");
            ASSERT_NE(panel, nullptr);
            EXPECT_TRUE(Inside(panel->rect, HintsAreaOf(shape))) << panel->rect.x << "," << panel->rect.y << " " << panel->rect.w << "x" << panel->rect.h;
            EXPECT_TRUE(Apart(panel->rect, CenterPanel(shape))) << "the waiting row is not on the centre panel";
            for (int i = 0; i < kWaitCells; ++i) {
                for (const char* prefix : {"WaitCell", "WaitTile", "WaitCount"}) {
                    const UIElement* part = Element("Hints", CellName(prefix, i).c_str());
                    ASSERT_NE(part, nullptr) << prefix << i;
                    EXPECT_TRUE(Inside(part->rect, panel->rect)) << prefix << i;
                    EXPECT_FALSE(part->IsHitTestable()) << prefix << i << " takes no input";
                }
            }
            for (const char* name : {"WaitPanel", "WaitCaption", "WaitRow"}) {
                EXPECT_FALSE(Element("Hints", name)->IsHitTestable()) << name;
            }
            EXPECT_FALSE(Click(panel->rect.x + panel->rect.w * 0.5f, panel->rect.y + panel->rect.h * 0.5f)) << "a click on the row is no canvas's";

            // The worst-case layout: no tile of any table lies where the panel does
            const TableLayout layout = MakeTableLayout(WorstCaseInput(), DesignWidthOf(shape), DesignHeightOf(shape));
            for (const TileSlot& slot : layout.slots) {
                EXPECT_TRUE(Apart(slot.rect, panel->rect)) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
            }
        }
    }

    // ----- the marks on the hand -----

    namespace {

        // 123s 456s 789m 5p 5p and three single honours: with a drawn north wind the computer discards it (worked out in `HintsTest`)
        constexpr const char* kBestIsTheNorthWind = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew";
        constexpr const char* kWindowPung = "5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd";  // a pung of the 5p lowers the shanten: claimed
        constexpr const char* kWindowPass = "3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m";  // a pung of the 5m does nothing: passed

    }  // namespace

    TEST_F(MahjongHintsTest, TheMarkedTilesAreExactlyTheDeclaringDiscards) {
        // 123s 456s 789m 234p Ew and a drawn 9p: the 9p goes and the hand waits on the Ew, or the Ew goes and it waits on the 9p
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        const eastl::array<bool, kTileIdCount> declaring = DeclaringDiscards(ViewOf(kSeat0));
        const std::vector<Rect> expected = ExpectedBars(declaring);
        ASSERT_EQ(expected.size(), 2u) << "the 9p and the east wind";

        const std::vector<Rect> bars = RectsOf(kDeclareMark);

        ASSERT_EQ(bars.size(), expected.size());
        for (size_t i = 0; i < bars.size(); ++i) {
            EXPECT_EQ(bars[i].x, expected[i].x) << i;
            EXPECT_EQ(bars[i].w, expected[i].w) << i;
            EXPECT_GE(bars[i].y, expected[i].y) << i << ": under the tile, not over it";
            EXPECT_GT(bars[i].h, 0.0f);
            EXPECT_LE(bars[i].y + bars[i].h, kViewHeight) << i << ": inside the view";
        }
        // The marks are the legal declaring discards of the model and nothing else
        int declaringSlots = 0;
        for (const HandAction& action : Hand().LegalActions(kSeat0)) {
            if (action.kind == ActionKind::Discard && action.declareCalling) {
                EXPECT_TRUE(declaring[action.tile.Id()]);
                ++declaringSlots;
            }
        }
        EXPECT_EQ(declaringSlots, 2);
        EXPECT_TRUE(declaring[T("9p").Id()] && declaring[T("Ew").Id()]);
        // They are drawn over the table: after its last tile
        size_t lastTile = 0;
        size_t firstMark = _renderer->calls.size();
        for (size_t i = 0; i < _renderer->calls.size(); ++i) {
            const DrawCall& call = _renderer->calls[i];
            if (call.kind == DrawKind::Texture && call.texture == _atlasTexture) {
                lastTile = i;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kDeclareMark)) {  // the gold of the turn mark has another alpha
                firstMark = std::min(firstMark, i);
            }
        }
        EXPECT_GT(firstMark, lastTile);
    }

    TEST_F(MahjongHintsTest, TheMarksAreInTheGoldFamilyOfTheTableAndDifferFromEachOther) {
        // The style: a mark on the table is gold and nothing else is (`UiPalette.h`); the two marks differ in alpha, so a test (and an eye) tells them apart
        for (const Color& mark : {kDeclareMark, kAdvisedFrame}) {
            EXPECT_EQ(mark.r, kTurnMark.r);
            EXPECT_EQ(mark.g, kTurnMark.g);
            EXPECT_EQ(mark.b, kTurnMark.b);
        }
        EXPECT_FALSE(SameColor(kDeclareMark, kAdvisedFrame));
        for (const Color& other : {kTurnMark, kPendingFrame, kVeil}) {
            EXPECT_FALSE(SameColor(kDeclareMark, other));
            EXPECT_FALSE(SameColor(kAdvisedFrame, other));
        }
    }

    TEST_F(MahjongHintsTest, AHandThatCannotDeclareHasNoBar) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        EXPECT_TRUE(RectsOf(kDeclareMark).empty()) << "no discard leaves a waiting hand";

        ASSERT_NO_FATAL_FAILURE(StartEast(kPlainEast, "5p"));
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        EXPECT_TRUE(RectsOf(kDeclareMark).empty());
    }

    TEST_F(MahjongHintsTest, NoMarkOffTheViewersTurnAndNoneWhenHintsDoNotApply) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        ASSERT_EQ(RectsOf(kDeclareMark).size(), 2u);

        _game->SetHintsOn(false);
        Draw();
        EXPECT_TRUE(RectsOf(kDeclareMark).empty()) << "hints off";
        _game->SetHintsOn(true);
        Draw();
        ASSERT_EQ(RectsOf(kDeclareMark).size(), 2u);
        _game->OpenSetup();
        Draw();
        EXPECT_TRUE(RectsOf(kDeclareMark).empty()) << "Setup is open";
        _game->CloseSetup();

        // The turn passes to a computer: the 13 tiles wait, but nothing can be discarded
        ASSERT_TRUE(Discard("9p"));
        Draw();
        ASSERT_FALSE(_game->GetSession().ActorIsPerson());
        EXPECT_TRUE(RectsOf(kDeclareMark).empty());
        EXPECT_FALSE(ButtonShown());

        // A spectator
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p", kAllComputers));
        EXPECT_TRUE(RectsOf(kDeclareMark).empty());
        EXPECT_FALSE(ButtonShown());
    }

    TEST_F(MahjongHintsTest, UnderTheCoverNeitherMarksNorButtonShowAndRevealBringsThem) {
        const SeatSetup twoPeople{SeatKind::Person, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p", twoPeople));
        ASSERT_TRUE(_game->GetSession().HandoffPending());
        EXPECT_TRUE(RectsOf(kDeclareMark).empty());
        EXPECT_FALSE(ButtonShown());

        _game->GetSession().Reveal();
        Refresh();
        EXPECT_EQ(RectsOf(kDeclareMark).size(), 2u);
        EXPECT_TRUE(ButtonShown());
    }

    TEST_F(MahjongHintsTest, NoMarkAndNoButtonWhenTheHandIsOver) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p", kDefaultSeatSetup, 1));
        ASSERT_TRUE(Discard("9p", true));
        Draw();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        EXPECT_TRUE(RectsOf(kDeclareMark).empty());
        EXPECT_FALSE(ButtonShown());
    }

    // ----- the button and the advice -----

    TEST_F(MahjongHintsTest, TheButtonIsOfferedExactlyWhileHintsApplyAndAPersonHasActions) {
        EXPECT_FALSE(ButtonShown()) << "hidden in the file";
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        EXPECT_TRUE(ButtonShown());
        EXPECT_FALSE(AdviceShown()) << "nothing is advised before the press";

        _game->SetHintsOn(false);
        Draw();
        EXPECT_FALSE(ButtonShown()) << "hints off";
        _game->SetHintsOn(true);
        Draw();
        EXPECT_TRUE(ButtonShown());
        _game->OpenSetup();
        Draw();
        EXPECT_FALSE(ButtonShown()) << "Setup is open";
        _game->CloseSetup();
        Draw();

        ASSERT_TRUE(Discard("9p"));
        Draw();
        EXPECT_FALSE(ButtonShown()) << "a computer acts";
    }

    TEST_F(MahjongHintsTest, TheButtonIsOfferedInAClaimWindowOfThePerson) {
        ASSERT_NO_FATAL_FAILURE(StartWindow(kWindowPung, "5p"));
        EXPECT_TRUE(ButtonShown());
    }

    // ----- the button in a narrow window: the row of ActionsCompact -----

    TEST_F(MahjongHintsTest, InANarrowWindowTheButtonIsInTheCompactRowAndOfferedExactlyWhereItIsOnTheTable) {
        // The same state is looked at in the design size (the canvas `HintsButton`) and in a small landscape window (the row of `ActionsCompact`, `HintsButton`
        // hidden with `Actions`): the button is on the one and on the other exactly together
        const auto expectBoth = [&](bool offered, const char* state) {
            SetOutputSize(*_renderer, kViewWidth, kViewHeight);
            Draw();
            EXPECT_EQ(ButtonShown(), offered) << state << ": on the table";
            EXPECT_FALSE(CompactButtonShown()) << state << ": the row is hidden at the design size";
            SetOutputSize(*_renderer, kCompactWindowWidth, kCompactWindowHeight);
            Draw();
            EXPECT_EQ(CompactButtonShown(), offered) << state << ": in the row";
            EXPECT_FALSE(ButtonShown()) << state << ": the canvas of the table is hidden in a narrow window";
        };
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        expectBoth(true, "a plain turn");
        // A plain turn: the hint button is the only button of the row
        for (int i = 0; i < kButtonCount; ++i) {
            EXPECT_FALSE(OnScreen("ActionsCompact", SpecOf(static_cast<ButtonKind>(i)).element)) << SpecOf(static_cast<ButtonKind>(i)).element;
        }
        _game->SetHintsOn(false);
        expectBoth(false, "hints off");
        _game->SetHintsOn(true);
        expectBoth(true, "hints on again");
        _game->OpenSetup();
        expectBoth(false, "Setup open");
        _game->CloseSetup();
        expectBoth(true, "Setup closed");
        ASSERT_TRUE(Discard("Nw"));
        expectBoth(false, "a computer acts");

        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw", kAllComputers));
        expectBoth(false, "a spectator");
        ASSERT_NO_FATAL_FAILURE(StartWindow(kWindowPung, "5p"));
        expectBoth(true, "a claim window of the person");
    }

    TEST_F(MahjongHintsTest, AClickOnTheButtonOfTheCompactRowAsksForTheAdviceAndPlaysNothing) {
        SetOutputSize(*_renderer, kCompactWindowWidth, kCompactWindowHeight);
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_TRUE(CompactButtonShown());
        const auto canvas = Canvas("ActionsCompact");
        const UIElement* button = Element("ActionsCompact", "HintButton");
        ASSERT_TRUE(canvas && button);
        const uint64_t version = _game->GetSession().Version();

        const auto [x, y] = OutputCenterOf(*canvas, *button);
        EXPECT_TRUE(Click(x, y)) << "the canvas takes the click";
        Draw();

        EXPECT_TRUE(AdviceShown());
        EXPECT_EQ(AdviceVar(), "Discard North Wind.");
        EXPECT_EQ(AdviceVar(), SentenceFor(kSeat0));
        EXPECT_EQ(_game->GetSession().Version(), version) << "the advice never moves the session";
        EXPECT_EQ(Hand().GetHand(kSeat0).concealed.size(), 14u);
        EXPECT_TRUE(_game->GetSession().ActorIsPerson());

        // The window grows: the canvases swap back, the button is on the table again and the advice stays (it belongs to the table, not to the canvas)
        SetOutputSize(*_renderer, kViewWidth, kViewHeight);
        Draw();
        EXPECT_TRUE(ButtonShown());
        EXPECT_FALSE(CompactButtonShown());
        EXPECT_TRUE(AdviceShown());
        EXPECT_EQ(AdviceVar(), SentenceFor(kSeat0));
    }

    TEST_F(MahjongHintsTest, APressSetsTheSentenceOfTheAdviceAndFramesTheAdvisedDiscard) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_TRUE(ButtonShown());
        ASSERT_TRUE(RectsOf(kAdvisedFrame).empty());
        const std::optional<HandAction> advice = AdviceFor(ViewOf(kSeat0));
        ASSERT_TRUE(advice.has_value());
        ASSERT_EQ(*advice, HandAction::MakeDiscard(T("Nw"))) << "worked out in HintsTest";
        const uint64_t version = _game->GetSession().Version();

        Press();

        EXPECT_EQ(AdviceVar(), "Discard North Wind.");
        EXPECT_EQ(AdviceVar(), SentenceFor(kSeat0));
        EXPECT_TRUE(AdviceShown());
        EXPECT_EQ(_game->GetSession().Version(), version) << "the advice never moves the session";
        // The frame lies on the Nw of the hand: four bars round exactly that slot
        const PlayerHand& held = Hand().GetHand(kSeat0);
        int slots = 0;
        for (const TileSlot& slot : _table->GetLayout().slots) {
            if (slot.role == SlotRole::HandTile && slot.seat == kSeat0 && held.concealed[slot.index] == T("Nw")) {
                ++slots;
                const Rect& r = slot.rect;
                bool top = false;
                for (const Rect& bar : RectsOf(kAdvisedFrame)) {
                    top = top || (bar.x == r.x && bar.y == r.y && bar.w == r.w);
                }
                EXPECT_TRUE(top);
            }
        }
        EXPECT_EQ(slots, 1);
        EXPECT_EQ(RectsOf(kAdvisedFrame).size(), 4u * slots);
        // Asking for the advice plays nothing: the hand still holds its 14 tiles and the person is still to act
        EXPECT_EQ(Hand().GetHand(kSeat0).concealed.size(), 14u);
        EXPECT_TRUE(_game->GetSession().ActorIsPerson());
    }

    TEST_F(MahjongHintsTest, TheDeclareBarAndTheAdviceFrameOfTheSelectedTileFollowTheRaise) {
        // 123s 456s 789m 234p Ew and a drawn 9p: the 9p and the east wind declare
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        const int index = IndexOf(kSeat0, T("9p"));
        const int other = IndexOf(kSeat0, T("Ew"));
        // Copies: the layout is made again by every draw
        ASSERT_TRUE(HandSlot(index) != nullptr && HandSlot(other) != nullptr);
        const Rect slotRect = HandSlot(index)->rect;
        const Rect otherRect = HandSlot(other)->rect;
        TapHandTile(index);
        Draw();
        ASSERT_EQ(_table->SelectedHandIndex(), index);
        const Rect raised = RaisedRect(slotRect);

        // The bar of the 9p lies under the raised tile, the bar of the east wind under its own slot
        bool raisedBar = false;
        bool plainBar = false;
        for (const Rect& bar : RectsOf(kDeclareMark)) {
            raisedBar = raisedBar || (bar.x == raised.x && bar.y > raised.y + raised.h && bar.y < slotRect.y + slotRect.h);
            plainBar = plainBar || (bar.x == otherRect.x && bar.y > otherRect.y + otherRect.h);
        }
        EXPECT_TRUE(raisedBar);
        EXPECT_TRUE(plainBar);
        EXPECT_EQ(RectsOf(kDeclareMark).size(), 2u);

        // The advice frame of the advised tile rises too
        const std::optional<HandAction> advice = AdviceFor(ViewOf(kSeat0));
        ASSERT_TRUE(advice.has_value());
        ASSERT_EQ(advice->kind, ActionKind::Discard);
        ASSERT_EQ(advice->tile, T("9p")) << "worked out in HintsTest: the advice is the 9p";
        Press();
        bool topBar = false;
        for (const Rect& bar : RectsOf(kAdvisedFrame)) {
            topBar = topBar || (bar.x == raised.x && bar.y == raised.y && bar.w == raised.w);
        }
        EXPECT_TRUE(topBar) << "the frame is round the raised rectangle";
        EXPECT_EQ(RectsOf(kAdvisedFrame).size(), 4u);
    }

    TEST_F(MahjongHintsTest, AnUnchangedFrameWithASelectionSetsNothingAgain) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        TapHandTile(IndexOf(kSeat0, T("9p")));
        Draw();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    TEST_F(MahjongHintsTest, ThePressOfTheButtonWorksByClick) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_TRUE(ClickElement("HintsButton", "HintButton"));
        Draw();
        EXPECT_TRUE(AdviceShown());
        EXPECT_EQ(AdviceVar(), SentenceFor(kSeat0));
    }

    TEST_F(MahjongHintsTest, ADeclaringAdviceSaysSoAndMarksTheDeclaringTile) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kSingleWait, "9p"));
        Press();
        EXPECT_EQ(AdviceVar(), "Discard 9 Dots and declare the ready hand.");
        EXPECT_EQ(RectsOf(kAdvisedFrame).size(), 4u);
        EXPECT_EQ(RectsOf(kDeclareMark).size(), 2u);
    }

    TEST_F(MahjongHintsTest, TheButtonIsASwitchDrawnSelectedWhileOn) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        const auto selected = [&](const char* node) {
            const UIElement* button = Element(node, "HintButton");
            return button != nullptr && button->selected;
        };
        EXPECT_FALSE(selected("HintsButton"));
        Press();
        EXPECT_TRUE(AdviceShown());
        EXPECT_FALSE(WhyShown()) << "the mode Hints gives the sentence only";
        EXPECT_TRUE(selected("HintsButton"));
        EXPECT_TRUE(selected("ActionsCompact")) << "both buttons show the switch";
        const uint64_t varsVersion = CoreManager::GetUIVars().GetVersion();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), varsVersion) << "an unchanged frame sets nothing";

        Press();
        EXPECT_FALSE(AdviceShown()) << "the second press switches it off";
        EXPECT_TRUE(AdviceVar().empty());
        EXPECT_TRUE(RectsOf(kAdvisedFrame).empty());
        EXPECT_FALSE(selected("HintsButton"));
        EXPECT_FALSE(selected("ActionsCompact"));
        EXPECT_TRUE(ButtonShown());
    }

    TEST_F(MahjongHintsTest, WhileTheSwitchIsOnEveryTurnOfThePersonGetsItsAdviceByItself) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        Press();
        ASSERT_TRUE(AdviceShown());
        ASSERT_TRUE(Discard("Nw"));
        Draw();
        EXPECT_FALSE(AdviceShown()) << "no advice while the computers move";
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson() || _game->GetSession().GetPhase() != SessionPhase::Playing; }));
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        Draw();
        EXPECT_TRUE(AdviceShown()) << "no press needed";
        EXPECT_EQ(AdviceVar(), SentenceFor(_game->GetSession().Actor()));
    }

    TEST_F(MahjongHintsTest, InAClaimWindowTheSentenceNamesTheClaimTheComputerWouldMake) {
        ASSERT_NO_FATAL_FAILURE(StartWindow(kWindowPung, "5p"));
        ASSERT_EQ(AdviceFor(ViewOf(kSeat1)), std::optional<HandAction>(HandAction::MakeClaim(ClaimKind::Pung)));
        const uint64_t version = _game->GetSession().Version();

        Press();

        EXPECT_EQ(AdviceVar(), "Take the discard to make a pung of 5 Dots.");
        EXPECT_EQ(AdviceVar(), SentenceFor(kSeat1));
        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(Hand().GetPhase(), Phase::Claim) << "nothing was claimed";
        EXPECT_TRUE(RectsOf(kAdvisedFrame).empty()) << "no tile of the hand is advised";
    }

    TEST_F(MahjongHintsTest, InAClaimWindowTheAdviceMayBeToLetTheDiscardGo) {
        ASSERT_NO_FATAL_FAILURE(StartWindow(kWindowPass, "5m"));
        ASSERT_EQ(AdviceFor(ViewOf(kSeat1)), std::optional<HandAction>(HandAction::MakePass()));

        Press();

        EXPECT_EQ(AdviceVar(), "Let the discard go.");
        EXPECT_TRUE(AdviceShown());
    }

    TEST_F(MahjongHintsTest, APressThatIsNotOfferedChangesNothing) {
        // A computer acts: the button is not offered, a stale press is ignored
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_TRUE(Discard("Nw"));
        Draw();
        ASSERT_FALSE(_game->GetSession().ActorIsPerson());
        const uint64_t version = _game->GetSession().Version();
        const uint64_t varsVersion = CoreManager::GetUIVars().GetVersion();
        Press();
        EXPECT_FALSE(AdviceShown());
        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), varsVersion);

        // Hints off, Setup open, a spectator
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        _game->SetHintsOn(false);
        Press();
        EXPECT_FALSE(AdviceShown());
        _game->SetHintsOn(true);
        _game->OpenSetup();
        Press();
        EXPECT_FALSE(AdviceShown());
        _game->CloseSetup();
        Draw();
        EXPECT_FALSE(AdviceShown()) << "the press while Setup was open is not kept";
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw", kAllComputers));
        Press();
        EXPECT_FALSE(AdviceShown());
    }

    TEST_F(MahjongHintsTest, TheAdviceGoesWhenTheDiscardIsMadeAndNothingStaysInTheVariable) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        Press();
        ASSERT_TRUE(AdviceShown());
        ASSERT_FALSE(AdviceVar().empty());

        ASSERT_TRUE(Discard("Nw"));
        Draw();

        EXPECT_FALSE(AdviceShown());
        EXPECT_TRUE(AdviceVar().empty());
        EXPECT_TRUE(RectsOf(kAdvisedFrame).empty());
    }

    TEST_F(MahjongHintsTest, AnyOtherDiscardDropsTheAdviceToo) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        Press();
        ASSERT_TRUE(AdviceShown());
        // Another discard than the advised one: the table moved on all the same
        ASSERT_TRUE(Discard("Rd"));
        Draw();
        EXPECT_FALSE(AdviceShown());
    }

    TEST_F(MahjongHintsTest, TheAdviceGoesWhenHintsGoOffOrASetupOpensOrACoverGoesUp) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        Press();
        ASSERT_TRUE(AdviceShown());
        _game->SetHintsOn(false);
        Draw();
        EXPECT_FALSE(AdviceShown());
        _game->SetHintsOn(true);
        Draw();
        EXPECT_TRUE(AdviceShown()) << "the switch is still on: it comes back";
        EXPECT_TRUE(ButtonShown());

        _game->OpenSetup();
        Draw();
        EXPECT_FALSE(AdviceShown());
        _game->CloseSetup();
        Draw();
        EXPECT_TRUE(AdviceShown());
        Press();
        ASSERT_FALSE(AdviceShown());

        // Hot seat: person 0 asks, discards; the cover goes up for person 2 and the advice is gone
        const SeatSetup twoPeople{SeatKind::Person, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw", twoPeople));
        _game->GetSession().Reveal();
        Refresh();
        Press();
        ASSERT_TRUE(AdviceShown());
        ASSERT_TRUE(Discard("Nw"));
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
        Draw();
        ASSERT_TRUE(_game->GetSession().HandoffPending());
        EXPECT_FALSE(AdviceShown());
        EXPECT_FALSE(ButtonShown());
        _game->GetSession().Reveal();
        Draw();
        EXPECT_TRUE(AdviceShown()) << "the switch is the table's: the next person gets an advice of their own";
        EXPECT_EQ(AdviceVar(), SentenceFor(_game->GetSession().Viewer()));
        EXPECT_TRUE(ButtonShown());
    }

    TEST_F(MahjongHintsTest, TheLanguageSwitchWordsTheAdviceAgain) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        Press();
        std::vector<std::string> seen;
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            EXPECT_EQ(AdviceVar(), SentenceFor(kSeat0));
            EXPECT_FALSE(AdviceVar().empty());
            EXPECT_TRUE(AdviceShown());
            seen.push_back(AdviceVar());
        });
        ASSERT_GE(seen.size(), 2u);
        EXPECT_NE(seen[0], seen[1]);
    }

    TEST_F(MahjongHintsTest, TheAdviceNeverMovesTheSessionAndTheControlStateIsEqualWithHintsOnAndOff) {
        const auto play = [&](bool hintsOn) {
            std::string state;
            [&] {
                _game->SetHintsOn(hintsOn);
                ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
                const uint64_t version = _game->GetSession().Version();
                for (int i = 0; i < 3; ++i) {
                    Press();
                }
                EXPECT_EQ(_game->GetSession().Version(), version);
                EXPECT_EQ(hintsOn, AdviceShown());
                const auto got = _game->AsControllable()->GetState();
                ASSERT_TRUE(got.has_value());
                Json shown = *got;
                shown.erase("version");  // counts the changes of the whole session, which the second match starts further on
                state = shown.dump();
            }();
            return state;
        };
        const std::string withHints = play(true);
        const std::string withoutHints = play(false);
        size_t at = 0;
        while (at < withHints.size() && at < withoutHints.size() && withHints[at] == withoutHints[at]) {
            ++at;
        }
        EXPECT_EQ(withHints, withoutHints) << "first difference at " << at << ": " << withHints.substr(at > 40 ? at - 40 : 0, 120) << " | " << withoutHints.substr(at > 40 ? at - 40 : 0, 120);
    }

    TEST_F(MahjongHintsTest, TheButtonAndTheSentenceFitInEveryLanguageAndTheStripIsClearOfTheTable) {
        ASSERT_NO_FATAL_FAILURE(StartWindow(kWindowPung, "5p"));
        Press();
        ASSERT_TRUE(AdviceShown());

        // The longest sentence of each language, over every kind of advice and every tile
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            std::vector<eastl::string> sentences;
            TableView view;
            view.pendingTile = T("5p");
            const auto add = [&](const HandAction& action) { sentences.push_back(AdviceText(strings, view, action)); };
            for (int id = 0; id < kTileIdCount; ++id) {
                const Tile tile = Tile(static_cast<uint8_t>(id));
                add(HandAction::MakeDiscard(tile, true));
                add(HandAction::MakeConcealedKong(tile));
                add(HandAction::MakeUpgradeKong(tile));
                if (tile.IsSuit() && tile.Rank() <= 7) {
                    add(HandAction::MakeClaim(ClaimKind::Chow, tile));
                }
            }
            for (const ClaimKind kind : {ClaimKind::Pung, ClaimKind::Kong, ClaimKind::Mahjong}) {
                view.pendingTile = T("7s");
                add(HandAction::MakeClaim(kind));
                view.pendingTile = T("Wd");
                add(HandAction::MakeClaim(kind));
            }
            add(HandAction::MakePass());
            add(HandAction::MakeDeclareWin());
            const eastl::string longest = LongestText(sentences);
            CoreManager::GetUIVars().Set("hintAdvice"_intern, eastl::string_view(longest));
            Draw();
            ASSERT_TRUE(AdviceShown());
            ASSERT_TRUE(ButtonShown());
            ExpectPassesTheMatrix("Hints", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Hints", output.width, output.height, 0.0f);
                ExpectWordsFit("HintsButton", output.width, output.height, 0.0f);
            }
            for (const char* node : {"Hints", "HintsButton"}) {
                const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas(node), 0.0f);
                EXPECT_TRUE(cut.empty()) << node << ": " << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
            }
            // The button is a 56-unit control on a canvas that the table's fit scales: the engine's minimum holds wherever `Actions` does, and in
            // the narrow windows the button of the row of `ActionsCompact` takes its place (`ExpectPassesTheMatrix("ActionsCompact")` of
            // `MahjongTableScreenTest` holds that one)
            ExpectMatrixExceptTouchWhere(*Canvas("HintsButton"), "HintsButton", EngineTouchMinimum(), ActionsMayBeSmall);
        });

        for (const TableShape shape : {TableShape::Landscape, TableShape::Portrait, TableShape::Wide, TableShape::Narrow}) {
            const MatrixWindow window = DesignWindowOf(shape);
            SCOPED_TRACE(window.label.c_str());
            LayOutOnTheTable({"Hints", "HintsButton"}, window);
            const UIElement* button = Element("HintsButton", "HintButton");
            const UIElement* label = Element("Hints", "AdviceText");
            const UIElement* panel = Element("Hints", "WaitPanel");
            ASSERT_NE(button, nullptr);
            ASSERT_NE(label, nullptr);
            ASSERT_NE(panel, nullptr);

            EXPECT_TRUE(Inside(button->rect, HintsAreaOf(shape)));
            EXPECT_TRUE(Inside(label->rect, HintsAreaOf(shape)));
            EXPECT_TRUE(Apart(button->rect, label->rect));
            EXPECT_TRUE(Apart(button->rect, panel->rect));
            EXPECT_TRUE(Apart(label->rect, panel->rect));
            for (const Rect* rect : {&button->rect, &label->rect, &panel->rect}) {
                EXPECT_TRUE(Apart(*rect, CenterPanel(shape))) << "nothing of the hints is on the centre panel";
            }
            EXPECT_TRUE(button->IsHitTestable()) << "the button takes the press";
            EXPECT_FALSE(label->IsHitTestable()) << "the sentence takes no input";

            for (const TileSlot& slot : MakeTableLayout(WorstCaseInput(), DesignWidthOf(shape), DesignHeightOf(shape)).slots) {
                EXPECT_TRUE(Apart(slot.rect, button->rect)) << "role " << static_cast<int>(slot.role);
                EXPECT_TRUE(Apart(slot.rect, label->rect)) << "role " << static_cast<int>(slot.role) << " slot " << slot.rect.x << "," << slot.rect.y << "," << slot.rect.w << "x" << slot.rect.h
                                                           << " label " << label->rect.x << "," << label->rect.y << "," << label->rect.w << "x" << label->rect.h;
            }
        }
    }

    // ----- the reasons of the advice -----

    namespace {

        /// The cards of every kind of advice over every tile and big numbers, in the language of `strings`, with the longest tally and
        /// every sentence of an advice as a title: the texts the card may have to hold.
        std::vector<HintCard> EveryCard(const UIStrings& strings) {
            constexpr int kBig = 99;
            constexpr int kKeepClasses[] = {kKeepWorthless, kKeepTerminal, kKeepSimple, kKeepValueTile};
            const eastl::string tally = CardTallyText(strings, CoachTally{kBig, kBig}, CoachTally{kBig * 9, kBig * 9});
            std::vector<HintCard> cards;
            TableView view;
            const auto say = [&](const Explanation& explained) { cards.push_back(MakeAdviceCard(strings, view, explained, tally)); };
            const auto title = [&](const HandAction& action) {
                HintCard card;
                card.title = AdviceText(strings, view, action);
                cards.push_back(card);
            };
            for (int id = 0; id < kTileIdCount; ++id) {
                const Tile tile = Tile(static_cast<uint8_t>(id));
                view.pendingTile = tile;
                title(HandAction::MakeDiscard(tile, true));
                title(HandAction::MakeConcealedKong(tile));
                title(HandAction::MakeUpgradeKong(tile));
                for (const ClaimKind kind : {ClaimKind::Pung, ClaimKind::Kong}) {
                    title(HandAction::MakeClaim(kind));
                }
                if (tile.IsSuit() && tile.Rank() <= 7) {
                    title(HandAction::MakeClaim(ClaimKind::Chow, tile));
                }
                for (const TurnStep step : {TurnStep::Discard, TurnStep::Declare, TurnStep::KeepWaiting, TurnStep::Kong, TurnStep::DeclareWin}) {
                    for (const int keep : kKeepClasses) {
                        TurnExplanation turn;
                        turn.step = step;
                        turn.action = step == TurnStep::Kong ? HandAction::MakeUpgradeKong(tile) : HandAction::MakeDiscard(tile, step == TurnStep::Declare);
                        turn.shantenBefore = 8;
                        turn.kongShantenAfter = 8;
                        turn.kongAcceptanceBefore = kBig;
                        turn.kongAcceptanceAfter = kBig;
                        turn.chosen = TurnOption{tile, 8, kBig, keep, 13, kBig};
                        turn.equalChoices = 13;
                        turn.declaredOpponents = 1;
                        turn.alternatives = {TurnOption{tile, 8, kBig, keep, 13, kBig}, TurnOption{tile, 8, kBig, keep + 1, 13, kBig}};
                        Explanation explained;
                        explained.turn = turn;
                        say(explained);
                    }
                }
                for (const ClaimStep step : {ClaimStep::Mahjong, ClaimStep::WaitingHand, ClaimStep::Claim, ClaimStep::NoGain}) {
                    ClaimExplanation claim;
                    claim.step = step;
                    claim.action = HandAction::MakePass();
                    claim.shantenBefore = 8;
                    claim.waits.assign(13, TileCopies{tile, kBig});
                    claim.chosen = RatedClaim{HandAction::MakeClaim(ClaimKind::Pung), DiscardRating{tile, 8, kBig}, 0, true};
                    for (const ClaimKind kind : {ClaimKind::Pung, ClaimKind::Kong, ClaimKind::Chow}) {
                        const HandAction action = kind == ClaimKind::Chow ? HandAction::MakeClaim(kind, tile) : HandAction::MakeClaim(kind);
                        claim.alternatives = {RatedClaim{action, DiscardRating{tile, 8, kBig}, 0, true}, RatedClaim{action, DiscardRating{tile, 8, kBig}, 0, false}};
                        Explanation explained;
                        explained.claim = claim;
                        say(explained);
                    }
                }
            }
            title(HandAction::MakePass());
            title(HandAction::MakeDeclareWin());
            title(HandAction::MakeClaim(ClaimKind::Mahjong));
            return cards;
        }

    }  // namespace

    TEST_F(MahjongHintsTest, InCoachThePressPutsTheCardUpWithTheSentenceAsItsTitleAndTheNextTakesItDown) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText());
        const uint64_t version = _game->GetSession().Version();

        Press();
        EXPECT_FALSE(AdviceShown()) << "in Coach the sentence is the title of the card, not a label of its own";
        EXPECT_EQ(AdviceVar(), "");
        ASSERT_TRUE(WhyShown());
        EXPECT_EQ(WhyVars(), ExpectedWhy(kSeat0));
        EXPECT_EQ(WhyVar(0), SentenceFor(kSeat0));
        EXPECT_FALSE(WhyVar(1).empty());
        EXPECT_EQ(RectsOf(kAdvisedFrame).size(), 4u);

        Press();
        EXPECT_FALSE(AdviceShown());
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText()) << "nothing of the reasons stays in a variable";

        Press();
        EXPECT_TRUE(WhyShown()) << "and it opens again";
        EXPECT_EQ(WhyVars(), ExpectedWhy(kSeat0));
        EXPECT_EQ(_game->GetSession().Version(), version) << "the reasons never move the session";
        EXPECT_EQ(Hand().GetHand(kSeat0).concealed.size(), 14u);
    }

    TEST_F(MahjongHintsTest, TheReasonsOfAClaimWindowAreThePersonsOwn) {
        ASSERT_NO_FATAL_FAILURE(StartWindow(kWindowPung, "5p"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        EXPECT_EQ(WhyVars(), ExpectedWhy(kSeat1));
        EXPECT_FALSE(WhyVar(0).empty());
    }

    TEST_F(MahjongHintsTest, ThePanelOfTheReasonsGoesWithTheAdvice) {
        // A discard
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        ASSERT_TRUE(Discard("Nw"));
        Draw();
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText());

        // Hints off, and they do not come back by themselves
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        _game->SetHintsOn(false);
        Draw();
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText());
        _game->SetHintsMode(HintsMode::Coach);
        Draw();
        EXPECT_TRUE(WhyShown()) << "the switch is still on: the card comes back";
        EXPECT_FALSE(AdviceShown());
        _game->SetHintsOn(true);
        Draw();
        EXPECT_FALSE(WhyShown()) << "the mode Hints has no card";
        EXPECT_TRUE(AdviceShown()) << "and the sentence is a label of its own again";
        Press();

        // Setup, the page Options and the rules
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        _game->OpenSetup();
        Draw();
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText());
        _game->CloseSetup();
        Draw();
        EXPECT_TRUE(WhyShown());
        Press();

        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        _game->OpenRules();
        Draw();
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText());
        _game->CloseRules();
        Draw();
        EXPECT_TRUE(WhyShown());
        Press();

        // Hot seat: person 0 opens it, discards; the cover goes up for person 2, who does not inherit it
        const SeatSetup twoPeople{SeatKind::Person, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw", twoPeople));
        _game->GetSession().Reveal();
        Refresh();
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        ASSERT_TRUE(Discard("Nw"));
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
        Draw();
        ASSERT_TRUE(_game->GetSession().HandoffPending());
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText());
        _game->GetSession().Reveal();
        Draw();
        EXPECT_NE(_game->GetSession().Viewer(), kSeat0);
        EXPECT_TRUE(WhyShown()) << "the next person gets reasons of their own";
        EXPECT_EQ(WhyVars(), ExpectedWhy(_game->GetSession().Viewer()));
    }

    TEST_F(MahjongHintsTest, APressThatIsNotOfferedOpensNoReasons) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_TRUE(Discard("Nw"));
        Draw();
        ASSERT_FALSE(_game->GetSession().ActorIsPerson());
        const uint64_t varsVersion = CoreManager::GetUIVars().GetVersion();
        Press();
        Press();
        EXPECT_FALSE(WhyShown());
        EXPECT_FALSE(AdviceShown());
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), varsVersion);

        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        _game->SetHintsOn(false);
        Press();
        Press();
        EXPECT_FALSE(WhyShown());
        EXPECT_TRUE(NoWhyText());
    }

    TEST_F(MahjongHintsTest, TheLanguageSwitchWordsTheReasonsAgain) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        std::vector<std::string> seen;
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            EXPECT_TRUE(WhyShown());
            EXPECT_EQ(WhyVars(), ExpectedWhy(kSeat0));
            EXPECT_FALSE(WhyVar(0).empty());
            seen.push_back(WhyVar(0));
        });
        ASSERT_GE(seen.size(), 2u);
        EXPECT_NE(seen[0], seen[1]);
    }

    TEST_F(MahjongHintsTest, AnUnchangedFrameWithTheReasonsOpenSetsNothingAgain) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    TEST_F(MahjongHintsTest, TheControlStateIsTheSameWithTheReasonsOpenAndWithHintsOff) {
        const auto play = [&](bool hintsOn) {
            std::string state;
            [&] {
                _game->SetHintsMode(hintsOn ? HintsMode::Coach : HintsMode::Off);
                ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
                const uint64_t version = _game->GetSession().Version();
                Press();
                EXPECT_EQ(hintsOn, WhyShown());
                EXPECT_EQ(_game->GetSession().Version(), version);
                const auto got = _game->AsControllable()->GetState();
                ASSERT_TRUE(got.has_value());
                Json shown = *got;
                shown.erase("version");
                state = shown.dump();
            }();
            return state;
        };
        const std::string withReasons = play(true);
        EXPECT_EQ(withReasons, play(false));
    }

    TEST_F(MahjongHintsTest, TheReasonsDoNotSeeTheOtherHands) {
        // Two tables that differ only in the hands of the other seats and the live wall after the viewer's draw
        const auto run = [&](const char* seat1, const char* seat3, const std::string& live) {
            std::vector<std::string> lines;
            [&] {
                ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
                FixedWallSpec spec = SpecOf(kBestIsTheNorthWind, "Nw");
                spec.hands[kSeat1] = seat1;
                spec.hands[kSeat3] = seat3;
                spec.live = live;
                Lay(MakeModel(spec));
                Draw();
                ASSERT_NO_FATAL_FAILURE(OpenWhy());
                lines = WhyVars();
            }();
            return lines;
        };
        const std::vector<std::string> plain = run(kQuiet1, kQuiet3, "4p Rd Gd Wd");
        const std::vector<std::string> loaded = run("7p 7p 7p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", "4p 4p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd Rd", "4p Gd Rd Wd");
        EXPECT_FALSE(plain.front().empty());
        EXPECT_EQ(plain, loaded);
    }

    TEST_F(MahjongHintsTest, ThePanelLiesOnTheWallRingClearOfEveryTileButTheWallAndTakesNoInput) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        for (const TableShape shape : {TableShape::Landscape, TableShape::Portrait, TableShape::Narrow, TableShape::Wide}) {
            const MatrixWindow window = DesignWindowOf(shape);
            SCOPED_TRACE(window.label.c_str());
            LayOutOnTheTable({"HintsWhy"}, window);

            const UIElement* panel = Element("HintsWhy", "CoachCard");
            ASSERT_NE(panel, nullptr);
            const Rect ring = MakeTableLayout(TableInput{}, DesignWidthOf(shape), DesignHeightOf(shape)).zones.wall;
            EXPECT_TRUE(Inside(panel->rect, ring)) << panel->rect.x << "," << panel->rect.y << " " << panel->rect.w << "x" << panel->rect.h;
            EXPECT_NEAR(panel->rect.w, ring.w, 0.5f) << "the whole ring: no wall stack shows beside the card";
            EXPECT_NEAR(panel->rect.h, ring.h, 0.5f);
            std::vector<std::string> parts{"CardTitle", "CardHead", "CardFoot0", "CardFoot1"};
            for (size_t i = 0; i < kMaxCardRows; ++i) {
                parts.push_back(CellName("CardRow", static_cast<int>(i)));
                for (const char* cell : {"CardWho", "CardMove", "CardSteps", "CardUseful"}) {
                    parts.push_back(CellName(cell, static_cast<int>(i)));
                }
            }
            for (const std::string& name : parts) {
                const UIElement* part = Element("HintsWhy", name.c_str());
                ASSERT_NE(part, nullptr) << name;
                ASSERT_GT(part->rect.w, 0.0f) << name << " is on screen (the card of this table has every row)";
                EXPECT_TRUE(Inside(part->rect, panel->rect)) << name;
                EXPECT_FALSE(part->IsHitTestable()) << name << " takes no input";
            }
            EXPECT_FALSE(panel->IsHitTestable());
            EXPECT_FALSE(Click(panel->rect.x + panel->rect.w * 0.5f, panel->rect.y + panel->rect.h * 0.5f)) << "a click on the panel is no canvas's";

            for (const TileSlot& slot : MakeTableLayout(WorstCaseInput(), DesignWidthOf(shape), DesignHeightOf(shape)).slots) {
                if (slot.role != SlotRole::WallStack) {
                    EXPECT_TRUE(Apart(slot.rect, panel->rect)) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                }
            }
        }
    }

    TEST_F(MahjongHintsTest, TheCardPassesTheSizeMatrixAndTheLongestTextOfEveryCellFitsInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kBestIsTheNorthWind, "Nw"));
        ASSERT_NO_FATAL_FAILURE(OpenWhy());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();  // the words of the language
            SetLongestCardTexts(EveryCard(CoreManager::GetUIStrings()));
            Draw();
            ASSERT_TRUE(WhyShown());
            ExpectPassesTheMatrix("HintsWhy", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("HintsWhy", output.width, output.height, 0.0f);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("HintsWhy"), 0.0f);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

}  // namespace BECore::Mahjong::Tests
