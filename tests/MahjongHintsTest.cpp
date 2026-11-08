#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UIImage.h>
#include <Mahjong/Components/MahjongHintsComponent.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Table/Hints.h>
#include <Mahjong/Table/TableLayout.h>

/**
 * The hints of the shipped scene (`Components/MahjongHintsComponent.h`, `Table/Hints.h`): the switch `Hints` On | Off of the panel
 * `Setup`, which the component answers and the game keeps (`MahjongGame::HintsOn()`), and the waiting row inside the wall ring. Fixed
 * hands (`FixedWall.h`) are laid into the hand in progress; the pure helper has its own test (`HintsTest`).
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

        // The hands of the waiting row (the same shapes as in `HintsTest`); East is the person at seat 0, holds the 13 tiles and draws one more.
        constexpr const char* kSingleWait = "1s 2s 3s 4s 5s 6s 7m 8m 9m 2p 3p 4p Ew";    // waits on Ew
        constexpr const char* kTwoSidedWait = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew";  // waits on 4p and 7p
        constexpr const char* kOrphansWait = "1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd";   // waits on all thirteen
        constexpr const char* kPlainEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";     // far from waiting
        constexpr int kWaitCells = kMaxWaitingTiles;
        /// The worst-case table, as `MahjongTableScreenTest` lays it out: every zone at its capacity.
        constexpr int kMostDiscards = 100;

        /// What the canvas shows: the notation of each tile of a shown cell and its count text.
        using Row = std::vector<std::pair<std::string, std::string>>;

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
        ASSERT_TRUE(_game->GetSession().ActorIsPerson() == false) << "a computer acts now";
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
            const eastl::string expected = Tr(CoreManager::GetUIStrings(), kWaitCaptionKey);
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

        bool Apart(const Rect& a, const Rect& b) {
            return a.x + a.w <= b.x || a.x >= b.x + b.w || a.y + a.h <= b.y || a.y >= b.y + b.h;
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
            ExpectPassesTheMatrix("Hints", kPanelTouchMinimum);
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Hints", output.width, output.height, 0.0f);
            }
        });
    }

    TEST_F(MahjongHintsTest, ThePanelLiesInsideTheWallRingClearOfEveryTileOfTheWorstCaseTableAndTakesNoInput) {
        ASSERT_NO_FATAL_FAILURE(StartEast(kOrphansWait, "Ew"));
        ASSERT_TRUE(Discard("Ew"));
        Draw();
        ASSERT_EQ(ShownRow().size(), static_cast<size_t>(kWaitCells));
        auto canvas = Canvas("Hints");
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(kViewWidth, kViewHeight);

        // The ground inside the ring of wall stacks, from the constants of the layout
        constexpr float ringW = kWallStackSize * kWallRingColumns;
        constexpr float ringH = kWallStackSize * kWallRingRows;
        const Rect inside((kViewWidth - ringW) * 0.5f + kWallStackSize, (kViewHeight - ringH) * 0.5f + kWallStackSize, ringW - 2.0f * kWallStackSize, ringH - 2.0f * kWallStackSize);

        const UIElement* panel = Element("Hints", "WaitPanel");
        ASSERT_NE(panel, nullptr);
        EXPECT_TRUE(Inside(panel->rect, inside)) << panel->rect.x << "," << panel->rect.y << " " << panel->rect.w << "x" << panel->rect.h;
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
        const TableLayout layout = MakeTableLayout(worst);
        for (const TileSlot& slot : layout.slots) {
            EXPECT_TRUE(Apart(slot.rect, panel->rect)) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
        }
    }

}  // namespace BECore::Mahjong::Tests
