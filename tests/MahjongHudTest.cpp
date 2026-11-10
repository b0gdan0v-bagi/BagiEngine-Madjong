#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UIButton.h>
#include <Common/TempPath.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/MoveText.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/SeatPlates.h>

/**
 * The HUD and the seat choice of the shipped scene (`Components/MahjongHudComponent.h`): the variables follow the session and the language,
 * the labels show them, the panel `Setup` opens and closes with the game, and both canvases pass the size matrix with the longest texts
 * of every language.
 */
namespace BECore::Mahjong::Tests {

    namespace {

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
        /// The longest text each variable can have in the current language: found by code points among the real candidates (every wind, every
        /// seat with and without the mark of "you" at a table of one person, the mark of the dealer), not typed in.
        static void SetWorstVars() {
            const UIStrings& strings = CoreManager::GetUIStrings();
            std::vector<eastl::string> winds;
            std::vector<eastl::string> names;
            std::vector<eastl::string> plateWinds;
            for (int wind = 0; wind < kSeatCount; ++wind) {
                winds.push_back(WindName(strings, static_cast<Wind>(wind)));
                plateWinds.push_back(WindName(strings, static_cast<Wind>(wind)));
                plateWinds.push_back(WindName(strings, static_cast<Wind>(wind)) + "*");
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                SeatSetup onePerson{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
                onePerson[seat] = SeatKind::Person;
                names.push_back(SeatLabel(strings, seat, onePerson));
                names.push_back(SeatLabel(strings, seat, kAllComputers));
            }
            const eastl::string longestName = LongestText(names);
            const eastl::string longestWind = LongestText(plateWinds);
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("round"_intern, eastl::string_view(LongestText(winds)));
            vars.Set("hand"_intern, 16);
            vars.Set("handsTotal"_intern, 16);
            vars.Set("wall"_intern, 144);
            for (int position = 0; position < kSeatCount; ++position) {
                vars.Set(PoolString::Intern(Format("plateName{}", position)), eastl::string_view(longestName));
                vars.Set(PoolString::Intern(Format("plateWind{}", position)), eastl::string_view(longestWind));
                vars.Set(PoolString::Intern(Format("plateScore{}", position)), "-9999");
            }
        }

        std::string Text(const char* node, const char* element) const {
            const UIElement* found = Element(node, element);
            const UILabel* label = found != nullptr ? found->Cast<UILabel>() : nullptr;
            if (label == nullptr && found != nullptr) {
                label = found->Cast<UIButton>();  // a button is a label by inheritance, but `Cast` is exact
            }
            return label != nullptr ? std::string(label->GetDisplayText().data(), label->GetDisplayText().size()) : std::string("<no label>");
        }

        /// The Russian words of the winds, written out here and not taken from the table
        static std::string RussianWind(Wind wind) {
            const char* const winds[] = {"Восток", "Юг", "Запад", "Север"};
            return winds[static_cast<int>(wind)];
        }

        /// The row of the plate at screen position `position` on screen: "South*: 2000".
        std::string PlateRow(int position) const {
            return Text("SeatPlates", Format("PlateRow{}", position).c_str());
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

        /// Makes the element and everything under it visible, so that every text of the canvas is asked for.
        static void ShowAll(UICanvasComponent& canvas, UIElement& element) {
            canvas.SetElementVisible(element.name, true);
            for (const auto& child : element.children) {
                ShowAll(canvas, *child);
            }
        }

        /// Plays the match to its end between computers, the person clicking `Next hand` each time.
        void PlayMatchToTheEnd() {
            for (int hand = 0; hand < kHandsToTry * 3 && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
                _game->GetSession().RunComputers();
                ASSERT_TRUE(_game->GetSession().NextHand().has_value());
            }
            ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        }

        /// Whether the plate `MoveLine` is shown (not hidden by the component).
        bool MoveLineShown() const {
            const UIElement* line = Element("Hud", "MoveLine");
            return line != nullptr && line->style.visible;
        }

        /// The last move of the hand as the HUD should word it in the current language; empty when the hand has none.
        std::string ExpectedLastMove() const {
            if (Hand().Moves().empty()) {
                return {};
            }
            const HandMove& move = Hand().Moves().back();
            const eastl::string text = MoveText(CoreManager::GetUIStrings(), move, Hand().SeatWind(move.seat));
            return std::string(text.data(), text.size());
        }

        /// Steps the computers until the last move of the hand is of `kind`; false when the hand ended first.
        bool StepUntilMove(MoveKind kind) {
            while (_game->GetSession().GetPhase() == SessionPhase::Playing) {
                if (!_game->GetSession().StepComputer().has_value()) {
                    return false;
                }
                if (!Hand().Moves().empty() && Hand().Moves().back().kind == kind) {
                    return true;
                }
            }
            return false;
        }

        /// The longest sentence of the move line in the current language: every kind of move with every seat, wind and the longest tile names.
        static eastl::string LongestMoveText() {
            const UIStrings& strings = CoreManager::GetUIStrings();
            std::vector<eastl::string> candidates;
            const Tile tiles[] = {T("9m"), T("Ew"), T("Rd"), T("Wd"), T("Gd"), T("F1"), T("T4"), T("5s")};
            for (int wind = 0; wind < kSeatCount; ++wind) {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    for (const MoveKind kind : {MoveKind::Discard, MoveKind::ClaimChow, MoveKind::ClaimPung, MoveKind::ClaimKong, MoveKind::ConcealedKong, MoveKind::UpgradeKong}) {
                        for (const Tile tile : tiles) {
                            for (const bool declaring : {false, true}) {
                                candidates.push_back(MoveText(strings, HandMove{.kind = kind, .seat = seat, .tile = tile, .declaredCalling = declaring}, static_cast<Wind>(wind)));
                            }
                        }
                    }
                }
            }
            return LongestText(candidates);
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
        EXPECT_EQ(Var("turn"), "") << "the turn is marked on the table, not worded in the HUD";
        for (int position = 0; position < kSeatCount; ++position) {
            // One person at seat 0: it sits at the bottom, the others follow counter-clockwise
            const Seat seat = SeatAtPosition(0, position);
            EXPECT_EQ(Var(Format("plateName{}", position).c_str()), Format("Seat {}", seat + 1).c_str() + std::string(seat == 0 ? " You" : ""))
                << "only the one person is marked, position " << position;
            const std::string wind = EnumUtils<Wind>::ToString(Hand().SeatWind(seat)).data();
            EXPECT_EQ(Var(Format("plateWind{}", position).c_str()), seat == match.East() ? wind + "*" : wind) << "the dealer is marked, position " << position;
            EXPECT_EQ(Var(Format("plateScore{}", position).c_str()), std::to_string(match.Scores()[seat]));
        }
        for (const char* gone : {"seatName0", "seatWind0", "seatScore0", "seatMark0", "seatDealer0"}) {
            EXPECT_EQ(Var(gone), "") << gone << " is not published any more";
        }
    }

    TEST_F(MahjongHudTest, ThePlatesFollowTheBottomSeatOfAPersonElsewhereAndOfASpectator) {
        for (Seat person = 0; person < kSeatCount; ++person) {
            SeatSetup seats = kAllComputers;
            seats[person] = SeatKind::Person;
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Draw();
            for (int position = 0; position < kSeatCount; ++position) {
                const Seat seat = SeatAtPosition(person, position);
                EXPECT_EQ(Var(Format("plateName{}", position).c_str()), Format("Seat {}", seat + 1).c_str() + std::string(seat == person ? " You" : ""))
                    << "person " << person << " position " << position;
                EXPECT_EQ(Var(Format("plateScore{}", position).c_str()), std::to_string(_game->GetMatch().Scores()[seat]));
            }
        }

        // A spectator sees the table from seat 0, nobody is "you"
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        for (int position = 0; position < kSeatCount; ++position) {
            EXPECT_EQ(Var(Format("plateName{}", position).c_str()), Format("Seat {}", position + 1).c_str());
        }
    }

    TEST_F(MahjongHudTest, TheBottomPlateOfAHotSeatTableIsTheViewerAfterReveal) {
        // Seat 1 is East and to act: the cover waits for that person, who is at the bottom, so seat 1 is at position 0 and seat 2 at position 1
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Lay(MakeModel(SpecOf(kPlainHand, "9p"), kSeat1));
        _game->GetSession().Reveal();
        Draw();
        ASSERT_EQ(BottomSeat(_game->GetSession()), kSeat1);
        EXPECT_EQ(Var("plateName0"), "Seat 2") << "nobody is 'you' at a table of two";
        EXPECT_EQ(Var("plateName1"), "Seat 3");
        for (int position = 0; position < kSeatCount; ++position) {
            const Seat seat = SeatAtPosition(kSeat1, position);
            EXPECT_EQ(Var(Format("plateScore{}", position).c_str()), std::to_string(_game->GetMatch().Scores()[seat]));
            EXPECT_EQ(Var(Format("plateWind{}", position).c_str()).back() == '*', seat == kSeat1) << "the dealer mark is on the East plate only";
        }
    }

    TEST_F(MahjongHudTest, NoOneIsMarkedYouWhenTwoPeoplePlayOrNobodyDoes) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Draw();
        for (int position = 0; position < kSeatCount; ++position) {
            EXPECT_EQ(Var(Format("plateName{}", position).c_str()).find("You"), std::string::npos) << position;
        }

        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        for (int position = 0; position < kSeatCount; ++position) {
            EXPECT_EQ(Var(Format("plateName{}", position).c_str()).find("You"), std::string::npos) << position;
        }
    }

