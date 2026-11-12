#include "CoachCardTestSupport.h"
#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Control/Json.h>
#include <Mahjong/Components/MahjongHintsComponent.h>
#include <Mahjong/Table/HintCard.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The coach's note on the table (`Components/MahjongHintsComponent.h`, `Table/Coach.h`, `Table/HintCard.h`): the card `CoachCard` of the
 * node `HintsWhy` and its variables (`CardVarNames`) in the shipped scene, over the ledger of the game. The judge and the ledger have their own test
 * (`CoachTest`); here a person's move is made on a fixed hand and the screen is read.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // 123s 456s 789m 5p 5p and three single honours: with a drawn north wind the computer discards it (`HintsTest`); a 5p leaves shanten 2.
        constexpr const char* kPaired = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew";
        constexpr const char* kDrawn = "Nw";
        constexpr float kOutputs[][2] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}};

        bool Inside(const Rect& inner, const Rect& outer) {
            return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        /// A note made by hand with the longest words of the notes: the advised claim, the taken discard of the longest tile name, the biggest numbers.
        CoachNote LongNote(const char* advisedTile, const char* takenTile) {
            CoachNote note;
            note.seat = kSeat0;
            note.advised = HandAction::MakeDiscard(T(advisedTile), false);
            note.taken = HandAction::MakeDiscard(T(takenTile), false);
            note.stepsBefore = 9;
            note.advisedSide = {CoachNumbers::Rated, 9, 99};
            note.takenSide = {CoachNumbers::Rated, 9, 99};
            return note;
        }

    }  // namespace

    class MahjongCoachTest : public MahjongTableTestBase {
    protected:
        /// A match in the mode `mode` with East (seat 0, the person) holding `kPaired` and drawing `drawn`.
        void StartEast(HintsMode mode, const SeatSetup& seats = kDefaultSeatSetup, const char* drawn = kDrawn, std::optional<int> liveAfterDeal = std::nullopt) {
            _game->SetHintsMode(mode);
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            FixedWallSpec spec = SpecOf(kPaired, drawn);
            spec.liveAfterDeal = liveAfterDeal;
            Lay(MakeModel(spec));
            Draw();
        }

        bool Discard(const char* tile) {
            return _game->GetSession().PerformPerson(HandAction::MakeDiscard(T(tile), false)).has_value();
        }

        /// The card is up and says the note of the ledger.
        bool NoteShown() const {
            return OnScreen("HintsWhy", "CoachCard") && _game->GetCoach().Note().has_value() && CardVars() == ExpectedLines();
        }

        std::string NoteVar(size_t index) const {
            return CardVars()[index];
        }

        std::vector<std::string> NoteVars() const {
            return CardVars();
        }

        bool NoNoteText() const {
            return NoCardText();
        }

        /// The tally of the game as the card says it.
        eastl::string Tally() const {
            const TableSession& session = _game->GetSession();
            return CardTallyText(CoreManager::GetUIStrings(), _game->GetCoach().HandTallyOf(HandKeyOf(session)), _game->GetCoach().MatchTallyOf(session.StartCount()));
        }

        /// What the pure helper says about the note the ledger holds now, in the current language, with the tally of the game.
        std::vector<std::string> ExpectedLines() const {
            if (const std::optional<CoachNote>& note = _game->GetCoach().Note()) {
                return CardTexts(MakeNoteCard(CoreManager::GetUIStrings(), *note, Tally()));
            }
            return CardTexts(HintCard{});
        }

        /// The title of the card of the advice of seat 0's table now.
        std::string AdviceTitle() const {
            const TableView view = MakeTableView(Hand(), kSeat0);
            const eastl::string text = AdviceText(CoreManager::GetUIStrings(), view, *AdviceFor(view));
            return Std(text);
        }

        /// A discard of the person at seat 0 that `JudgeMove` says differs, or nothing.
        std::optional<HandAction> DifferingDiscard() const {
            const TableView view = MakeTableView(Hand(), kSeat0);
            for (const HandAction& action : view.legal) {
                if (action.kind == ActionKind::Discard && JudgeMove(view, action).has_value()) {
                    return action;
                }
            }
            return std::nullopt;
        }

        /// The click on the felt in the middle of the table: no canvas takes it and no tile is under it.
        void ClickTheFelt() {
            Click(kViewWidth * 0.5f, kViewHeight * 0.5f);
            Draw();
        }

        /// The first person's differing discard of `kPaired`, drawn: the note is up.
        void PutNoteUp() {
            ASSERT_NO_FATAL_FAILURE(StartEast(HintsMode::Coach));
            ASSERT_TRUE(Discard("5p"));
            Draw();
            ASSERT_TRUE(NoteShown());
        }

        /// The next turn of the person at seat 0, drawn.
        void ToThePersonsNextTurn() {
            ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
            Draw();
        }
    };

    // ----- when the note is up -----

    TEST_F(MahjongCoachTest, ADifferingDiscardPutsTheNoteUpOnScreenAndInTheVariables) {
        ASSERT_NO_FATAL_FAILURE(StartEast(HintsMode::Coach));
        EXPECT_FALSE(NoteShown());
        EXPECT_TRUE(NoNoteText());
        ASSERT_TRUE(Discard("5p"));
        Draw();

        ASSERT_TRUE(_game->GetCoach().Note().has_value());
        EXPECT_TRUE(NoteShown());
        EXPECT_EQ(NoteVars(), ExpectedLines());
        EXPECT_FALSE(NoteVar(0).empty());
        EXPECT_FALSE(NoteVar(2).empty());
    }

    TEST_F(MahjongCoachTest, ALanguageSwitchWordsTheNoteAgain) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        std::vector<std::vector<std::string>> seen;
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            EXPECT_TRUE(NoteShown());
            EXPECT_EQ(NoteVars(), ExpectedLines());
            seen.push_back(NoteVars());
        });
        ASSERT_GE(seen.size(), 2u);
        EXPECT_NE(seen[0], seen[1]);
    }

    TEST_F(MahjongCoachTest, AnAdvisedDiscardOrOneOfEqualNumbersPutsNothingUp) {
        for (const char* tile : {"Nw", "Rd", "Ew"}) {
            SCOPED_TRACE(tile);
            ASSERT_NO_FATAL_FAILURE(StartEast(HintsMode::Coach));
            ASSERT_TRUE(Discard(tile));
            Draw();
            EXPECT_FALSE(_game->GetCoach().Note().has_value());
            EXPECT_FALSE(NoteShown());
            EXPECT_TRUE(NoNoteText());
            EXPECT_EQ(_game->GetCoach().MatchTally().moves, 1);
            EXPECT_EQ(_game->GetCoach().MatchTally().agreed, 1);
        }
    }

    TEST_F(MahjongCoachTest, AComputersMoveAndTheModesOffAndHintsPutNothingUp) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().GetPhase() != SessionPhase::Playing; }));
        Draw();
        EXPECT_FALSE(_game->GetCoach().Note().has_value()) << "no person moved";
        EXPECT_FALSE(NoteShown());

        for (const HintsMode mode : {HintsMode::Off, HintsMode::Hints}) {
            SCOPED_TRACE(HintsModeText(mode).data());
            ASSERT_NO_FATAL_FAILURE(StartEast(mode));
            ASSERT_TRUE(Discard("5p"));
            Draw();
            EXPECT_FALSE(_game->GetCoach().Note().has_value());
            EXPECT_FALSE(NoteShown());
            EXPECT_TRUE(NoNoteText());
        }
    }

    TEST_F(MahjongCoachTest, ATableOfTwoPeopleHasNoNoteNotEvenAfterTheCover) {
        ASSERT_NO_FATAL_FAILURE(StartEast(HintsMode::Coach, kTwoPeople));
        ASSERT_TRUE(Discard("5p"));
        Draw();
        EXPECT_FALSE(_game->GetCoach().Note().has_value());
        EXPECT_FALSE(NoteShown());
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
        Draw();
        _game->GetSession().Reveal();
        Draw();
        EXPECT_FALSE(NoteShown());
        EXPECT_TRUE(NoNoteText());
    }

    TEST_F(MahjongCoachTest, AModeSwitchToHintsOrOffTakesTheNoteDown) {
        for (const HintsMode mode : {HintsMode::Hints, HintsMode::Off}) {
            SCOPED_TRACE(HintsModeText(mode).data());
            ASSERT_NO_FATAL_FAILURE(PutNoteUp());
            _game->SetHintsMode(mode);
            Draw();
            EXPECT_FALSE(NoteShown());
            EXPECT_TRUE(NoNoteText());
            EXPECT_FALSE(_game->GetCoach().Note().has_value());
        }
    }

    // ----- how long it stays -----

    TEST_F(MahjongCoachTest, TheNoteOutlivesTheComputersTurnsAndASteadyFrameChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        const std::vector<std::string> words = NoteVars();
        ASSERT_NO_FATAL_FAILURE(ToThePersonsNextTurn());
        EXPECT_TRUE(NoteShown()) << "the computers moved, the person has not";
        EXPECT_EQ(NoteVars(), words);

        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version) << "an unchanged frame sets nothing";
    }

    TEST_F(MahjongCoachTest, AClickTakesTheNoteDownAndTheVariablesAreEmptied) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        ClickTheFelt();
        EXPECT_FALSE(NoteShown());
        EXPECT_TRUE(NoNoteText()) << "nothing of the note stays in a variable";
        EXPECT_FALSE(_game->GetCoach().Note().has_value());
    }

    TEST_F(MahjongCoachTest, ATouchTakesTheNoteDownToo) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        Tap(kViewWidth * 0.5f, kViewHeight * 0.5f);
        Draw();
        EXPECT_FALSE(NoteShown());
    }

    TEST_F(MahjongCoachTest, TheNextAcceptedActionReplacesTheNoteOrTakesItDown) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        ASSERT_NO_FATAL_FAILURE(ToThePersonsNextTurn());
        const std::vector<std::string> first = NoteVars();

        const TableView view = MakeTableView(Hand(), kSeat0);
        const std::optional<HandAction> advice = AdviceFor(view);
        ASSERT_TRUE(advice.has_value());
        ASSERT_TRUE(_game->GetSession().PerformPerson(*advice).has_value());
        Draw();
        EXPECT_FALSE(NoteShown()) << "a move that agrees leaves no note";
        EXPECT_TRUE(NoNoteText());

        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        ASSERT_NO_FATAL_FAILURE(ToThePersonsNextTurn());
        const std::optional<HandAction> differing = DifferingDiscard();
        ASSERT_TRUE(differing.has_value());
        ASSERT_TRUE(_game->GetSession().PerformPerson(*differing).has_value());
        Draw();
        EXPECT_TRUE(NoteShown());
        EXPECT_EQ(NoteVars(), ExpectedLines()) << "the words of the new note";
    }

    TEST_F(MahjongCoachTest, OnePressOfHintTakesTheNoteDownAndPutsTheAdviceOnTheCard) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        ASSERT_NO_FATAL_FAILURE(ToThePersonsNextTurn());
        ASSERT_TRUE(NoteShown());
        ASSERT_TRUE(ClickElement("HintsButton", "HintButton"));
        Draw();
        EXPECT_FALSE(NoteShown());
        EXPECT_FALSE(_game->GetCoach().Note().has_value());
        EXPECT_FALSE(OnScreen("Hints", "AdviceText")) << "in Coach the sentence is the title of the card";
        EXPECT_TRUE(OnScreen("HintsWhy", "CoachCard")) << "the same press puts the advice on the card";
        EXPECT_EQ(NoteVar(0), AdviceTitle());
    }

    TEST_F(MahjongCoachTest, WithTheSwitchOnTheNoteOfTheLastMoveGoesFirstAndTheAdviceComesOnceItIsTakenDown) {
        ASSERT_NO_FATAL_FAILURE(StartEast(HintsMode::Coach));
        EmitAction("MahjongHintAdvice");
        Draw();
        ASSERT_TRUE(OnScreen("HintsWhy", "CoachCard"));
        ASSERT_EQ(NoteVar(0), AdviceTitle());
        ASSERT_TRUE(Discard("5p"));
        Draw();
        ASSERT_TRUE(NoteShown());
        ASSERT_NO_FATAL_FAILURE(ToThePersonsNextTurn());
        EXPECT_TRUE(NoteShown()) << "the card says the note of the last move, not the advice of the new turn";
        EXPECT_FALSE(OnScreen("Hints", "AdviceText"));

        ClickTheFelt();
        EXPECT_FALSE(NoteShown());
        EXPECT_TRUE(OnScreen("HintsWhy", "CoachCard"));
        EXPECT_EQ(NoteVar(0), AdviceTitle()) << "the advice of the new turn comes once the note is down";
        EXPECT_FALSE(OnScreen("Hints", "AdviceText"));
    }

    TEST_F(MahjongCoachTest, TheEndOfTheHandAndANewMatchTakeTheNoteDown) {
        // A short live wall: the discard ends the hand as drawn
        ASSERT_NO_FATAL_FAILURE(StartEast(HintsMode::Coach, kDefaultSeatSetup, kDrawn, 1));
        ASSERT_TRUE(Discard("5p"));
        Draw();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        EXPECT_FALSE(NoteShown());
        EXPECT_TRUE(NoNoteText());
        EXPECT_FALSE(_game->GetCoach().Note().has_value());

        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        ASSERT_TRUE(_game->StartMatch(11).has_value());
        Draw();
        EXPECT_FALSE(NoteShown());
        EXPECT_TRUE(NoNoteText());
    }

    TEST_F(MahjongCoachTest, ASetupOrRulesScreenOverTheTableTakesTheNoteDown) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        _game->OpenSetup();
        Draw();
        EXPECT_FALSE(NoteShown());
        _game->CloseSetup();
        Draw();
        EXPECT_FALSE(NoteShown()) << "a note that was taken down does not come back";

        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        _game->OpenRules();
        Draw();
        EXPECT_FALSE(NoteShown());
        EXPECT_TRUE(NoNoteText());
    }

    // ----- what the note is -----

    TEST_F(MahjongCoachTest, TheControlStateIsTheSameWithTheNoteUpAndInTheOtherModes) {
        const auto play = [&](HintsMode mode) {
            std::string state;
            [&] {
                ASSERT_NO_FATAL_FAILURE(StartEast(mode));
                const uint64_t version = _game->GetSession().Version();
                ASSERT_TRUE(Discard("5p"));
                Draw();
                EXPECT_EQ(mode == HintsMode::Coach, NoteShown());
                EXPECT_EQ(_game->GetSession().Version(), version + 1) << "the note moves nothing of the session";
                const auto got = _game->AsControllable()->GetState();
                ASSERT_TRUE(got.has_value());
                Json shown = *got;
                shown.erase("version");
                state = shown.dump();
            }();
            return state;
        };
        const std::string coach = play(HintsMode::Coach);
        EXPECT_EQ(coach, play(HintsMode::Hints));
        EXPECT_EQ(coach, play(HintsMode::Off));
    }

    TEST_F(MahjongCoachTest, TheNoteIsTheSameForTwoTablesThatDifferOnlyInWhatTheViewerCannotSee) {
        const auto run = [&](const char* seat1, const char* seat3) {
            std::vector<std::string> lines;
            [&] {
                _game->SetHintsMode(HintsMode::Coach);
                ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
                FixedWallSpec spec = SpecOf(kPaired, kDrawn);
                spec.hands[kSeat1] = seat1;
                spec.hands[kSeat3] = seat3;
                Lay(MakeModel(spec));
                Draw();
                ASSERT_TRUE(Discard("5p"));
                Draw();
                ASSERT_TRUE(NoteShown());
                lines = NoteVars();
            }();
            return lines;
        };
        const std::vector<std::string> plain = run(kQuiet1, kQuiet3);
        const std::vector<std::string> loaded = run("7p 7p 7p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", "4p 4p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd Rd");
        EXPECT_FALSE(plain.front().empty());
        EXPECT_EQ(plain, loaded);
    }

    // ----- the whole path -----

    /// A match of four hands on a fixed seed, the person at seat 0 playing through the table: a pass or the win by the buttons of the
    /// action bar, a discard of the drawn tile by a click on it, `Next hand` by its button. The tally is kept here from `JudgeMove` on the
    /// view before each move, so it is no copy of the ledger.
    TEST_F(MahjongCoachTest, AShortMatchPlayedThroughTheTableCountsEveryMoveAndSaysTheTallyOnBothScreens) {
        _game->SetHintsMode(HintsMode::Coach);
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = kRoundsPerMatch;
        _game->SetSettings(settings);
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        TableSession& session = _game->GetSession();

        CoachTally hand;
        CoachTally match;
        int handsSeen = 0;
        for (int guard = 0; guard < kActionLimit * kRoundsPerMatch && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++guard) {
            session.RunComputers();
            Draw();
            if (session.GetPhase() == SessionPhase::HandOver) {
                // The score table says the tally of the hand and of the match as counted here (it may wait for a lost claim to be seen)
                for (int wait = 0; wait < 4 && !OnScreen("HandOverAction", "NextHand"); ++wait) {
                    AdvanceTime(kClaimLostSeconds);
                    Draw();
                }
                ASSERT_TRUE(OnScreen("HandOver", "HandCoachRow") == (match.moves > 0)) << "hand " << handsSeen;
                const eastl::string expected = FormatCoachTally(CoreManager::GetUIStrings(), hand, match);
                EXPECT_STREQ(Var("handOverCoach").c_str(), expected.c_str()) << "hand " << handsSeen;
                EXPECT_EQ(_game->GetCoach().HandTally(), hand) << "hand " << handsSeen;
                EXPECT_EQ(_game->GetCoach().MatchTally(), match) << "hand " << handsSeen;
                ASSERT_TRUE(ClickElement("HandOverAction", "NextHand")) << "hand " << handsSeen;
                ++handsSeen;
                hand = {};
                continue;
            }
            ASSERT_TRUE(session.ActorIsPerson());
            const Seat actor = session.Actor();
            const TableView view = MakeTableView(Hand(), actor);
            std::optional<HandAction> chosen;
            const uint64_t version = session.Version();
            if (std::any_of(view.legal.begin(), view.legal.end(), [](const HandAction& action) { return action.kind == ActionKind::Pass; })) {
                chosen = HandAction::MakePass();
                EmitAction("MahjongPass");
            } else if (std::any_of(view.legal.begin(), view.legal.end(), [](const HandAction& action) { return action.kind == ActionKind::DeclareWin; })) {
                chosen = HandAction::MakeDeclareWin();
                EmitAction("MahjongWin");
            } else {
                const eastl::vector<Tile>& concealed = Hand().GetHand(actor).concealed;
                const int index = static_cast<int>(concealed.size()) - 1;
                chosen = HandAction::MakeDiscard(concealed[static_cast<size_t>(index)], false);
                ClickHandTile(index);  // no canvas takes it: the table does
            }
            ASSERT_GT(session.Version(), version) << "the move was accepted";
            const bool agrees = !JudgeMove(view, *chosen).has_value();
            ++hand.moves;
            ++match.moves;
            hand.agreed += agrees ? 1 : 0;
            match.agreed += agrees ? 1 : 0;
        }
        ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        ASSERT_EQ(handsSeen, kRoundsPerMatch);
        ASSERT_GT(match.moves, 0);
        Draw();

        ASSERT_TRUE(OnScreen("MatchOver", "MatchCoachRow"));
        const eastl::string expected = FormatCoachTally(CoreManager::GetUIStrings(), CoachTally{}, match);
        EXPECT_STREQ(Var("matchOverCoach").c_str(), expected.c_str());
        EXPECT_EQ(_game->GetCoach().MatchTally(), match);
    }

    // ----- the place and the words -----

    TEST_F(MahjongCoachTest, TheCardOfTheNoteLiesOnTheWallRingMarksTheTwoMovesAndTakesNoInput) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        for (const TableShape shape : {TableShape::Landscape, TableShape::Portrait, TableShape::Narrow, TableShape::Wide}) {
            const MatrixWindow window = DesignWindowOf(shape);
            SCOPED_TRACE(window.label.c_str());
            LayOutOnTheTable({"HintsWhy"}, window);

            const UIElement* panel = Element("HintsWhy", "CoachCard");
            ASSERT_NE(panel, nullptr);
            const TableLayout layout = MakeTableLayout(TableInput{}, DesignWidthOf(shape), DesignHeightOf(shape));
            EXPECT_TRUE(Inside(panel->rect, layout.zones.wall)) << "on the ring";
            for (const char* name : {"CardTitle", "CardHead", "CardRow0", "CardRow1", "CardFoot0"}) {
                const UIElement* part = Element("HintsWhy", name);
                ASSERT_NE(part, nullptr) << name;
                ASSERT_GT(part->rect.w, 0.0f) << name;
                EXPECT_TRUE(Inside(part->rect, panel->rect)) << name;
                EXPECT_FALSE(part->IsHitTestable()) << name << " takes no input";
            }
            EXPECT_EQ(Element("HintsWhy", "CardRow2")->rect.w, 0.0f) << "a note has two rows";
            EXPECT_FALSE(panel->IsHitTestable());
            EXPECT_FALSE(Click(panel->rect.x + panel->rect.w * 0.5f, panel->rect.y + panel->rect.h * 0.5f)) << "a click on the card is no canvas's";
            EXPECT_TRUE(SameColor(panel->color, PaletteColor("surface")));
            EXPECT_TRUE(SameColor(Element("HintsWhy", "CardRow0")->color, PaletteColor("surfaceRaised"))) << "the computer's choice";
            EXPECT_TRUE(SameColor(Element("HintsWhy", "CardRow1")->color, PaletteColor("danger"))) << "the person's move";

            for (const TileSlot& slot : layout.slots) {
                if (slot.role != SlotRole::WallStack) {
                    EXPECT_TRUE(Apart(slot.rect, panel->rect)) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat;
                }
            }
        }
    }

    TEST_F(MahjongCoachTest, TheCardOfANotePassesTheSizeMatrixAndTheLongestTextOfEveryCellFitsInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(PutNoteUp());
        std::vector<CoachNote> notes;
        notes.push_back(LongNote("Nw", "9p"));
        notes.push_back(LongNote("Rd", "9m"));
        CoachNote chow = LongNote("Rd", "9m");
        chow.advised = HandAction::MakeClaim(ClaimKind::Chow, T("7m"));
        chow.taken = HandAction::MakePass();
        chow.pending = T("9m");
        chow.takenSide = {};
        notes.push_back(chow);
        CoachNote pung = chow;
        pung.advised = HandAction::MakePass();
        pung.taken = HandAction::MakeClaim(ClaimKind::Pung);
        pung.takenSide = {CoachNumbers::Rated, 9, 99};
        notes.push_back(pung);

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();  // the words of the language
            const UIStrings& strings = CoreManager::GetUIStrings();
            const eastl::string tally = CardTallyText(strings, CoachTally{99, 99}, CoachTally{999, 999});
            std::vector<HintCard> cards;
            for (const CoachNote& note : notes) {
                cards.push_back(MakeNoteCard(strings, note, tally));
            }
            ASSERT_TRUE(OnScreen("HintsWhy", "CoachCard"));
            SetLongestCardTexts(cards);
            Draw();
            ExpectPassesTheMatrix("HintsWhy", EngineTouchMinimum());
            for (const auto& output : kOutputs) {
                ExpectWordsFit("HintsWhy", output[0], output[1], 0.0f);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("HintsWhy"), 0.0f);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

}  // namespace BECore::Mahjong::Tests
