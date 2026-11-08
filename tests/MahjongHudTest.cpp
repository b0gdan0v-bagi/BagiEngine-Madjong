#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"

#include <BECore/Control/Json.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>

/**
 * The HUD and the seat choice of the shipped scene (`Components/MahjongHudComponent.h`): the variables follow the session, the labels
 * show them, the panel `Setup` opens and closes with the game, and both canvases pass the size matrix with the longest texts.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kLongestSeat = "Seat 4 (North) You";
        /// Output sizes (and the air a button keeps) the words are asked in: the design size, a small window, the narrowest windows.
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};
        constexpr float kButtonAir = 8.0f;
        constexpr float kMinControlHeight = 48.0f;
        constexpr int kHandsToTry = 20;
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";

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

        std::string HandsChoiceVar() const {
            return Var("handsChoice");
        }

        bool HandsSelected(const char* segment) const {
            const UIElement* element = Element("Setup", segment);
            const auto* button = element != nullptr ? element->Cast<UIButton>() : nullptr;
            return button != nullptr && button->selected;
        }

        static void ExpectSameSettings(const MahjongSettings& actual, const MahjongSettings& expected) {
            EXPECT_EQ(actual.handsPerMatch, expected.handsPerMatch);
            EXPECT_EQ(actual.flowers, expected.flowers);
            EXPECT_EQ(actual.replayDrawnHand, expected.replayDrawnHand);
            EXPECT_EQ(actual.limit, expected.limit);
            EXPECT_EQ(actual.startingScore, expected.startingScore);
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

        // The labels are no touch target; the one button of the HUD is in `HudMenu`, a Screen canvas with the engine's touch minimum
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        ExpectPassesTheMatrix("Hud", minTouch);
        ExpectPassesTheMatrix("HudMenu", minTouch);
        for (const Output& output : kOutputs) {
            ExpectWordsFit("Hud", output.width, output.height, kButtonAir);
            ExpectWordsFit("HudMenu", output.width, output.height, kButtonAir);
        }
        EXPECT_GE(Element("HudMenu", "NewGame")->rect.h, kMinControlHeight);
    }

    TEST_F(MahjongHudTest, TheSetupPanelPassesTheSizeMatrixAndEveryWordFitsAndEverySegmentIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());

        ExpectPassesTheMatrix("Setup", CoreManager::GetUIEnvironment().GetSettings().minTouchSize);
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
        for (const char* hands : {"Hands16", "Hands8"}) {
            const UIElement* segment = Element("Setup", hands);
            ASSERT_NE(segment, nullptr) << hands;
            EXPECT_GE(segment->rect.h, kMinControlHeight) << hands;
        }
        EXPECT_GE(Element("Setup", "Start")->rect.h, kMinControlHeight);
    }

    TEST_F(MahjongHudTest, TheSetupPanelKeepsItsRowsInsideThePlateAndTheSafeAreaWithEveryWordFittingInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());
        auto setup = Canvas("Setup");
        ASSERT_TRUE(setup);
        EXPECT_EQ(setup->GetLayout().params.space, CanvasSpace::Screen) << "the seat choice is a Screen canvas";
        EXPECT_EQ(setup->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        const char* const rows[] = {"Title", "SeatRow0", "SeatRow1", "SeatRow2", "SeatRow3", "HandsRow", "Start"};
        const char* const groups[] = {"SeatKind0", "SeatKind1", "SeatKind2", "SeatKind3", "HandsChoice"};
        size_t windows = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            for (const float userScale : {1.0f, 1.5f}) {
                const std::string label = Format("{} at user scale {}", window.label.c_str(), userScale).c_str();
                const UICanvasResolved resolved = LayOutInWindow(*setup, window, CanvasScaleMode::ScaleWithScreen, userScale);
                const UIElement* plate = Element("Setup", "Plate");
                ASSERT_NE(plate, nullptr);
                ASSERT_FALSE(plate->rect.IsEmpty()) << label;
                const Rect& safe = resolved.contentBounds;
                EXPECT_GE(plate->rect.x, safe.x - 0.01f) << label;
                EXPECT_GE(plate->rect.y, safe.y - 0.01f) << label;
                EXPECT_LE(plate->rect.x + plate->rect.w, safe.x + safe.w + 0.01f) << label;
                EXPECT_LE(plate->rect.y + plate->rect.h, safe.y + safe.h + 0.01f) << label;

                // The rows lie in the plate, one under the other, and are touch targets as the user sees them
                const UIElement* previous = nullptr;
                for (const char* name : rows) {
                    const UIElement* row = Element("Setup", name);
                    ASSERT_NE(row, nullptr) << name;
                    ASSERT_FALSE(row->rect.IsEmpty()) << label << " " << name;
                    EXPECT_GE(row->rect.y, plate->rect.y - 0.01f) << label << " " << name;
                    EXPECT_LE(row->rect.y + row->rect.h, plate->rect.y + plate->rect.h + 0.01f) << label << " " << name;
                    if (previous != nullptr) {
                        EXPECT_GE(row->rect.y, previous->rect.y + previous->rect.h - 0.01f) << label << " " << name << " overlaps the row above";
                    }
                    previous = row;
                    if (row->interactive) {
                        EXPECT_GE(row->rect.h * resolved.scale, minTouch) << label << " " << name;
                    }
                }
                // Every segment lies in its row and is a touch target (the Computer segments are the narrower ones of a window)
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    const UIElement* row = Element("Setup", Format("SeatRow{}", seat).c_str());
                    for (const char* kind : {"Person", "Computer"}) {
                        const UIElement* segment = Element("Setup", Format("Seat{}{}", seat, kind).c_str());
                        ASSERT_TRUE(row != nullptr && segment != nullptr);
                        EXPECT_GE(segment->rect.x, row->rect.x - 0.01f) << label << " " << kind;
                        EXPECT_LE(segment->rect.x + segment->rect.w, row->rect.x + row->rect.w + 0.01f) << label << " " << kind;
                        EXPECT_GE(segment->rect.y, row->rect.y - 0.01f) << label << " " << kind;
                        EXPECT_LE(segment->rect.y + segment->rect.h, row->rect.y + row->rect.h + 0.01f) << label << " " << kind;
                        EXPECT_GE(std::min(segment->rect.w, segment->rect.h) * resolved.scale, minTouch) << label << " " << kind;
                    }
                }
                const UIElement* handsRow = Element("Setup", "HandsRow");
                for (const char* hands : {"Hands16", "Hands8"}) {
                    const UIElement* segment = Element("Setup", hands);
                    ASSERT_TRUE(handsRow != nullptr && segment != nullptr);
                    EXPECT_GE(segment->rect.y, handsRow->rect.y - 0.01f) << label << " " << hands;
                    EXPECT_LE(segment->rect.y + segment->rect.h, handsRow->rect.y + handsRow->rect.h + 0.01f) << label << " " << hands;
                    EXPECT_GE(std::min(segment->rect.w, segment->rect.h) * resolved.scale, minTouch) << label << " " << hands;
                }
                for (const char* name : groups) {
                    EXPECT_FALSE(Element("Setup", name)->rect.IsEmpty()) << label << " " << name;
                }

                // Every word, with the air a button keeps (the narrowest window at the user scale 1.5 is in this loop)
                std::vector<const UIElement*> controls;
                CollectControls(setup->GetRoot(), controls);
                for (const UIElement* control : controls) {
                    const float spare = control->interactive ? kButtonAir : 0.0f;
                    const UIContentFit fit = control->CheckContent(control->rect.w - 2.0f * spare, control->rect.h, resolved.scale);
                    EXPECT_TRUE(!fit.hasContent || fit.fits) << label << " " << control->name.ToStringView().data() << " cuts its text";
                }
            }
            ++windows;
        });
        EXPECT_EQ(windows, BECore::Tests::UILayoutMatrix().size());
    }

    TEST_F(MahjongHudTest, ThePlateOfTheSeatChoiceIsAsBigAsItWasWhereThereIsRoom) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();  // the window of the fixture is the design size
        const UIElement* plate = Element("Setup", "Plate");
        ASSERT_NE(plate, nullptr);
        EXPECT_NEAR(plate->rect.h, 452.0f, 0.01f);
        for (const char* row : {"SeatRow0", "SeatRow1", "SeatRow2", "SeatRow3", "HandsRow", "Start"}) {
            EXPECT_NEAR(Element("Setup", row)->rect.h, 56.0f, 0.01f) << row;
        }
        EXPECT_NEAR(Element("Setup", "Title")->rect.h, 44.0f, 0.01f);
    }

    TEST_F(MahjongHudTest, TheSeatChoiceDimsTheWholeWindowAndTheNewGameButtonUnderItTakesNoClick) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        auto setup = Canvas("Setup");
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(setup && menu);
        ASSERT_TRUE(menu->TakesInput()) << "the HUD button is shown while the seat choice is open";

        // In every window the dim (the whole window, bars included) covers the New game button, which lies on a bar in a narrow window
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            LayOutInWindow(*setup, window);
            LayOutInWindow(*menu, window);
            const UIElement* dim = Element("Setup", "Dim");
            const UIElement* button = Element("HudMenu", "NewGame");
            ASSERT_TRUE(dim != nullptr && button != nullptr);
            const Rect dimOutput = setup->GetLayout().GetResolved().transform.ToOutput(dim->rect.x, dim->rect.y, dim->rect.w, dim->rect.h);
            const Rect buttonOutput = menu->GetLayout().GetResolved().transform.ToOutput(button->rect.x, button->rect.y, button->rect.w, button->rect.h);
            EXPECT_LE(dimOutput.x, 0.01f) << window.label.c_str();
            EXPECT_LE(dimOutput.y, 0.01f) << window.label.c_str();
            EXPECT_GE(dimOutput.x + dimOutput.w, window.outputWidth - 0.01f) << window.label.c_str();
            EXPECT_GE(dimOutput.y + dimOutput.h, window.outputHeight - 0.01f) << window.label.c_str();
            EXPECT_GE(buttonOutput.x, dimOutput.x - 0.01f) << window.label.c_str();
            EXPECT_LE(buttonOutput.x + buttonOutput.w, dimOutput.x + dimOutput.w + 0.01f) << window.label.c_str();
            EXPECT_GE(buttonOutput.y, dimOutput.y - 0.01f) << window.label.c_str();
            EXPECT_LE(buttonOutput.y + buttonOutput.h, dimOutput.y + dimOutput.h + 0.01f) << window.label.c_str();
        });

        // And a click on it reaches the modal canvas, not the button: the choice made so far stays
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "Seat2Person"));
        const SeatSetup pending = Hud()->PendingSeats();
        ASSERT_EQ(pending[2], SeatKind::Person);
        ASSERT_TRUE(OnScreen("HudMenu", "NewGame"));
        ClickElement("HudMenu", "NewGame");
        EXPECT_TRUE(SetupShown());
        EXPECT_EQ(Hud()->PendingSeats(), pending) << "New game would have opened the panel again from the seats of the game";
    }

    TEST_F(MahjongHudTest, TheHudCoversNoTileOfTheWorstTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        const TableLayout layout = MakeWorstCaseTableLayout();

        for (const char* name : {"Info", "Scores"}) {
            const UIElement* element = Element("Hud", name);
            ASSERT_NE(element, nullptr) << name;
            ASSERT_FALSE(element->rect.IsEmpty()) << name;
            for (const TileSlot& slot : layout.slots) {
                const bool apart = slot.rect.x + slot.rect.w <= element->rect.x || slot.rect.x >= element->rect.x + element->rect.w || slot.rect.y + slot.rect.h <= element->rect.y ||
                                   slot.rect.y >= element->rect.y + element->rect.h;
                EXPECT_TRUE(apart) << name << " covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
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

    TEST_F(MahjongHudTest, ARefusedStartLeavesTheSettingsAsTheyWere) {
        _game->Start();
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = kUnplayableHandsPerMatch;
        settings.limit = 300;
        _game->SetSettings(settings);
        const SeatSetup seats = _game->GetSeats();
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "Seat1Person"));

        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();

        EXPECT_TRUE(SetupShown());
        ExpectSameSettings(_game->GetSettings(), settings);
        EXPECT_EQ(_game->GetSeats(), seats) << "the seats the panel offered are not taken either";
    }

    // ----- the number of hands -----

    TEST_F(MahjongHudTest, ThePanelOpensOnTheHandsOfTheGame) {
        _game->Start();
        Draw();

        ASSERT_TRUE(SetupShown());
        EXPECT_EQ(HandsChoiceVar(), "16");
        EXPECT_TRUE(HandsSelected("Hands16"));
        EXPECT_FALSE(HandsSelected("Hands8"));
        EXPECT_EQ(Hud()->PendingHands(), 16);

        _game->CloseSetup();
        Draw();
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = 8;
        _game->SetSettings(settings);
        _game->OpenSetup();
        Draw();

        EXPECT_EQ(HandsChoiceVar(), "8");
        EXPECT_FALSE(HandsSelected("Hands16"));
        EXPECT_TRUE(HandsSelected("Hands8"));
        EXPECT_EQ(Hud()->PendingHands(), 8);
    }

    TEST_F(MahjongHudTest, AnUnofferedNumberOfHandsLeavesNoSegmentSelectedAndIsKept) {
        _game->Start();
        _game->CloseSetup();
        Draw();
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = 12;
        _game->SetSettings(settings);
        _game->OpenSetup();
        Draw();

        EXPECT_EQ(HandsChoiceVar(), "12");
        EXPECT_FALSE(HandsSelected("Hands16"));
        EXPECT_FALSE(HandsSelected("Hands8"));
        EXPECT_EQ(Hud()->PendingHands(), 12);
    }

    TEST_F(MahjongHudTest, AClickOnAHandsSegmentChangesOnlyThePendingNumberAndTheVariable) {
        _game->Start();
        Draw();
        const uint64_t version = _game->GetSession().Version();
        const SeatSetup seats = _game->GetSeats();
        const MahjongSettings settings = _game->GetSettings();

        ASSERT_TRUE(ClickElement("Setup", "Hands8"));
        Draw();

        EXPECT_EQ(Hud()->PendingHands(), 8);
        EXPECT_EQ(HandsChoiceVar(), "8");
        EXPECT_TRUE(HandsSelected("Hands8"));
        EXPECT_FALSE(HandsSelected("Hands16"));
        EXPECT_EQ(_game->GetSettings().handsPerMatch, 16) << "nothing is decided before Start";
        ExpectSameSettings(_game->GetSettings(), settings);
        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetSeats(), seats);
        EXPECT_EQ(Hud()->PendingSeats(), seats);

        ASSERT_TRUE(ClickElement("Setup", "Hands16"));
        Draw();

        EXPECT_EQ(Hud()->PendingHands(), 16);
        EXPECT_EQ(HandsChoiceVar(), "16");
        EXPECT_TRUE(HandsSelected("Hands16"));
        EXPECT_FALSE(HandsSelected("Hands8"));
    }

    TEST_F(MahjongHudTest, StartAfterTheShortGameBeginsAMatchOfEightHandsWithTheSeatsOfTheSameVisit) {
        _game->Start();
        Draw();
        const uint64_t version = _game->GetSession().Version();

        ASSERT_TRUE(ClickElement("Setup", "Hands8"));
        ASSERT_TRUE(ClickElement("Setup", "Seat2Person"));
        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();

        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(SetupShown());
        EXPECT_GT(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetMatch().HandsTotal(), 8);
        EXPECT_EQ(_game->GetSettings().handsPerMatch, 8);
        EXPECT_EQ(Var("handsTotal"), "8");
        EXPECT_EQ(_game->GetSession().KindOf(2), SeatKind::Person);
        EXPECT_EQ(_game->GetSession().PersonCount(), 2);
    }

    TEST_F(MahjongHudTest, AStartFromThePanelChangesOnlyTheNumberOfHands) {
        MahjongSettings settings;
        settings.handsPerMatch = kFullMatchHands;
        settings.flowers = true;
        settings.replayDrawnHand = false;
        settings.limit = 300;
        settings.startingScore = 1500;
        _game->SetSettings(settings);
        _game->Start();
        Draw();

        ASSERT_TRUE(ClickElement("Setup", "Hands8"));
        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();

        const MahjongSettings after = _game->GetSettings();
        EXPECT_EQ(after.handsPerMatch, 8);
        EXPECT_TRUE(after.flowers) << "a setting of the control API is not undone";
        EXPECT_FALSE(after.replayDrawnHand);
        EXPECT_EQ(after.limit, 300);
        EXPECT_EQ(after.startingScore, 1500);
        EXPECT_EQ(_game->GetMatch().HandsTotal(), 8);
        EXPECT_EQ(_game->GetMatch().Scores(), (SeatScores{1500, 1500, 1500, 1500})) << "the match uses the settings";
    }

    TEST_F(MahjongHudTest, ThePanelNeverTurnsTheFlowersOn) {
        _game->Start();
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "Hands8"));
        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();

        EXPECT_FALSE(_game->GetSettings().flowers);
    }

    TEST_F(MahjongHudTest, TheChoiceOfHandsIsKeptForNewGameAndFollowsAControlReset) {
        _game->Start();
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "Hands8"));
        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();
        ASSERT_FALSE(SetupShown());

        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
        Draw();

        ASSERT_TRUE(SetupShown());
        EXPECT_EQ(Hud()->PendingHands(), 8);
        EXPECT_EQ(HandsChoiceVar(), "8");
        EXPECT_TRUE(HandsSelected("Hands8"));

        ASSERT_TRUE(ClickElement("Setup", "Start"));  // without touching the choice
        Draw();
        EXPECT_EQ(_game->GetMatch().HandsTotal(), 8);

        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        Json options = Json::object();
        options["hands"] = 16;
        ASSERT_TRUE(control->Reset(options).has_value());
        Draw();
        ASSERT_FALSE(SetupShown());
        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
        Draw();

        ASSERT_TRUE(SetupShown());
        EXPECT_EQ(Hud()->PendingHands(), 16);
        EXPECT_EQ(HandsChoiceVar(), "16");
        EXPECT_TRUE(HandsSelected("Hands16"));
    }

    TEST_F(MahjongHudTest, AValueThatIsNeitherSixteenNorEightChangesNothingAndNeitherDoesAClosedPanel) {
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());

        for (const char* value : {"12", "0", "-8", "08", "8 ", " 8", "eight", "16.0", ""}) {
            EmitAction("MahjongSetHands", value);
            EXPECT_EQ(Hud()->PendingHands(), 16) << "'" << value << "'";
            EXPECT_EQ(HandsChoiceVar(), "16") << "'" << value << "'";
        }

        _game->CloseSetup();
        Draw();
        ASSERT_FALSE(SetupShown());
        EmitAction("MahjongSetHands", "8");
        EXPECT_EQ(Hud()->PendingHands(), 16) << "the panel is closed";
        EXPECT_EQ(HandsChoiceVar(), "16");
    }

    TEST_F(MahjongHudTest, NewGameOpensThePanelOnTheSeatsOfTheLastMatch) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Draw();
        EXPECT_FALSE(SetupShown());

        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
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