    TEST_F(MahjongHudTest, TheVariablesFollowAnActionAndAreNotSetAgainWhileTheVersionStandsStill) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        const uint64_t varsVersion = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), varsVersion) << "an unchanged table sets nothing";

        const uint64_t version = _game->GetSession().Version();
        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());  // a discard
        ASSERT_GT(_game->GetSession().Version(), version);
        Draw();

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
            paid = _game->GetMatch().Scores() != SeatScores{kExpectedStartingScore, kExpectedStartingScore, kExpectedStartingScore, kExpectedStartingScore};
        }
        ASSERT_TRUE(paid) << "one of the hands is won";

        Draw();

        const Match& match = _game->GetMatch();
        for (int position = 0; position < kSeatCount; ++position) {
            const Seat seat = SeatAtPosition(0, position);  // a table of computers is seen from seat 0
            const std::string name = Text("SeatPlates", Format("PlateName{}", position).c_str());
            const std::string row = PlateRow(position);
            EXPECT_EQ(name, Format("Seat {}", seat + 1).c_str()) << "seat " << seat;
            const std::string wind = EnumUtils<Wind>::ToString(Hand().SeatWind(seat)).data();
            EXPECT_EQ(row, (seat == match.East() ? wind + "*" : wind) + ": " + std::to_string(match.Scores()[seat])) << "seat " << seat;
        }
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Round: East, hand 2 of 16");
        EXPECT_EQ(Text("Hud", "WallLabel").substr(0, 6), "Wall: ");
    }

    TEST_F(MahjongHudTest, TheVariablesAndTheLabelsAreInRussianWhenTheLanguageIsRussian) {
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        EXPECT_EQ(Var("round"), "Восток");
        for (int position = 0; position < kSeatCount; ++position) {
            const Seat seat = SeatAtPosition(0, position);
            EXPECT_EQ(Var(Format("plateName{}", position).c_str()), "Место " + std::to_string(seat + 1) + (seat == 0 ? " Вы" : "")) << "only the one person is marked, seat " << seat;
            const std::string wind = RussianWind(Hand().SeatWind(seat));
            EXPECT_EQ(Var(Format("plateWind{}", position).c_str()), seat == _game->GetMatch().East() ? wind + "*" : wind);
            EXPECT_EQ(Var(Format("plateScore{}", position).c_str()), std::to_string(_game->GetMatch().Scores()[seat])) << "numbers are the same";
            EXPECT_EQ(PlateRow(position), Var(Format("plateWind{}", position).c_str()) + ": " + Var(Format("plateScore{}", position).c_str()));
        }
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Раунд: Восток, раздача 1 из 16");
        EXPECT_EQ(Text("Hud", "WallLabel"), "Стена: " + std::to_string(Hand().GetWall().LiveCount()));
        EXPECT_EQ(Text("HudMenu", "NewGame"), "Новая игра");

        for (const SeatSetup& seats : {kTwoPeople, kAllComputers}) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Draw();
            for (int position = 0; position < kSeatCount; ++position) {
                EXPECT_EQ(Var(Format("plateName{}", position).c_str()).find("Вы"), std::string::npos) << "no one is 'you' at a table of " << (seats == kTwoPeople ? "two" : "no") << " people";
            }
        }
    }

    TEST_F(MahjongHudTest, TheRowsOfEachPanelLieInsideItWithThePaddingKept) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        SetWorstVars();
        constexpr float kPanelPadding = 8.0f;

        const auto expectInside = [&](const char* node, const char* name, const Rect& plate) {
            const UIElement* row = Element(node, name);
            ASSERT_NE(row, nullptr) << name;
            EXPECT_GE(row->rect.x, plate.x + kPanelPadding) << name;
            EXPECT_GE(row->rect.y, plate.y + kPanelPadding) << name;
            EXPECT_LE(row->rect.x + row->rect.w, plate.x + plate.w - kPanelPadding) << name;
            EXPECT_LE(row->rect.y + row->rect.h, plate.y + plate.h - kPanelPadding) << name;
        };
        const UIElement* info = Element("Hud", "Info");
        ASSERT_NE(info, nullptr);
        for (const char* name : {"RoundLabel", "WallLabel"}) {
            expectInside("Hud", name, info->rect);
        }
        // The plate of a seat is painted by the table: its rectangle is the layout's (the viewer of the worst-case layout is seat 0)
        const TableLayout layout = MakeWorstCaseTableLayout();
        for (int position = 0; position < kSeatCount; ++position) {
            const Rect& plate = layout.zones.seatPlates[SeatAtPosition(layout.viewer, position)];
            expectInside("SeatPlates", Format("PlateName{}", position).c_str(), plate);
            expectInside("SeatPlates", Format("PlateRow{}", position).c_str(), plate);
        }
    }

    TEST_F(MahjongHudTest, TheSeatPlatesLieAtTheRectanglesOfTheLayoutAndClearOfEverythingElseOnTheTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        // Everything that hides in the file is shown, so that its place is asked for
        for (const char* node : {"Hud", "Actions", "Hints", "HintsButton", "SeatPlates"}) {
            const auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            ShowAll(*canvas, canvas->GetRoot());
            canvas->UpdateLayout(kViewWidth, kViewHeight);
        }
        const TableLayout layout = MakeWorstCaseTableLayout();

        // The file is by screen position and the viewer of the worst-case layout is seat 0, so position p is seat p: the two labels of a
        // plate are the plate without its padding of 8
        constexpr float kPlatePad = 8.0f;
        eastl::vector<Rect> plates;
        for (int position = 0; position < kSeatCount; ++position) {
            const UIElement* name = Element("SeatPlates", Format("PlateName{}", position).c_str());
            const UIElement* row = Element("SeatPlates", Format("PlateRow{}", position).c_str());
            ASSERT_NE(name, nullptr) << position;
            ASSERT_NE(row, nullptr) << position;
            const Rect& expected = layout.zones.seatPlates[SeatAtPosition(layout.viewer, position)];
            EXPECT_FLOAT_EQ(name->rect.x, expected.x + kPlatePad) << position;
            EXPECT_FLOAT_EQ(name->rect.y, expected.y + kPlatePad) << position;
            EXPECT_FLOAT_EQ(name->rect.w, expected.w - 2 * kPlatePad) << position;
            EXPECT_FLOAT_EQ(row->rect.x, expected.x + kPlatePad) << position;
            EXPECT_FLOAT_EQ(row->rect.y + row->rect.h, expected.y + expected.h - kPlatePad) << position;
            EXPECT_FLOAT_EQ(row->rect.w, expected.w - 2 * kPlatePad) << position;
            EXPECT_FALSE(name->IsHitTestable() || row->IsHitTestable()) << "a plate takes no input";
            plates.push_back(expected);
        }

        // The panels and the areas of the buttons that lie near: a grid unit clear of every plate
        struct Neighbour {
            const char* node;
            const char* element;
        };
        const Neighbour neighbours[] = {{"Hud", "Info"},        {"Hud", "MoveLine"},     {"Actions", "Hint"},          {"Actions", "ActionRow"},
                                        {"Hints", "WaitPanel"}, {"Hints", "AdviceText"}, {"HintsButton", "HintButton"}};
        for (const Neighbour& neighbour : neighbours) {
            const UIElement* element = Element(neighbour.node, neighbour.element);
            ASSERT_NE(element, nullptr) << neighbour.element;
            ASSERT_FALSE(element->rect.IsEmpty()) << neighbour.element;
            for (size_t i = 0; i < plates.size(); ++i) {
                EXPECT_TRUE(Apart(Grown(plates[i], kGridUnit), element->rect)) << neighbour.element << " is less than a grid unit from the plate at position " << i;
            }
        }
    }

    TEST_F(MahjongHudTest, ThePlatesSayWhoIsEastAfterTheDealMovesAndTheScoresAfterAHand) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        bool moved = false;
        for (uint32_t seed = 1; seed <= kHandsToTry && !moved; ++seed) {
            ASSERT_TRUE(_game->StartMatch(seed).has_value());
            const Seat east = _game->GetMatch().East();
            _game->GetSession().RunComputers();
            ASSERT_TRUE(_game->GetSession().NextHand().has_value());
            moved = _game->GetMatch().East() != east && _game->GetMatch().Scores() != SeatScores{kExpectedStartingScore, kExpectedStartingScore, kExpectedStartingScore, kExpectedStartingScore};
        }
        ASSERT_TRUE(moved) << "one of the hands is won by a seat that is not East";

        Draw();
        const Match& match = _game->GetMatch();
        for (int position = 0; position < kSeatCount; ++position) {
            const Seat seat = SeatAtPosition(0, position);
            const bool east = seat == match.East();
            EXPECT_EQ(Var(Format("plateWind{}", position).c_str()).back() == '*', east) << "the mark is on the seat that deals now, seat " << seat;
            EXPECT_EQ(Var(Format("plateScore{}", position).c_str()), std::to_string(match.Scores()[seat]));
            EXPECT_EQ(Hand().SeatWind(seat) == Wind::East, east);
        }
    }

    TEST_F(MahjongHudTest, ALanguageSwitchPutsTheWordsAgainAndNothingIsSetAgainWhileNothingChanges) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        EXPECT_EQ(Var("round"), "East");
        const uint64_t varsVersion = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), varsVersion) << "an unchanged table sets nothing";

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();

        EXPECT_NE(CoreManager::GetUIVars().GetVersion(), varsVersion) << "the language is a change";
        EXPECT_EQ(Var("round"), "Восток");
        EXPECT_EQ(Var("plateName0").find("Место 1 "), 0u) << Var("plateName0");
        EXPECT_EQ(Text("SeatPlates", "PlateName0").find("Место 1 "), 0u) << Text("SeatPlates", "PlateName0");
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Раунд: Восток, раздача 1 из 16");
        const uint64_t russian = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), russian) << "and unchanged again in the new language";

        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Draw();
        EXPECT_EQ(Var("round"), "East");
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Round: East, hand 1 of 16");
    }

    TEST_F(MahjongHudTest, TheSeatChoiceIsWordedInTheLanguageOfTheTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        EXPECT_EQ(Text("Setup", "Title"), "New game: who plays?");
        EXPECT_EQ(Text("Setup", "SeatCaption0"), "Seat 1");
        EXPECT_EQ(Text("Setup", "Seat3Computer"), "Computer");
        EXPECT_EQ(Text("Setup", "Start"), "Start");

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();
        EXPECT_EQ(Text("Setup", "Title"), "Новая игра: кто играет?");
        EXPECT_EQ(Text("Setup", "SeatCaption0"), "Место 1");
        EXPECT_EQ(Text("Setup", "SeatCaption3"), "Место 4");
        EXPECT_EQ(Text("Setup", "Seat1Person"), "Человек");
        EXPECT_EQ(Text("Setup", "Seat3Computer"), "Компьютер");
        EXPECT_EQ(Text("Setup", "Start"), "Начать");
        EXPECT_EQ(SeatKindVar(1), "computer") << "the values of the segments are ids, not words";
    }

    TEST_F(MahjongHudTest, NoKeyIsMissingWhenEveryCanvasOfTheSceneIsShownInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Scene& scene = *_manager->GetSceneByName("MahjongScene"_intern);
        for (const char* node : {"Actions", "ActionsCompact", "Hud", "SeatPlates", "HudMenu", "Handoff", "HandOver", "HandOverAction", "MatchOver", "Setup", "Rules", "RulesAction"}) {
            UICanvasComponent::SetVisibleByName(scene, PoolString::Intern(node), true);
            const auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            ShowAll(*canvas, canvas->GetRoot());
        }

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            Draw();
            Draw();
            for (const auto& missing : CoreManager::GetUIStrings().GetMissing()) {
                ADD_FAILURE() << "the key " << missing.key.c_str() << " has no text in " << missing.language.ToStringView().data() << " (drawn in " << language.ToStringView().data() << ")";
            }
        });
        EXPECT_TRUE(CoreManager::GetUIStrings().GetMissing().empty());
    }

    // ----- the size matrix -----

    TEST_F(MahjongHudTest, TheHudPassesTheSizeMatrixInEveryLanguageWithTheLongestTextsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            SetWorstVars();

            // The labels are no touch target; the one button of the HUD is in `HudMenu`, a Screen canvas with the engine's touch minimum
            const float minTouch = EngineTouchMinimum();
            ExpectPassesTheMatrix("Hud", minTouch);
            ExpectPassesTheMatrix("SeatPlates", minTouch);
            ExpectPassesTheMatrix("HudMenu", minTouch);
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Hud", output.width, output.height, kButtonAir);
                ExpectWordsFit("SeatPlates", output.width, output.height, kButtonAir);
                ExpectWordsFit("HudMenu", output.width, output.height, kButtonAir);
            }
            EXPECT_GE(Element("HudMenu", "NewGame")->rect.h, kMinControlHeight);
            // Both buttons of the menu are shown together, the word of each in the language of the table
            ASSERT_TRUE(OnScreen("HudMenu", "Rules"));
            ASSERT_TRUE(OnScreen("HudMenu", "NewGame"));
            EXPECT_GE(Element("HudMenu", "Rules")->rect.h, kMinControlHeight);
            EXPECT_EQ(Text("HudMenu", "Rules"), Tr(CoreManager::GetUIStrings(), "hud.rules").c_str());
            EXPECT_EQ(Text("HudMenu", "NewGame"), Tr(CoreManager::GetUIStrings(), "hud.newGame").c_str());
        });
    }

    TEST_F(MahjongHudTest, TheSetupPanelPassesTheSizeMatrixInEveryLanguageAndEveryWordFitsAndEverySegmentIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());

            // In the design size the controls are as high as main's anatomy has them
            Draw();
            Canvas("Setup")->UpdateLayout(kViewWidth, kViewHeight);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (const char* kind : {"Person", "Computer"}) {
                    const UIElement* segment = Element("Setup", Format("Seat{}{}", seat, kind).c_str());
                    ASSERT_NE(segment, nullptr);
                    EXPECT_GE(segment->rect.h, kMinControlHeight) << seat << kind;
                }
            }
            for (const char* segmentName : {"LanguageEn", "LanguageRu", "Hands16", "Hands8", "HintsOn", "HintsOff"}) {
                const UIElement* segment = Element("Setup", segmentName);
                ASSERT_NE(segment, nullptr) << segmentName;
                EXPECT_GE(segment->rect.h, kMinControlHeight) << segmentName;
            }
            EXPECT_GE(Element("Setup", "Start")->rect.h, kMinControlHeight);

            // In every window, scale mode and user scale: the engine's touch minimum, and every word whole
            ExpectPassesTheMatrix("Setup", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Setup", output.width, output.height, kButtonAir);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("Setup"), kButtonAir);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

    TEST_F(MahjongHudTest, TheSeatChoiceFollowsTheModalAnatomyAndTheGridGaps) {
        constexpr float kRowPadding = 4.0f;  // the seven rows of 64 round a 56 segment: rows of 72 would not leave room for the plate
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());
        Canvas("Setup")->UpdateLayout(kViewWidth, kViewHeight);

        const Rect& plate = Element("Setup", "Plate")->rect;
        const UIElement* title = Element("Setup", "Title");
        ASSERT_NE(title, nullptr);
        EXPECT_TRUE(SameColor(title->color, PaletteColor("header")));
        EXPECT_TRUE(SameColor(Element("Setup", "Plate")->color, PaletteColor("surface")));
        EXPECT_EQ(title->rect.x, plate.x + kModalPadding);
        EXPECT_EQ(title->rect.y, plate.y + kModalPadding);
        EXPECT_EQ(title->rect.h, kModalTitleHeight);

        // The rows from the top: the language, the four seats, the number of hands, the hints; each a raised band with a muted caption and a group
        struct Row {
            std::string row, caption, group;
            std::vector<std::string> segments;
        };
        std::vector<Row> rows{{"LanguageRow", "LanguageCaption", "Language", {"LanguageEn", "LanguageRu"}}};
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            rows.push_back({Format("SeatRow{}", seat).c_str(),
                            Format("SeatCaption{}", seat).c_str(),
                            Format("SeatKind{}", seat).c_str(),
                            {Format("Seat{}Person", seat).c_str(), Format("Seat{}Computer", seat).c_str()}});
        }
        rows.push_back({"HandsRow", "HandsCaption", "HandsChoice", {"Hands16", "Hands8"}});
        rows.push_back({"HintsRow", "HintsCaption", "HintsChoice", {"HintsOn", "HintsOff"}});

        const Rect* above = &title->rect;
        for (const Row& entry : rows) {
            SCOPED_TRACE(entry.row);
            const UIElement* row = Element("Setup", entry.row.c_str());
            ASSERT_NE(row, nullptr);
            EXPECT_TRUE(SameColor(row->color, PaletteColor("surfaceRaised")));
            EXPECT_EQ(GapBelow(*above, row->rect), kModalGap);
            above = &row->rect;

            const UIElement* caption = Element("Setup", entry.caption.c_str());
            ASSERT_NE(caption, nullptr);
            EXPECT_TRUE(SameColor(caption->Cast<UILabel>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(caption->rect.x, row->rect.x + kRowPadding);
            const UIElement* group = Element("Setup", entry.group.c_str());
            ASSERT_NE(group, nullptr);
            EXPECT_EQ(group->rect.x, caption->rect.x + caption->rect.w + kModalGap);
            EXPECT_EQ(group->rect.x + group->rect.w, row->rect.x + row->rect.w - kRowPadding) << "the group ends inside the row's padding";
            EXPECT_EQ(group->rect.y, row->rect.y + kRowPadding);
            EXPECT_EQ(group->rect.y + group->rect.h, row->rect.y + row->rect.h - kRowPadding);

            for (const std::string& name : entry.segments) {
                const UIElement* element = Element("Setup", name.c_str());
                const auto* segment = element != nullptr ? element->Cast<UIButton>() : nullptr;
                ASSERT_NE(segment, nullptr) << name;
                EXPECT_TRUE(SameColor(segment->selectedColor, PaletteColor("primary"))) << name << ": the chosen segment is the confirming colour";
                EXPECT_TRUE(SameColor(segment->color, PaletteColor("neutral"))) << name << ": the others are neutral";
                EXPECT_GE(segment->rect.x, group->rect.x);
                EXPECT_LE(segment->rect.x + segment->rect.w, group->rect.x + group->rect.w);
                EXPECT_EQ(segment->rect.h, group->rect.h);
            }
        }

        const UIElement* start = Element("Setup", "Start");
        ASSERT_NE(start, nullptr);
        EXPECT_TRUE(SameColor(start->color, PaletteColor("primary")));
        EXPECT_EQ(start->rect.h, kMainButtonHeight);
        EXPECT_EQ(GapBelow(*above, start->rect), kModalSectionGap) << "Start is set apart from the rows by 16";
        EXPECT_EQ(start->rect.x, plate.x + kModalPadding);
        EXPECT_EQ(start->rect.y + start->rect.h, plate.y + plate.h - kModalPadding) << "the plate holds Start and nothing more";
        EXPECT_LE(plate.y + plate.h, kViewHeight);
    }

    TEST_F(MahjongHudTest, TheSelectedSegmentIsDrawnInTheFillOfThePrimaryRole) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());

        const auto* chosen = Element("Setup", "Seat0Person")->Cast<UIButton>();
        const auto* other = Element("Setup", "Seat0Computer")->Cast<UIButton>();
        ASSERT_TRUE(chosen != nullptr && other != nullptr);
        ASSERT_TRUE(chosen->selected);
        ASSERT_FALSE(other->selected);
        EXPECT_TRUE(SameColor(chosen->GetCurrentFill(), PaletteColor("primary"))) << "the choice is read from the colour, not only from the bar";
        EXPECT_FALSE(SameColor(other->GetCurrentFill(), PaletteColor("primary")));
        EXPECT_TRUE(SameColor(other->GetCurrentFill(), PaletteColor("neutral")));
    }

    TEST_F(MahjongHudTest, TheHudCoversNoTileOfTheWorstTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        const TableLayout layout = MakeWorstCaseTableLayout();

        // The plates the table paints under the discard blocks and the wall ring
        const eastl::vector<Rect> plates = ZonePlates(layout);

        // The line of the last move is hidden until a move is made: shown here, so that its place is checked too
        Canvas("Hud")->SetElementVisible("MoveLine"_intern, true);

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();  // laid out again with the words of this language
            for (const char* name : {"Info", "MoveLine"}) {
                const UIElement* element = Element("Hud", name);
                ASSERT_NE(element, nullptr) << name;
                ASSERT_FALSE(element->rect.IsEmpty()) << name;
                for (const TileSlot& slot : layout.slots) {
                    EXPECT_TRUE(Apart(slot.rect, element->rect)) << name << " covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                }
                // A gap of one grid unit at least: panels that touch a plate edge to edge merge with it on screen
                const Rect aired = Grown(element->rect, kGridUnit);
                for (size_t i = 0; i < plates.size(); ++i) {
                    EXPECT_TRUE(Apart(plates[i], aired)) << name << " is less than one grid unit from plate " << i;
                }
            }
        });
    }

    // ----- the move line -----

    TEST_F(MahjongHudTest, TheMoveLineIsEmptyAtTheStartAndWordsTheLastMoveAfterwards) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        EXPECT_EQ(Var("lastMove"), "");
        EXPECT_FALSE(MoveLineShown()) << "no empty plate";

        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());  // East's discard
        Draw();

        ASSERT_EQ(Hand().Moves().size(), 1u);
        const HandMove& move = Hand().Moves().back();
        EXPECT_EQ(move.kind, MoveKind::Discard);
        EXPECT_EQ(Var("lastMove"), ExpectedLastMove());
        EXPECT_EQ(Var("lastMove").find("Seat " + std::to_string(move.seat + 1) + " ("), 0u) << Var("lastMove");
        EXPECT_NE(Var("lastMove").find(" discarded "), std::string::npos) << Var("lastMove");
        EXPECT_TRUE(MoveLineShown());
        EXPECT_TRUE(OnScreen("Hud", "MoveLine"));
        EXPECT_EQ(Text("Hud", "MoveText"), Var("lastMove"));
    }

    TEST_F(MahjongHudTest, TheMoveLineWordsAClaimAndTheNextDiscardAndAnEmptyLineGoesWhenANewHandStarts) {
        bool claimed = false;
        for (uint32_t seed = 1; seed <= kHandsToTry && !claimed; ++seed) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers, seed));
            claimed = StepUntilMove(MoveKind::ClaimPung);
        }
        ASSERT_TRUE(claimed) << "a pung is claimed in one of the hands";
        Draw();

        EXPECT_EQ(Var("lastMove"), ExpectedLastMove());
        EXPECT_NE(Var("lastMove").find(" claimed "), std::string::npos) << Var("lastMove");
        EXPECT_NE(Var("lastMove").find(" for a pung"), std::string::npos) << Var("lastMove");
        EXPECT_TRUE(MoveLineShown());

        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());  // the claimer discards
        Draw();
        EXPECT_EQ(Hand().Moves().back().kind, MoveKind::Discard);
        EXPECT_EQ(Var("lastMove"), ExpectedLastMove());

        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));  // a new match: no move yet
        Draw();
        EXPECT_EQ(Var("lastMove"), "");
        EXPECT_FALSE(MoveLineShown());
    }

    TEST_F(MahjongHudTest, TheMoveLineIsEmptyWhenTheHandIsOverAndTheEndScreensSpeak) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        _game->GetSession().RunComputers();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        ASSERT_FALSE(Hand().Moves().empty());

        Draw();

        EXPECT_EQ(Var("lastMove"), "");
        EXPECT_FALSE(MoveLineShown());
        ASSERT_TRUE(_game->GetSession().NextHand().has_value());
        Draw();
        EXPECT_EQ(Var("lastMove"), "") << "and the next hand has no move yet";
    }

    TEST_F(MahjongHudTest, TheMoveLineIsWordedAgainInTheOtherLanguageAndNothingIsSetWhileNothingChanges) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        Draw();
        const std::string english = Var("lastMove");
        ASSERT_FALSE(english.empty());
        const uint64_t varsVersion = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), varsVersion) << "an unchanged table sets nothing";

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();

        EXPECT_NE(Var("lastMove"), english);
        EXPECT_EQ(Var("lastMove"), ExpectedLastMove());
        EXPECT_EQ(Var("lastMove").find("Место "), 0u) << Var("lastMove");
        EXPECT_NE(Var("lastMove").find(" сбросило "), std::string::npos) << Var("lastMove");
        EXPECT_EQ(Text("Hud", "MoveText"), Var("lastMove"));
        const uint64_t russian = CoreManager::GetUIVars().GetVersion();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), russian);

        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Draw();
        EXPECT_EQ(Var("lastMove"), english);
    }

    TEST_F(MahjongHudTest, TheMoveLineNamesNoTileOfAConcealedKong) {
        // A concealed kong is public as a fact only: the sentence names the seat and no tile
        bool declared = false;
        for (uint32_t seed = 1; seed <= 200 && !declared; ++seed) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers, seed));
            declared = StepUntilMove(MoveKind::ConcealedKong);
        }
        if (!declared) {
            GTEST_SKIP() << "no computer declared a concealed kong in these seeds (MoveTextTest holds the sentence)";
        }
        Draw();
        EXPECT_EQ(Var("lastMove"), ExpectedLastMove());
        EXPECT_NE(Var("lastMove").find("concealed kong"), std::string::npos) << Var("lastMove");
        for (const char* word : {"Dots", "Bamboo", "Characters", "Dragon", "Wind"}) {
            EXPECT_EQ(Var("lastMove").find(word), std::string::npos) << Var("lastMove");
        }
    }

    TEST_F(MahjongHudTest, TheMoveLinePassesTheSizeMatrixInEveryLanguageWithTheLongestSentenceAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        Draw();
        ASSERT_TRUE(MoveLineShown());

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            SetWorstVars();
            const eastl::string longest = LongestMoveText();
            ASSERT_FALSE(longest.empty());
            CoreManager::GetUIVars().Set("lastMove"_intern, eastl::string_view(longest));

            ExpectPassesTheMatrix("Hud", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Hud", output.width, output.height, kButtonAir);
            }
        });
    }

    // ----- the seat choice -----

    // ----- the language row -----

    TEST_F(MahjongHudTest, TheLanguageRowShowsTheLanguageOfTheGameAndEachLanguageIsWrittenInItself) {
        _game->Start();
        Draw();
        ASSERT_TRUE(SetupShown());

        EXPECT_EQ(Var("language"), "en");
        EXPECT_EQ(Text("Setup", "LanguageCaption"), "Language");
        EXPECT_EQ(Text("Setup", "LanguageEn"), "English");
        EXPECT_EQ(Text("Setup", "LanguageRu"), "Русский");
        EXPECT_TRUE(Element("Setup", "LanguageEn")->Cast<UIButton>()->selected);
        EXPECT_FALSE(Element("Setup", "LanguageRu")->Cast<UIButton>()->selected);
        EXPECT_GE(Element("Setup", "LanguageEn")->rect.h, kMinControlHeight);
        EXPECT_GE(Element("Setup", "LanguageRu")->rect.h, kMinControlHeight);

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();
        EXPECT_EQ(Text("Setup", "LanguageEn"), "English") << "a language is written the same in both";
        EXPECT_EQ(Text("Setup", "LanguageRu"), "Русский");
        EXPECT_EQ(Text("Setup", "LanguageCaption"), "Язык");
    }

    TEST_F(MahjongHudTest, AClickOnRussianChangesTheGameAndTheWordsOfSetupAndTheHud) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_EQ(_game->GetLanguage(), "en"_intern);

        ASSERT_TRUE(ClickElement("Setup", "LanguageRu"));
        Draw();

        EXPECT_EQ(_game->GetLanguage(), "ru"_intern);
        EXPECT_EQ(CoreManager::GetUIStrings().GetLanguage(), "ru"_intern);
        EXPECT_EQ(Var("language"), "ru");
        EXPECT_TRUE(Element("Setup", "LanguageRu")->Cast<UIButton>()->selected);
        EXPECT_FALSE(Element("Setup", "LanguageEn")->Cast<UIButton>()->selected);
        EXPECT_EQ(Text("Setup", "Title"), "Новая игра: кто играет?");
        EXPECT_EQ(Text("Setup", "Seat3Computer"), "Компьютер");
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Раунд: Восток, раздача 1 из 16");
        EXPECT_EQ(Var("round"), "Восток");
        EXPECT_EQ(SeatKindVar(1), "computer") << "the seats are untouched";

        ASSERT_TRUE(ClickElement("Setup", "LanguageEn"));
        Draw();
        EXPECT_EQ(_game->GetLanguage(), "en"_intern);
        EXPECT_EQ(Text("Setup", "Title"), "New game: who plays?");
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Round: East, hand 1 of 16");
    }

    TEST_F(MahjongHudTest, TheChoiceIsKeptByStartNewGameAndAControlReset) {
        _game->Start();
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "LanguageRu"));
        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();

        EXPECT_FALSE(SetupShown());
        EXPECT_EQ(CoreManager::GetUIStrings().GetLanguage(), "ru"_intern) << "a started match speaks the language of the game";
        EXPECT_EQ(Text("HudMenu", "NewGame"), "Новая игра");

        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
        Draw();
        EXPECT_TRUE(SetupShown());
        EXPECT_EQ(Var("language"), "ru") << "the row shows the language the game has";
        EXPECT_TRUE(Element("Setup", "LanguageRu")->Cast<UIButton>()->selected);
        EXPECT_EQ(Text("Setup", "Start"), "Начать");

        Json options = Json::object();
        options["seed"] = 5;
        ASSERT_TRUE(_game->AsControllable()->Reset(options).has_value());
        Draw();
        EXPECT_EQ(_game->GetLanguage(), "ru"_intern);
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Раунд: Восток, раздача 1 из 16");
    }

    TEST_F(MahjongHudTest, AnUnknownLanguageChangesNothingAndNeitherDoesAClickWhileThePanelIsClosed) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        EmitAction("MahjongSetLanguage", "de");
        EmitAction("MahjongSetLanguage", "");
        Draw();
        EXPECT_EQ(_game->GetLanguage(), "en"_intern);
        EXPECT_EQ(CoreManager::GetUIStrings().GetLanguage(), "en"_intern);
        EXPECT_EQ(Var("language"), "en") << "the variable keeps the truth";
        EXPECT_TRUE(Element("Setup", "LanguageEn")->Cast<UIButton>()->selected);

        _game->CloseSetup();
        Draw();
        ASSERT_FALSE(SetupShown());
        const uint64_t strings = CoreManager::GetUIStrings().GetVersion();
        EmitAction("MahjongSetLanguage", "ru");
        Draw();
        EXPECT_EQ(_game->GetLanguage(), "en"_intern) << "a stale click is ignored";
        EXPECT_EQ(CoreManager::GetUIStrings().GetVersion(), strings);
        EXPECT_EQ(Text("HudMenu", "NewGame"), "New game");
    }

    TEST_F(MahjongHudTest, TheLanguageOfTheScoreTableAndTheStandingsFollowsTheRowOfSetup) {
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = kRoundsPerMatch;
        _game->SetSettings(settings);
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->GetSession().RunComputers();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        Draw();
        EXPECT_EQ(Text("HandOverAction", "NextHand"), "Next hand");

        _game->OpenSetup();  // New game over the score table
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "LanguageRu"));
        Draw();
        EXPECT_EQ(Text("HandOverAction", "NextHand"), "Следующая раздача");

        _game->CloseSetup();
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        EXPECT_EQ(Text("MatchOver", "NewGame"), "Новая игра");
        EXPECT_EQ(Text("MatchOver", "Headline"), FormatStandings(CoreManager::GetUIStrings(), _game->GetMatch(), _game->GetSession().Seats()).headline.c_str());
        EXPECT_EQ(Var("standing0").find("1. "), 0u);
    }

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

    TEST_F(MahjongHudTest, ContinueIsHiddenInTheFileAndWhenThereIsNothingToContinue) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());
        EXPECT_FALSE(_game->CanContinue());
        ASSERT_NE(Element("Setup", "Continue"), nullptr);
        EXPECT_FALSE(OnScreen("Setup", "Continue"));
        // Start has the whole width of the plate then
        const UIElement* start = Element("Setup", "Start");
        ASSERT_NE(start, nullptr);
        EXPECT_EQ(start->rect.x, Element("Setup", "Plate")->rect.x + kModalPadding);
        EXPECT_EQ(start->rect.w, Element("Setup", "Plate")->rect.w - 2.0f * kModalPadding);

        // The action does nothing, with the panel open or closed
        const uint64_t version = _game->GetSession().Version();
        EmitAction("MahjongContinue");
        EXPECT_TRUE(_game->IsSetupOpen());
        _game->CloseSetup();
        EmitAction("MahjongContinue");
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_EQ(_game->GetSession().Version(), version);
    }

    /// The game starts on a folder that holds a saved match (a game of its own wrote it), as the executable does.
    class MahjongHudContinueTest : public MahjongHudTest {
    protected:
        void SetUp() override {
            _folder = TestTempDirectory("BagiMahjongHudContinueTest");
            std::filesystem::remove_all(_folder);
            SetMatchSaveDirectory(_folder);
            {
                IntrusivePtr<MahjongGame> saver = New<MahjongGame>();
                ASSERT_TRUE(saver->AsControllable()->Reset(ParseJson("{\"seed\": 5, \"seats\": \"PCCC\", \"hands\": 8}").value()).has_value());
                const auto actions = saver->AsControllable()->GetActions();
                ASSERT_TRUE(actions.has_value() && !actions->empty());
                ASSERT_TRUE(saver->AsControllable()->PerformAction((*actions)[0]).has_value());
                _savedState = StateOf(*saver);
                saver->FlushSave();
            }
            ASSERT_TRUE(std::filesystem::exists(MatchFile()));
            MahjongHudTest::SetUp();
        }

        void TearDown() override {
            MahjongHudTest::TearDown();  // the game stops: its last flush still goes to the folder
            SetMatchSaveDirectory(std::filesystem::path());
            std::error_code error;
            std::filesystem::remove_all(_folder, error);
        }

        std::filesystem::path MatchFile() const {
            return _folder / "match.save";
        }

        /// Whether the button is not hidden by the component (a hidden canvas keeps the rects of its last layout, so `OnScreen` says nothing there).
        bool ContinueShown() const {
            const UIElement* proceed = Element("Setup", "Continue");
            return proceed != nullptr && proceed->style.visible;
        }

        std::string ReadMatchFile() const {
            std::ifstream file(MatchFile(), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        static Json StateOf(MahjongGame& game) {
            auto state = game.AsControllable()->GetState();
            EXPECT_TRUE(state.has_value());
            Json json = state ? *state : Json();
            json.erase("version");  // two games reach one table by different numbers of steps
            return json;
        }

        std::filesystem::path _folder;
        Json _savedState;
    };

    TEST_F(MahjongHudContinueTest, ContinueIsShownBesideStartExactlyWhileASavedMatchIsOnOfferInBothLanguages) {
        Draw();
        ASSERT_TRUE(_game->CanContinue());
        ASSERT_TRUE(SetupShown());
        ASSERT_TRUE(OnScreen("Setup", "Continue"));
        EXPECT_EQ(StateOf(*_game), _savedState) << "the saved match is the table behind the panel";

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            Canvas("Setup")->UpdateLayout(kViewWidth, kViewHeight);
            const UIElement* proceed = Element("Setup", "Continue");
            const UIElement* start = Element("Setup", "Start");
            const UIElement* plate = Element("Setup", "Plate");
            ASSERT_TRUE(proceed != nullptr && start != nullptr && plate != nullptr);
            EXPECT_EQ(Text("Setup", "Continue"), Tr(CoreManager::GetUIStrings(), "setup.continue").c_str());
            EXPECT_TRUE(SameColor(proceed->color, PaletteColor("secondary")));
            EXPECT_TRUE(SameColor(start->color, PaletteColor("primary")));
            // Side by side in one row of the main button's height, sharing the width, inside the plate, the gap of 8 between
            EXPECT_EQ(proceed->rect.y, start->rect.y);
            EXPECT_EQ(proceed->rect.h, kMainButtonHeight);
            EXPECT_EQ(start->rect.h, kMainButtonHeight);
            EXPECT_EQ(proceed->rect.w, start->rect.w);
            EXPECT_EQ(proceed->rect.x, plate->rect.x + kModalPadding);
            EXPECT_EQ(proceed->rect.x + proceed->rect.w + kModalGap, start->rect.x);
            EXPECT_EQ(start->rect.x + start->rect.w, plate->rect.x + plate->rect.w - kModalPadding);
            EXPECT_GE(proceed->rect.h, kMinControlHeight);

            // The size matrix with both buttons there: the engine's touch minimum and every word whole
            ExpectPassesTheMatrix("Setup", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Setup", output.width, output.height, kButtonAir);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("Setup"), kButtonAir);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();
        EXPECT_EQ(Text("Setup", "Continue"), "Продолжить");
    }

    TEST_F(MahjongHudContinueTest, AClickOnContinueClosesThePanelOnTheSavedMatchAndNothingSteppedMeanwhile) {
        Draw();
        const uint64_t version = _game->GetSession().Version();
        for (int i = 0; i < 3; ++i) {
            Elapse(kComputerStepSeconds * 2.0f);
        }
        EXPECT_EQ(_game->GetSession().Version(), version) << "no computer steps behind the panel";

        ASSERT_TRUE(ClickElement("Setup", "Continue"));
        Draw();
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(_game->CanContinue());
        EXPECT_FALSE(SetupShown());
        EXPECT_FALSE(ContinueShown());
        EXPECT_EQ(StateOf(*_game), _savedState);
        EXPECT_EQ(_game->GetSession().Version(), version);

        EmitAction("MahjongContinue");  // out of date now
        EXPECT_EQ(_game->GetSession().Version(), version);
    }

    TEST_F(MahjongHudContinueTest, AControlResetTakesTheOfferAway) {
        Draw();
        ASSERT_TRUE(ContinueShown());
        ASSERT_TRUE(_game->AsControllable()->Reset(ParseJson("{\"seed\": 6, \"seats\": \"PCCC\", \"hands\": 8}").value()).has_value());
        Draw();
        EXPECT_FALSE(_game->CanContinue());
        EXPECT_FALSE(ContinueShown());
    }

    TEST_F(MahjongHudContinueTest, StartTakesTheOfferAwayAndOverwritesTheSaveAtTheNextFrameAndNotBefore) {
        Draw();
        ASSERT_TRUE(ContinueShown());
        const std::string saved = ReadMatchFile();
        Elapse(0.01f);
        EXPECT_EQ(ReadMatchFile(), saved) << "nothing changed, so a frame writes nothing";

        ASSERT_TRUE(ClickElement("Setup", "Start"));
        EXPECT_EQ(ReadMatchFile(), saved) << "the start itself writes nothing";
        Elapse(0.01f);
        EXPECT_NE(ReadMatchFile(), saved) << "the frame writes the new match";

        Draw();
        EXPECT_FALSE(_game->CanContinue());
        EXPECT_FALSE(ContinueShown());
        _game->OpenSetup();  // New game
        Draw();
        ASSERT_TRUE(SetupShown());
        EXPECT_FALSE(ContinueShown()) << "the offer is made at the start of the game only";
    }

    // ----- the page Options -----

    TEST_F(MahjongHudTest, OptionsOpensOnlyOverSetupAndBackClosesItAndSetupStaysOpen) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_FALSE(_game->IsSetupOpen());
        EmitAction("MahjongOptions");
        EXPECT_FALSE(_game->IsOptionsOpen()) << "a click that reached the HUD with Setup closed is out of date";

        _game->OpenSetup();
        Draw();
        EmitAction("MahjongOptions");
        EXPECT_TRUE(_game->IsOptionsOpen());
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(_game->IsTablePaused());

        EmitAction("MahjongOptionsBack");
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_TRUE(_game->IsSetupOpen());
        EmitAction("MahjongOptionsBack");  // closed already
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_TRUE(_game->IsSetupOpen());
    }

    TEST_F(MahjongHudTest, TheLevelIsPublishedWhenSetupOpensAndAClickOnThePageChangesTheGameAndTheVariable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->SetLevel(ComputerLevel::Easy);
        _game->OpenSetup();
        Draw();
        EXPECT_EQ(Var("levelChoice"), "easy") << "Setup opens on the level of the game";

        EmitAction("MahjongOptions");
        EmitAction("MahjongSetLevel", "normal");
        EXPECT_EQ(_game->GetLevel(), ComputerLevel::Normal);
        EXPECT_EQ(Var("levelChoice"), "normal");
        EmitAction("MahjongSetLevel", "easy");
        EXPECT_EQ(_game->GetLevel(), ComputerLevel::Easy);
        EXPECT_EQ(Var("levelChoice"), "easy");
    }

    TEST_F(MahjongHudTest, ABadLevelAndALevelClickWhileThePageOrSetupIsClosedChangeNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_EQ(_game->GetLevel(), ComputerLevel::Normal);
        ASSERT_EQ(Var("levelChoice"), "normal");

        EmitAction("MahjongSetLevel", "easy");
        EXPECT_EQ(_game->GetLevel(), ComputerLevel::Normal) << "the page is closed";
        EXPECT_EQ(Var("levelChoice"), "normal");

        EmitAction("MahjongOptions");
        for (const char* bad : {"hard", "Easy", "EASY", " easy", "", "1"}) {
            EmitAction("MahjongSetLevel", bad);
            EXPECT_EQ(_game->GetLevel(), ComputerLevel::Normal) << bad;
            EXPECT_EQ(Var("levelChoice"), "normal") << bad;
        }

        EmitAction("MahjongOptionsBack");
        EmitAction("MahjongSetLevel", "easy");
        EXPECT_EQ(_game->GetLevel(), ComputerLevel::Normal) << "Back closed the page";

        _game->CloseSetup();
        EmitAction("MahjongSetLevel", "easy");
        EXPECT_EQ(_game->GetLevel(), ComputerLevel::Normal) << "Setup is closed";
    }

    TEST_F(MahjongHudTest, TheLevelOfThePageReachesTheNextMatchAndNotTheRunningOne) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_EQ(_game->GetSession().Level(), ComputerLevel::Normal);
        _game->OpenSetup();
        Draw();
        EmitAction("MahjongOptions");
        EmitAction("MahjongSetLevel", "easy");
        EXPECT_EQ(_game->GetSession().Level(), ComputerLevel::Normal) << "a running session keeps its level";

        EmitAction("MahjongOptionsBack");
        EmitAction("MahjongStart");
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_EQ(_game->GetSession().Level(), ComputerLevel::Easy);
    }

    TEST_F(MahjongHudTest, AStartAControlResetAndAControlActionCloseThePage) {
        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        _game->Start();
        Draw();

        EmitAction("MahjongOptions");
        ASSERT_TRUE(_game->IsOptionsOpen());
        EmitAction("MahjongStart");
        EXPECT_FALSE(_game->IsOptionsOpen());

        _game->OpenSetup();
        Draw();
        EmitAction("MahjongOptions");
        ASSERT_TRUE(_game->IsOptionsOpen());
        Json options = Json::object();
        options["seed"] = 3;
        options["seats"] = "PCCC";
        ASSERT_TRUE(control->Reset(options).has_value());
        EXPECT_FALSE(_game->IsOptionsOpen());

        _game->OpenSetup();
        Draw();
        EmitAction("MahjongOptions");
        ASSERT_TRUE(_game->IsOptionsOpen());
        const auto actions = control->GetActions();
        ASSERT_TRUE(actions.has_value() && !actions->empty());
        ASSERT_TRUE(control->PerformAction((*actions)[0]).has_value());
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(_game->IsSetupOpen());
    }

    TEST_F(MahjongHudTest, TheLevelVariableIsNotSetAgainWhileNothingChanges) {
        _game->Start();
        Draw();
        EmitAction("MahjongOptions");
        Draw();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    TEST_F(MahjongHudContinueTest, ContinueWithThePageOpenClosesBothAndKeepsTheSavedMatch) {
        Draw();
        ASSERT_TRUE(_game->CanContinue());
        EmitAction("MahjongOptions");
        ASSERT_TRUE(_game->IsOptionsOpen());
        EXPECT_TRUE(_game->CanContinue()) << "the page does not take the offer away";

        EmitAction("MahjongContinue");
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(_game->IsTablePaused());
        EXPECT_EQ(StateOf(*_game), _savedState);
    }

}  // namespace BECore::Mahjong::Tests
