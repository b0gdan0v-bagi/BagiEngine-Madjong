#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/SeatName.h>

/**
 * The HUD and the seat choice of the shipped scene (`Components/MahjongHudComponent.h`): the variables follow the session and the language,
 * the labels show them, the panel `Setup` opens and closes with the game, and both canvases pass the size matrix with the longest texts
 * of every language.
 */
namespace BECore::Mahjong::Tests {

    namespace {

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
        /// The longest text each variable can have in the current language: found by code points among the real candidates (every wind, every
        /// seat with every wind, the mark of "you" at a table of one person), not typed in.
        static void SetWorstVars() {
            const UIStrings& strings = CoreManager::GetUIStrings();
            std::vector<eastl::string> winds;
            std::vector<eastl::string> turns{Tr(strings, "hud.handOver"), Tr(strings, "hud.matchOver")};
            std::vector<eastl::string> names;
            for (int wind = 0; wind < kSeatCount; ++wind) {
                winds.push_back(WindName(strings, static_cast<Wind>(wind)));
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    SeatSetup onePerson{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
                    onePerson[seat] = SeatKind::Person;
                    turns.push_back(SeatName(strings, seat, static_cast<Wind>(wind)));
                    names.push_back(SeatLabel(strings, seat, static_cast<Wind>(wind), onePerson));
                }
            }
            const eastl::string longestName = LongestText(names);
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("round"_intern, eastl::string_view(LongestText(winds)));
            vars.Set("hand"_intern, 16);
            vars.Set("handsTotal"_intern, 16);
            vars.Set("wall"_intern, 144);
            vars.Set("turn"_intern, eastl::string_view(LongestText(turns)));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                vars.Set(PoolString::Intern(Format("seatName{}", seat)), eastl::string_view(longestName));
                vars.Set(PoolString::Intern(Format("seatScore{}", seat)), "-9999");
                vars.Set(PoolString::Intern(Format("seatMark{}", seat)), ">");
                vars.Set(PoolString::Intern(Format("seatDealer{}", seat)), "*");
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

    TEST_F(MahjongHudTest, TheVariablesAndTheLabelsAreInRussianWhenTheLanguageIsRussian) {
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        // The names of the winds, written out here and not taken from the table
        const char* const winds[] = {"Восток", "Юг", "Запад", "Север"};
        const auto windOf = [&](Seat seat) { return std::string(winds[static_cast<int>(Hand().SeatWind(seat))]); };
        const Seat turn = Hand().CurrentSeat();
        EXPECT_EQ(Var("round"), "Восток");
        EXPECT_EQ(Var("turn"), "Место " + std::to_string(turn + 1) + " (" + windOf(turn) + ")");
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const std::string name = "Место " + std::to_string(seat + 1) + " (" + windOf(seat) + ")";
            EXPECT_EQ(Var(Format("seatName{}", seat).c_str()), seat == 0 ? name + " Вы" : name) << "only the one person is marked, seat " << seat;
            EXPECT_EQ(Var(Format("seatWind{}", seat).c_str()), windOf(seat));
            EXPECT_EQ(Var(Format("seatScore{}", seat).c_str()), std::to_string(_game->GetMatch().Scores()[seat])) << "numbers are the same";
        }
        EXPECT_EQ(Text("Hud", "RoundLabel"), "Раунд: Восток, раздача 1 из 16");
        EXPECT_EQ(Text("Hud", "WallLabel"), "Стена: " + std::to_string(Hand().GetWall().LiveCount()));
        EXPECT_EQ(Text("Hud", "TurnLabel"), "Ход: " + Var("turn"));
        EXPECT_EQ(Text("Hud", "NewGame"), "Новая игра");

        for (const SeatSetup& seats : {kTwoPeople, kAllComputers}) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Draw();
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(Var(Format("seatName{}", seat).c_str()).find("Вы"), std::string::npos) << "no one is 'you' at a table of " << (seats == kTwoPeople ? "two" : "no") << " people";
            }
        }
    }

    TEST_F(MahjongHudTest, TheTurnWordsForTheEndOfAHandAndOfAMatchComeFromTheTable) {
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = kRoundsPerMatch;
        _game->SetSettings(settings);
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->GetSession().RunComputers();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);

        Draw();
        EXPECT_EQ(Var("turn"), "Hand over");
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();
        EXPECT_EQ(Var("turn"), "Раздача окончена");
        EXPECT_EQ(Text("Hud", "TurnLabel"), "Ход: Раздача окончена");

        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        EXPECT_EQ(Var("turn"), "Матч окончен");
        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Draw();
        EXPECT_EQ(Var("turn"), "Match over");
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
        EXPECT_EQ(Var("seatName0").find("Место 1 "), 0u) << Var("seatName0");
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
        for (const char* node : {"Actions", "Hud", "Handoff", "HandOver", "MatchOver", "Setup"}) {
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

            ExpectPassesTheMatrix("Hud", kHudTouchMinimum);
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Hud", output.width, output.height, kButtonAir);
            }
            EXPECT_GE(Element("Hud", "NewGame")->rect.h, kMinControlHeight);
        });
    }

    TEST_F(MahjongHudTest, TheSetupPanelPassesTheSizeMatrixInEveryLanguageAndEveryWordFitsAndEverySegmentIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(SetupShown());

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
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
        });
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

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();  // laid out again with the words of this language
            for (const char* name : {"Info", "Scores", "NewGame"}) {
                const UIElement* element = Element("Hud", name);
                ASSERT_NE(element, nullptr) << name;
                ASSERT_FALSE(element->rect.IsEmpty()) << name;
                for (const TileSlot& slot : layout.slots) {
                    const bool apart = slot.rect.x + slot.rect.w <= element->rect.x || slot.rect.x >= element->rect.x + element->rect.w || slot.rect.y + slot.rect.h <= element->rect.y ||
                                       slot.rect.y >= element->rect.y + element->rect.h;
                    EXPECT_TRUE(apart) << name << " covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                }
            }
        });
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
