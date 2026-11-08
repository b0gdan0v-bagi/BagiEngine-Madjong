#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The HUD and the seat choice of the shipped scene (`Components/MahjongHudComponent.h`): the variables follow the session, the labels
 * show them, the panel `Setup` opens and closes with the game, and both canvases pass the size matrix with the longest texts.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kLongestSeat = "Seat 4 (North) You";
        constexpr float kHudTouchMinimum = 20.0f;  // the HUD and the panel lie on the fixed design of the table, as the action buttons do
        /// Output sizes (and the air a button keeps) the words are asked in: the design size, a small window, the narrowest windows.
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}};
        constexpr float kButtonAir = 8.0f;
        constexpr float kMinControlHeight = 48.0f;
        constexpr int kHandsToTry = 20;
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        constexpr int kWorstDiscards = 100;

        void Elapse(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

    }  // namespace

    class MahjongHudTest : public MahjongTableTestBase {
    protected:
        /// The longest text each variable can have.
        static void SetWorstVars() {
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("round"_intern, "North");
            vars.Set("hand"_intern, 16);
            vars.Set("handsTotal"_intern, 16);
            vars.Set("wall"_intern, 144);
            vars.Set("turn"_intern, eastl::string_view(kLongestSeat));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                vars.Set(PoolString::Intern(Format("seatName{}", seat)), eastl::string_view(kLongestSeat));
                vars.Set(PoolString::Intern(Format("seatScore{}", seat)), "-9999");
                vars.Set(PoolString::Intern(Format("seatMark{}", seat)), ">");
                vars.Set(PoolString::Intern(Format("seatDealer{}", seat)), "*");
            }
        }

        std::string Text(const char* node, const char* element) const {
            const UIElement* found = Element(node, element);
            const auto* label = found != nullptr ? found->Cast<UILabel>() : nullptr;
            return label != nullptr ? std::string(label->GetDisplayText().data(), label->GetDisplayText().size()) : std::string("<no label>");
        }

        std::string SeatKindVar(Seat seat) const {
            return Var(Format("seatKind{}", seat).c_str());
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }

        MahjongHudComponent* Hud() const {
            return MahjongHudComponent::GetActive();
        }
    };

    // ----- the variables and the labels -----

    TEST_F(MahjongHudTest, TheVariablesAfterAStartAreTheMatchAsItStands) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const Match& match = _game->GetMatch();
        EXPECT_EQ(Var("round"), "East");
        EXPECT_EQ(Var("hand"), "1");
        EXPECT_EQ(Var("handsTotal"), "16");
        EXPECT_EQ(Var("wall"), std::to_string(Hand().GetWall().LiveCount()));
        const Seat turn = Hand().CurrentSeat();
        EXPECT_EQ(Var("turn"), std::string(Format("Seat {} ({})", turn + 1, EnumUtils<Wind>::ToString(Hand().SeatWind(turn))).c_str()));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const std::string name = Format("Seat {} ({})", seat + 1, EnumUtils<Wind>::ToString(Hand().SeatWind(seat))).c_str();
            EXPECT_EQ(Var(Format("seatName{}", seat).c_str()), seat == 0 ? name + " You" : name) << "only the one person is marked, seat " << seat;
            EXPECT_EQ(Var(Format("seatWind{}", seat).c_str()), EnumUtils<Wind>::ToString(Hand().SeatWind(seat)).data());
            EXPECT_EQ(Var(Format("seatScore{}", seat).c_str()), std::to_string(match.Scores()[seat]));
            EXPECT_EQ(Var(Format("seatMark{}", seat).c_str()), seat == turn ? ">" : "") << "the mark is on the seat whose turn it is";
            EXPECT_EQ(Var(Format("seatDealer{}", seat).c_str()), seat == match.East() ? "*" : "");
        }
    }

    TEST_F(MahjongHudTest, NoOneIsMarkedYouWhenTwoPeoplePlayOrNobodyDoes) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Draw();
        EXPECT_EQ(Var("seatName0").find("You"), std::string::npos);
        EXPECT_EQ(Var("seatName1").find("You"), std::string::npos);

        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        EXPECT_EQ(Var("seatName0").find("You"), std::string::npos);
    }

    TEST_F(MahjongHudTest, TheVariablesFollowAnActionAndAreNotSetAgainWhileTheVersionStandsStill) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        const std::string turn = Var("turn");
        const uint64_t varsVersion = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), varsVersion) << "an unchanged table sets nothing";

        const uint64_t version = _game->GetSession().Version();
        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());  // a discard
        ASSERT_GT(_game->GetSession().Version(), version);
        Draw();

        EXPECT_NE(Var("turn"), turn) << "the turn went on";
        EXPECT_EQ(Var("wall"), std::to_string(Hand().GetWall().LiveCount()));
        EXPECT_NE(CoreManager::GetUIVars().GetVersion(), varsVersion);
    }

    TEST_F(MahjongHudTest, TheLabelsOnScreenShowTheScoresOfTheMatchAfterAHand) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        bool paid = false;
        for (uint32_t seed = 1; seed <= kHandsToTry && !paid; ++seed) {
            ASSERT_TRUE(_game->StartMatch(seed).has_value());
            _game->GetSession().RunComputers();
            ASSERT_TRUE(_game->GetSession().NextHand().has_value());
            paid = _game->GetMatch().Scores() != SeatScores{2000, 2000, 2000, 2000};
        }
        ASSERT_TRUE(paid) << "one of the hands is won";

        Draw();

        const Match& match = _game->GetMatch();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const std::string text = Text("Hud", Format("Score{}", seat).c_str());
            EXPECT_NE(text.find(std::to_string(match.Scores()[seat])), std::string::npos) << text;
            EXPECT_NE(text.find(Format("Seat {}", seat + 1).c_str()), std::string::npos) << text;
        }
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Round: East, hand 2 of 16");
        EXPECT_EQ(Text("Hud", "WallLabel").substr(0, 6), "Wall: ");
    }

    // ----- the size matrix -----

    TEST_F(MahjongHudTest, TheHudPassesTheSizeMatrixWithTheLongestTextsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        SetWorstVars();

        ExpectPassesTheMatrix("Hud", kHudTouchMinimum);
        for (const Output& output : kOutputs) {
            ExpectWordsFit("Hud", output.width, output.height, kButtonAir);
        }
        EXPECT_GE(Element("Hud", "NewGame")->rect.h, kMinControlHeight);
    }

    TEST_F(MahjongHudTest, TheRowsOfEachPanelLieInsideItWithThePaddingKept) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        SetWorstVars();
        constexpr float kPanelPadding = 8.0f;

        const struct {
            const char* panel;
            std::vector<const char*> rows;
        } panels[] = {{"Info", {"RoundLabel", "WallLabel", "TurnLabel"}}, {"Scores", {"Score0", "Score1", "Score2", "Score3"}}};
        for (const auto& [panel, rows] : panels) {
            const UIElement* plate = Element("Hud", panel);
            ASSERT_NE(plate, nullptr) << panel;
            for (const char* name : rows) {
                const UIElement* row = Element("Hud", name);
                ASSERT_NE(row, nullptr) << name;
                EXPECT_GE(row->rect.x, plate->rect.x + kPanelPadding) << name;
                EXPECT_GE(row->rect.y, plate->rect.y + kPanelPadding) << name;
                EXPECT_LE(row->rect.x + row->rect.w, plate->rect.x + plate->rect.w - kPanelPadding) << name;
                EXPECT_LE(row->rect.y + row->rect.h, plate->rect.y + plate->rect.h - kPanelPadding) << name;
            }
        }
    }

    TEST_F(MahjongHudTest, TheSetupPanelPassesTheSizeMatrixAndEveryWordFitsAndEverySegmentIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());

        ExpectPassesTheMatrix("Setup", kHudTouchMinimum);
        for (const Output& output : kOutputs) {
            ExpectWordsFit("Setup", output.width, output.height, kButtonAir);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            for (const char* kind : {"Person", "Computer"}) {
                const UIElement* segment = Element("Setup", Format("Seat{}{}", seat, kind).c_str());
                ASSERT_NE(segment, nullptr);
                EXPECT_GE(segment->rect.h, kMinControlHeight) << seat << kind;
            }
        }
        EXPECT_GE(Element("Setup", "Start")->rect.h, kMinControlHeight);
    }

    TEST_F(MahjongHudTest, TheHudCoversNoTileOfTheWorstTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
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
        for (int i = 0; i < kWorstDiscards; ++i) {
            worst.discardSeats.push_back(i % kSeatCount);
        }
        worst.wallRemaining = 2 * kWallRingCells;
        const TableLayout layout = MakeTableLayout(worst);

        // The plates the table paints under the discard blocks and the wall ring, as the component pads them
        eastl::vector<Rect> plates;
        for (const Rect& zone : layout.zones.discards) {
            plates.push_back({zone.x - kPlatePadding, zone.y - kPlatePadding, zone.w + 2.0f * kPlatePadding, zone.h + 2.0f * kPlatePadding});
        }
        const Rect& wall = layout.zones.wall;
        plates.push_back({wall.x - kPlatePadding, wall.y - kPlatePadding, wall.w + 2.0f * kPlatePadding, wall.h + 2.0f * kPlatePadding});

        const auto apartFrom = [](const Rect& a, const Rect& b) { return a.x + a.w <= b.x || a.x >= b.x + b.w || a.y + a.h <= b.y || a.y >= b.y + b.h; };
        for (const char* name : {"Info", "Scores", "NewGame"}) {
            const UIElement* element = Element("Hud", name);
            ASSERT_NE(element, nullptr) << name;
            ASSERT_FALSE(element->rect.IsEmpty()) << name;
            for (const TileSlot& slot : layout.slots) {
                EXPECT_TRUE(apartFrom(slot.rect, element->rect)) << name << " covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
            }
            // A gap of one grid unit at least: panels that touch a plate edge to edge merge with it on screen
            const Rect aired{element->rect.x - kGridUnit, element->rect.y - kGridUnit, element->rect.w + 2.0f * kGridUnit, element->rect.h + 2.0f * kGridUnit};
            for (size_t i = 0; i < plates.size(); ++i) {
                EXPECT_TRUE(apartFrom(plates[i], aired)) << name << " is less than one grid unit from plate " << i;
            }
        }
    }

    // ----- the seat choice -----

    TEST_F(MahjongHudTest, TheGameStartsWithTheSeatChoiceOpenOnTheSeatsOfTheGame) {
        _game->SetSeats(kTwoPeople);
        _game->Start();
        EXPECT_TRUE(_game->IsSetupOpen());
        Draw();

        EXPECT_TRUE(SetupShown());
        EXPECT_EQ(Hud()->PendingSeats(), kTwoPeople);
        EXPECT_EQ(SeatKindVar(0), "person");
        EXPECT_EQ(SeatKindVar(1), "person");
        EXPECT_EQ(SeatKindVar(2), "computer");
        EXPECT_EQ(SeatKindVar(3), "computer");
        EXPECT_TRUE(Element("Setup", "Seat1Person")->Cast<UIButton>()->selected) << "the group shows the person";
        EXPECT_FALSE(Element("Setup", "Seat1Computer")->Cast<UIButton>()->selected);
        EXPECT_TRUE(Element("Setup", "Seat3Computer")->Cast<UIButton>()->selected);
    }

    TEST_F(MahjongHudTest, TheSegmentsChangeThePendingSeatsAndTheVariablesAndStartBeginsAMatchWithThem) {
        _game->Start();
        Draw();
        const uint64_t version = _game->GetSession().Version();

        ASSERT_TRUE(ClickElement("Setup", "Seat2Person"));
        ASSERT_TRUE(ClickElement("Setup", "Seat0Computer"));
        Draw();

        EXPECT_EQ(SeatKindVar(2), "person");
        EXPECT_EQ(SeatKindVar(0), "computer");
        EXPECT_EQ(Hud()->PendingSeats(), (SeatSetup{SeatKind::Computer, SeatKind::Computer, SeatKind::Person, SeatKind::Computer}));
        EXPECT_EQ(_game->GetSeats(), kDefaultSeatSetup) << "nothing is decided before Start";
        EXPECT_EQ(_game->GetSession().Version(), version);

        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();

        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(SetupShown());
        EXPECT_GT(_game->GetSession().Version(), version) << "a new match";
        EXPECT_EQ(_game->GetSession().KindOf(0), SeatKind::Computer);
        EXPECT_EQ(_game->GetSession().KindOf(2), SeatKind::Person);
        EXPECT_EQ(_game->GetSession().PersonCount(), 1);
        EXPECT_EQ(_game->GetMatch().HandsPlayed(), 0);
    }

    TEST_F(MahjongHudTest, ARefusedStartLeavesThePanelOpen) {
        _game->Start();
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = kUnplayableHandsPerMatch;
        _game->SetSettings(settings);
        Draw();
        const uint64_t version = _game->GetSession().Version();

        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();

        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(SetupShown());
        EXPECT_EQ(_game->GetSession().Version(), version);
    }

    TEST_F(MahjongHudTest, NewGameOpensThePanelOnTheSeatsOfTheLastMatch) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Draw();
        EXPECT_FALSE(SetupShown());

        ASSERT_TRUE(ClickElement("Hud", "NewGame"));
        Draw();

        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(SetupShown());
        EXPECT_EQ(Hud()->PendingSeats(), kTwoPeople);
        EXPECT_EQ(SeatKindVar(1), "person");
    }

    TEST_F(MahjongHudTest, AControlResetAndAControlActionCloseThePanel) {
        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());

        Json options = Json::object();
        options["seed"] = 3;
        options["seats"] = "PCCC";
        ASSERT_TRUE(control->Reset(options).has_value());
        Draw();
        EXPECT_FALSE(SetupShown());

        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());
        const auto actions = control->GetActions();
        ASSERT_TRUE(actions.has_value());
        ASSERT_FALSE(actions->empty());
        EXPECT_TRUE(SetupShown()) << "listing does not enter the game";
        ASSERT_TRUE(control->PerformAction((*actions)[0]).has_value());
        Draw();
        EXPECT_FALSE(SetupShown());
    }

    TEST_F(MahjongHudTest, NothingIsPlayedWhileThePanelIsOpen) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->OpenSetup();
        Draw();
        TableSession& session = _game->GetSession();
        const uint64_t version = session.Version();

        for (int i = 0; i < 5; ++i) {
            Elapse(kComputerStepSeconds * 2.0f);
        }
        EXPECT_EQ(session.Version(), version) << "no computer steps behind the panel";

        _game->CloseSetup();
        Elapse(kComputerStepSeconds);
        EXPECT_GT(session.Version(), version) << "and one steps when it is closed";
    }

    TEST_F(MahjongHudTest, NoClickAndNoButtonReachesTheTableWhileThePanelIsOpen) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Lay(MakeModel(SpecOf(kPlainHand, "9p"), kSeat0));
        Draw();
        ASSERT_EQ(_game->GetSession().Actor(), kSeat0);
        ASSERT_NE(HandSlot(0), nullptr);
        _game->OpenSetup();  // not drawn yet: the canvas is still hidden, so the click gets as far as the table
        const std::string before = Snapshot(Hand());

        ClickHandTile(0);
        EmitAction("MahjongPass");
        EmitAction("MahjongWin");

        EXPECT_EQ(Snapshot(Hand()), before);
    }

}  // namespace BECore::Mahjong::Tests
