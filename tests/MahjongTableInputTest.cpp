#include "ComputerTestSupport.h"
#include "MahjongButtonSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UIToggleComponent.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * Playing on the table with the mouse (`Components/MahjongTableComponent.h`): a click on a tile discards it, `Declare` and the dimmed
 * tiles, the pace of the computers, the cover of a hot seat and an action that comes through the control API. The fixed hands come from
 * `FixedWall.h` and are laid into the hand in progress; clicks go through `UIInput` as the platform layer sends them.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seat 0 holds a hand that is far from waiting; as East it draws 9p.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// Seat 0 waits on Rd (a single); drawing 9p it can discard 9p or Rd and still wait, no other tile.
        constexpr const char* kWaitingHand = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        constexpr int kTilesThatCannotDeclare = kDealtTiles + 1 - 2;
        /// Steps that fit in no pause: a fraction of the pause per event.
        constexpr float kPauseFraction = 0.4f;
        constexpr float kLongFrame = 100.0f;

        void Elapse(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

    }  // namespace

    class MahjongTableInputTest : public MahjongTableTestBase {
    protected:
        /// A match with `seats`, then a fixed hand laid: seat 0 holds `seat0Hand` (the quiet hands for the others), `eastSeat` deals.
        void StartWith(const SeatSetup& seats, const char* seat0Hand = kPlainHand, Seat eastSeat = kSeat0) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(MakeModel(SpecOf(seat0Hand, "9p"), eastSeat));
            Draw();
        }

        /// The rectangles drawn in the colour of the veil over a tile that cannot declare.
        int VeilCalls() const {
            int veils = 0;
            for (const DrawCall& call : _renderer->calls) {
                const bool veil = call.kind == DrawKind::Rect && SameColor(call.color, kVeil);
                veils += veil ? 1 : 0;
            }
            return veils;
        }
    };

    // ----- the mouse -----

    TEST_F(MahjongTableInputTest, AClickOnATileOfTheHandDiscardsThatTile) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat0);
        const int index = IndexOf(kSeat0, T("9p"));
        ASSERT_GE(index, 0);
        const uint64_t version = _game->GetSession().Version();

        EXPECT_FALSE(ClickHandTile(index)) << "no canvas takes the click: the table does";

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p"));
        EXPECT_EQ(Hand().Discards().front().seat, kSeat0);
        EXPECT_EQ(Hand().GetHand(kSeat0).concealed.size(), static_cast<size_t>(kDealtTiles));
        EXPECT_GT(_game->GetSession().Version(), version);
    }

    TEST_F(MahjongTableInputTest, EveryTileOfTheHandIsClickableAtItsOwnPlace) {
        for (const char* tile : {"1p", "7p", "4s", "Rd"}) {
            ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
            const int index = IndexOf(kSeat0, T(tile));
            ASSERT_GE(index, 0) << tile;

            ClickHandTile(index);

            ASSERT_FALSE(Hand().Discards().empty()) << tile;
            EXPECT_EQ(Hand().Discards().front().tile, T(tile));
        }
    }

    TEST_F(MahjongTableInputTest, AClickOnTheFeltOrAnOpponentsTileOrTheWrongButtonDoesNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const std::string before = Snapshot(Hand());
        const uint64_t version = _game->GetSession().Version();

        Click(5.0f, 5.0f);  // empty felt
        for (const TileSlot& slot : _table->GetLayout().slots) {
            if (slot.role == SlotRole::HandTile && slot.seat != kSeat0) {
                Click(slot.rect.x + slot.rect.w * 0.5f, slot.rect.y + slot.rect.h * 0.5f);
            }
        }
        const TileSlot* slot = HandSlot(0);
        ASSERT_NE(slot, nullptr);
        SDL_MouseButtonEvent right = MouseButton(slot->rect.x + 1.0f, slot->rect.y + 1.0f, true);
        right.button = SDL_BUTTON_RIGHT;
        CoreManager::GetUIInput().DispatchMouseButtonDown(right);

        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_EQ(_game->GetSession().Version(), version);
    }

    TEST_F(MahjongTableInputTest, NoClickIsTakenWhileAComputerHasToAct) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kPlainHand, kSeat1));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);
        const std::string before = Snapshot(Hand());

        ClickHandTile(0);

        EXPECT_EQ(Snapshot(Hand()), before) << "the viewer is not the actor";
    }

    TEST_F(MahjongTableInputTest, AClickAfterTheHandIsOverDoesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->GetSession().RunComputers();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        Draw();
        const std::string before = Snapshot(Hand());

        ClickHandTile(0);

        EXPECT_EQ(Snapshot(Hand()), before);
    }

    // ----- declaring the calling hand -----

    TEST_F(MahjongTableInputTest, DeclareThenATileThatLeavesAWaitingHandDeclaresWithTheDiscard) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        ASSERT_TRUE(_table->GetButtons()[ButtonKind::Declare].shown);
        EXPECT_FALSE(_table->DeclareNext());
        EXPECT_EQ(Var("declareNext"), "off");

        EXPECT_TRUE(ClickElement("Actions", "Declare")) << "the toggle takes the click and asks for `on`";
        Draw();
        EXPECT_TRUE(_table->DeclareNext());
        EXPECT_EQ(Var("declareNext"), "on");
        const auto* declare = Element("Actions", "Declare")->GetComponent<UIToggleComponent>();
        ASSERT_NE(declare, nullptr);
        EXPECT_TRUE(declare->IsOn()) << "the switch shows that the flag is on";

        ClickHandTile(IndexOf(kSeat0, T("9p")));

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p"));
        EXPECT_TRUE(Hand().GetHand(kSeat0).callingDeclared);
        EXPECT_FALSE(_table->DeclareNext()) << "the discard clears the flag";
    }

    TEST_F(MahjongTableInputTest, ATileWhoseDeclaringDiscardIsNotLegalIsDimmedAndDoesNothingWhileDeclaring) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        EXPECT_EQ(VeilCalls(), 0);
        EmitAction("MahjongDeclare", "on");
        Draw();
        EXPECT_EQ(VeilCalls(), kTilesThatCannotDeclare) << "a veil over each tile that cannot declare, none over 9p and Rd";

        const std::string before = Snapshot(Hand());
        ClickHandTile(IndexOf(kSeat0, T("Ew")));

        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_TRUE(_table->DeclareNext()) << "the flag stays until a discard goes through";

        ClickHandTile(IndexOf(kSeat0, T("Rd")));
        EXPECT_EQ(Hand().Discards().front().tile, T("Rd"));
        EXPECT_TRUE(Hand().GetHand(kSeat0).callingDeclared);
    }

    TEST_F(MahjongTableInputTest, WithoutTheFlagAnyTileIsDiscardedPlainAndNothingDeclares) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));

        ClickHandTile(IndexOf(kSeat0, T("Ew")));

        EXPECT_EQ(Hand().Discards().front().tile, T("Ew"));
        EXPECT_FALSE(Hand().GetHand(kSeat0).callingDeclared);
    }

    TEST_F(MahjongTableInputTest, TheToggleAsksForOnAndThenForOffAndWithoutADeclaringDiscardItIsNotOffered) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        EmitAction("MahjongDeclare", "on");
        EXPECT_TRUE(_table->DeclareNext());
        EmitAction("MahjongDeclare", "on");
        EXPECT_TRUE(_table->DeclareNext()) << "`on` asks for the state, it does not flip it";
        EmitAction("MahjongDeclare", "off");
        EXPECT_FALSE(_table->DeclareNext());
        Draw();
        ASSERT_TRUE(ClickElement("Actions", "Declare"));
        EXPECT_TRUE(_table->DeclareNext());
        Draw();
        ASSERT_TRUE(ClickElement("Actions", "Declare"));
        EXPECT_FALSE(_table->DeclareNext()) << "the toggle that is on asks for `off`";

        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kPlainHand));
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Declare].shown);
        EXPECT_FALSE(OnScreen("Actions", "Declare"));
        EmitAction("MahjongDeclare", "on");
        EXPECT_FALSE(_table->DeclareNext());
    }

    TEST_F(MahjongTableInputTest, TheFlagIsClearedWhenTheTurnPassesToAnotherSeat) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        EmitAction("MahjongDeclare", "on");
        ASSERT_TRUE(_table->DeclareNext());

        // The model moves on under the table (a discard that bypasses the session): the next frame has no Declare button
        ASSERT_TRUE(_game->GetMatch().Hand().Discard(kSeat0, T("Ew")).has_value());
        Draw();

        EXPECT_FALSE(_table->DeclareNext());
        EXPECT_EQ(Var("declareNext"), "off");
    }

    TEST_F(MahjongTableInputTest, TheFlagDoesNotSurviveIntoTheNextTurnOfTheSamePerson) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        EmitAction("MahjongDeclare", "on");
        ASSERT_TRUE(_table->DeclareNext());

        // A control client plays 9p for the person (no declaration); the computers play on until the person is asked again
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.PerformPerson(HandAction::MakeDiscard(T("9p"), false)).has_value());
        session.RunComputers();
        ASSERT_EQ(session.Actor(), kSeat0);
        ASSERT_EQ(Hand().GetPhase(), Phase::Discard) << "this seed gives the person a plain turn again";
        Draw();

        EXPECT_TRUE(_table->GetButtons()[ButtonKind::Declare].shown) << "the hand still waits, so Declare is offered again";
        EXPECT_FALSE(_table->DeclareNext()) << "but the flag was set in the turn before";
        EXPECT_EQ(Var("declareNext"), "off");
    }

    // ----- the tap: a first tap selects, a second one discards (the touch regime, `Table/TapSelect.h`) -----

    TEST_F(MahjongTableInputTest, AMouseDiscardsWithOneClickInABigWindowAndSelectsNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const int index = IndexOf(kSeat0, T("9p"));

        ClickHandTile(index);

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p"));
        Draw();
        EXPECT_FALSE(_table->SelectedHandIndex().has_value());
        EXPECT_EQ(Var("selectedTileText"), "");
    }

    TEST_F(MahjongTableInputTest, ATapSelectsTheTileAndASecondTapOnItDiscardsIt) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const int index = IndexOf(kSeat0, T("9p"));
        const uint64_t version = _game->GetSession().Version();
        const std::string before = Snapshot(Hand());

        TapHandTile(index);
        Draw();

        EXPECT_EQ(Snapshot(Hand()), before) << "the first tap discards nothing";
        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_table->SelectedHandIndex(), index);
        EXPECT_EQ(Var("selectedTileText"), "Selected: 9 Dots");

        TapHandTile(index);
        Draw();

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p"));
        EXPECT_GT(_game->GetSession().Version(), version);
        EXPECT_FALSE(_table->SelectedHandIndex().has_value());
        EXPECT_EQ(Var("selectedTileText"), "");
    }

    TEST_F(MahjongTableInputTest, TheNameLabelIsShownExactlyWhileATileIsSelectedAndSaysItInBothLanguages) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const int index = IndexOf(kSeat0, T("9p"));
        EXPECT_FALSE(OnScreen("Hud", "SelectedTile"));

        TapHandTile(index);
        Draw();
        ASSERT_TRUE(OnScreen("Hud", "SelectedTile"));
        EXPECT_EQ(Var("selectedTileText"), "Selected: 9 Dots");

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();
        EXPECT_TRUE(OnScreen("Hud", "SelectedTile"));
        EXPECT_EQ(Var("selectedTileText").find("Выбрано: "), 0u);
        ASSERT_TRUE(_game->SetLanguage("en"_intern));

        Tap(5.0f, 5.0f);
        Draw();
        EXPECT_FALSE(OnScreen("Hud", "SelectedTile")) << "gone with the selection";
    }

    TEST_F(MahjongTableInputTest, ATapOnAnotherTileMovesTheSelectionAndOnlyTheSelectedOneDiscards) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const int first = IndexOf(kSeat0, T("1p"));
        const int second = IndexOf(kSeat0, T("Rd"));
        ASSERT_NE(first, second);

        TapHandTile(first);
        TapHandTile(second);
        Draw();

        EXPECT_TRUE(Hand().Discards().empty());
        EXPECT_EQ(_table->SelectedHandIndex(), second);
        EXPECT_EQ(Var("selectedTileText"), "Selected: Red Dragon");

        TapHandTile(second);

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("Rd"));
    }

    TEST_F(MahjongTableInputTest, ATapInTheStripBesideTheTilesSelectsTheNearestTile) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const TileSlot* slot = HandSlot(3);
        ASSERT_NE(slot, nullptr);

        Tap(slot->rect.x + slot->rect.w * 0.5f, slot->rect.y + slot->rect.h + 2.0f);  // the margin under the tiles
        Draw();

        EXPECT_EQ(_table->SelectedHandIndex(), 3);
        EXPECT_TRUE(Hand().Discards().empty());
    }

    TEST_F(MahjongTableInputTest, ATapElsewhereOnTheTableClearsTheSelectionAndTheNextTapSelectsAgain) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const int index = IndexOf(kSeat0, T("9p"));
        TapHandTile(index);
        Draw();
        ASSERT_EQ(_table->SelectedHandIndex(), index);

        Tap(5.0f, 5.0f);  // empty felt
        Draw();

        EXPECT_FALSE(_table->SelectedHandIndex().has_value());
        EXPECT_EQ(Var("selectedTileText"), "");
        TapHandTile(index);
        Draw();
        EXPECT_EQ(_table->SelectedHandIndex(), index);
        EXPECT_TRUE(Hand().Discards().empty()) << "the tap after the clear selects, it does not discard";
    }

    TEST_F(MahjongTableInputTest, TheTouchRegimeStaysOnForAMouseClickAfterATouch) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const int index = IndexOf(kSeat0, T("9p"));
        TapHandTile(index);

        ClickHandTile(index);  // a mouse on a touch device (a pen, a trackpad): the regime stays

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p")) << "the second click is on the selected tile";
    }

    TEST_F(MahjongTableInputTest, ASmallWindowWithAMouseTakesTheTapRule) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        // The phone profile gives this window its widest hand tile (`kPhoneHandTileMaxW`), still under the touch minimum: about 29 px
        SetOutputSize(*_renderer, 480.0f, 270.0f);
        Draw();
        const int index = IndexOf(kSeat0, T("9p"));
        const TileSlot* slot = HandSlot(index);
        ASSERT_NE(slot, nullptr);
        const float scale = 480.0f / kViewWidth;
        const float x = (slot->rect.x + slot->rect.w * 0.5f) * scale;
        const float y = (slot->rect.y + slot->rect.h * 0.5f) * scale;

        Click(x, y);
        Draw();

        EXPECT_TRUE(Hand().Discards().empty());
        EXPECT_EQ(_table->SelectedHandIndex(), index);

        Click(x, y);

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p"));
    }

    TEST_F(MahjongTableInputTest, TheRegimeIsTheOneOfTheLastDrawWhenTheOutputDiffersBetweenDraws) {
        // The editor: the table is drawn into the viewport's small target, while a click and the scene update see the whole editor window.
        // The regime of the draw holds between draws, so the first click selects, the update keeps it and the second click discards
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        SetOutputSize(*_renderer, 480.0f, 270.0f);
        Draw();
        const int index = IndexOf(kSeat0, T("9p"));
        const TileSlot* slot = HandSlot(index);
        ASSERT_NE(slot, nullptr);
        const float scale = 480.0f / kViewWidth;
        const float x = (slot->rect.x + slot->rect.w * 0.5f) * scale;
        const float y = (slot->rect.y + slot->rect.h * 0.5f) * scale;
        const auto atEditorWindow = [&] { SetOutputSize(*_renderer, kViewWidth * 2.0f, kViewHeight * 2.0f); };
        const auto atTarget = [&] { SetOutputSize(*_renderer, 480.0f, 270.0f); };

        atEditorWindow();
        Click(x, y);
        AdvanceTime(0.01f);
        atTarget();
        Draw();

        EXPECT_TRUE(Hand().Discards().empty());
        ASSERT_EQ(_table->SelectedHandIndex(), index);

        atEditorWindow();
        AdvanceTime(0.01f);
        Click(x, y);

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p"));
    }

    TEST_F(MahjongTableInputTest, TheSelectionIsGoneWhenTheWindowGrowsAndTheRegimeIsOffAgain) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        SetOutputSize(*_renderer, 480.0f, 270.0f);  // under the touch minimum even with the phone profile
        Draw();
        const TileSlot* slot = HandSlot(IndexOf(kSeat0, T("9p")));
        ASSERT_NE(slot, nullptr);
        const float scale = 480.0f / kViewWidth;
        Click((slot->rect.x + slot->rect.w * 0.5f) * scale, (slot->rect.y + slot->rect.h * 0.5f) * scale);
        Draw();
        ASSERT_TRUE(_table->SelectedHandIndex().has_value());

        SetOutputSize(*_renderer, kViewWidth, kViewHeight);
        Draw();

        EXPECT_FALSE(_table->SelectedHandIndex().has_value());
    }

    TEST_F(MahjongTableInputTest, TheSelectionIsGoneWhenTheTurnEndsAndTheComputersPlayOnToTheNextTurnOfThePerson) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        TapHandTile(IndexOf(kSeat0, T("1p")));
        Draw();
        ASSERT_TRUE(_table->SelectedHandIndex().has_value());

        // A control client plays 9p for the person; the computers play on until the person is asked again
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.PerformPerson(HandAction::MakeDiscard(T("9p"), false)).has_value());
        session.RunComputers();
        ASSERT_EQ(session.Actor(), kSeat0);
        ASSERT_EQ(Hand().GetPhase(), Phase::Discard);
        Draw();

        EXPECT_FALSE(_table->SelectedHandIndex().has_value()) << "a selection belongs to the turn it was made in, though the tile at its index may be the same";
        EXPECT_EQ(Var("selectedTileText"), "");
    }

    TEST_F(MahjongTableInputTest, ADiscardOfTheControlApiWithASelectionDiscardsThatTileAndClearsIt) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        TapHandTile(IndexOf(kSeat0, T("1p")));
        Draw();
        ASSERT_TRUE(_table->SelectedHandIndex().has_value());

        const auto action = ParseJson(R"({"type": "discard", "tile": "9p", "declareCalling": false})").value();
        ASSERT_TRUE(_game->AsControllable()->PerformAction(action).has_value());
        Draw();

        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("9p")) << "the control API acts on the discard, not on the tap";
        EXPECT_FALSE(_table->SelectedHandIndex().has_value());
    }

    TEST_F(MahjongTableInputTest, ATapDoesNotSelectInAClaimWindowOfThePerson) {
        constexpr SeatSetup personAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        ASSERT_NO_FATAL_FAILURE(Start(personAtSeat1));
        FixedWallSpec spec = SpecOf(kPlainHand, "5p");
        spec.hands[kSeat1] = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        Lay(model);
        Draw();
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        const std::string before = Snapshot(Hand());

        TapHandTile(0);
        Draw();

        EXPECT_FALSE(_table->SelectedHandIndex().has_value()) << "there is no tile to raise in a claim window";
        EXPECT_EQ(Snapshot(Hand()), before);
    }

    TEST_F(MahjongTableInputTest, NoSelectionSurvivesTheCoverOfTheNextPerson) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, kPlainHand));
        EmitAction("MahjongReveal");
        Draw();
        const int index = IndexOf(kSeat0, T("9p"));
        TapHandTile(index);
        Draw();
        ASSERT_EQ(_table->SelectedHandIndex(), index);

        TapHandTile(index);  // the discard: the next person's cover goes up
        Draw();

        ASSERT_TRUE(_game->GetSession().HandoffPending());
        EXPECT_FALSE(_table->SelectedHandIndex().has_value());
        EXPECT_EQ(Var("selectedTileText"), "");
    }

    TEST_F(MahjongTableInputTest, WithTheDeclareSwitchOnATapOnATileThatCannotDeclareDiscardsNothingAndTheNextTapOnADeclaringTileSelectsIt) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        EmitAction("MahjongDeclare", "on");
        Draw();
        const int plain = IndexOf(kSeat0, T("Ew"));
        const int declaring = IndexOf(kSeat0, T("Rd"));
        const std::string before = Snapshot(Hand());

        TapHandTile(plain);
        TapHandTile(plain);  // the second tap asks for a discard that does not declare: the model refuses it
        Draw();

        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_TRUE(_table->DeclareNext()) << "the flag stays until a discard goes through";
        EXPECT_EQ(_table->SelectedHandIndex(), plain) << "the selection stays";

        TapHandTile(declaring);
        Draw();

        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_EQ(_table->SelectedHandIndex(), declaring);
        TapHandTile(declaring);
        EXPECT_EQ(Hand().Discards().front().tile, T("Rd"));
        EXPECT_TRUE(Hand().GetHand(kSeat0).callingDeclared);
    }

    TEST_F(MahjongTableInputTest, TheDeclareSwitchKeepsTheSelection) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        const int index = IndexOf(kSeat0, T("Rd"));
        TapHandTile(index);
        Draw();

        EmitAction("MahjongDeclare", "on");
        Draw();

        EXPECT_EQ(_table->SelectedHandIndex(), index);
    }

    TEST_F(MahjongTableInputTest, TheSelectedTileIsDrawnRaisedAboveItsSlotAfterEveryOtherTileWithItsMarks) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kWaitingHand));
        EmitAction("MahjongDeclare", "on");
        const int index = IndexOf(kSeat0, T("Ew"));  // cannot declare: veiled
        TapHandTile(index);
        Draw();
        const TileSlot* slot = HandSlot(index);
        ASSERT_NE(slot, nullptr);
        const Rect raised = RaisedRect(slot->rect);
        EXPECT_FLOAT_EQ(raised.y, slot->rect.y - kSelectedTileRise);
        EXPECT_FLOAT_EQ(raised.h, slot->rect.h);

        const std::vector<DrawCall> calls = TileCalls();
        ASSERT_EQ(calls.size(), _table->GetLayout().slots.size());
        const DrawCall& last = calls.back();
        EXPECT_EQ(last.rect.x, raised.x);
        EXPECT_EQ(last.rect.y, raised.y);
        ASSERT_TRUE(last.source.has_value());
        EXPECT_EQ(last.source->x, SourceRect(T("Ew")).x) << "the raised tile is drawn last: over the tiles of the bonus row that lie above the hand";
        EXPECT_EQ(last.source->y, SourceRect(T("Ew")).y);
        // Its veil follows the raise, the veils of the others do not
        int veilsAtRaised = 0;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect && SameColor(call.color, kVeil) && call.rect.x == raised.x && call.rect.y == raised.y) {
                ++veilsAtRaised;
            }
        }
        EXPECT_EQ(veilsAtRaised, 1);
        EXPECT_EQ(VeilCalls(), kTilesThatCannotDeclare);
    }

    TEST_F(MahjongTableInputTest, AnUnchangedFrameWithASelectionDoesNotMoveTheVariables) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        TapHandTile(IndexOf(kSeat0, T("9p")));
        Draw();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();

        Draw();
        Draw();

        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    TEST_F(MahjongTableInputTest, TheNameOfTheSelectedTileFollowsTheLanguage) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        TapHandTile(IndexOf(kSeat0, T("9p")));
        Draw();
        ASSERT_EQ(Var("selectedTileText"), "Selected: 9 Dots");

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        Draw();

        const eastl::string expected = SelectedTileText(CoreManager::GetUIStrings(), T("9p"));
        EXPECT_EQ(Var("selectedTileText"), std::string(expected.data(), expected.size()));
        EXPECT_NE(Var("selectedTileText"), "Selected: 9 Dots");
    }

    // ----- the pace of the computers -----

    TEST_F(MahjongTableInputTest, AComputerPlaysOneActionAfterThePauseAndNotBefore) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kPlainHand, kSeat1));
        TableSession& session = _game->GetSession();
        ASSERT_EQ(session.Actor(), kSeat1);
        const uint64_t version = session.Version();
        const std::string before = Snapshot(Hand());

        Elapse(kComputerStepSeconds * kPauseFraction);
        Elapse(kComputerStepSeconds * kPauseFraction);
        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_EQ(session.Version(), version);

        Elapse(kComputerStepSeconds * kPauseFraction);  // 1.2 pauses in all
        EXPECT_EQ(session.Version(), version + 1) << "exactly one action";
        EXPECT_NE(Snapshot(Hand()), before);

        Elapse(kComputerStepSeconds * kPauseFraction);  // the pause starts again after a step
        EXPECT_EQ(session.Version(), version + 1);
    }

    TEST_F(MahjongTableInputTest, ALongFrameStillPlaysOnlyOneAction) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kPlainHand, kSeat1));
        const uint64_t version = _game->GetSession().Version();

        Elapse(kLongFrame);

        EXPECT_EQ(_game->GetSession().Version(), version + 1);
    }

    TEST_F(MahjongTableInputTest, TheComputersPlayOnToAPersonAndStopThere) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, kPlainHand, kSeat1));
        TableSession& session = _game->GetSession();
        int frames = 0;
        while (!session.ActorIsPerson() && session.GetPhase() == SessionPhase::Playing && frames++ < kActionLimit) {
            Elapse(kComputerStepSeconds);
        }
        ASSERT_TRUE(session.ActorIsPerson());
        const uint64_t version = session.Version();
        const std::string before = Snapshot(Hand());

        for (int i = 0; i < 5; ++i) {
            Elapse(kLongFrame);
        }

        EXPECT_EQ(session.Version(), version) << "nothing moves while a person has to act";
        EXPECT_EQ(Snapshot(Hand()), before);
    }

    TEST_F(MahjongTableInputTest, TheComputersPlayAWholeHandAtTheirPaceAndThenStop) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        TableSession& session = _game->GetSession();
        int frames = 0;
        while (session.GetPhase() == SessionPhase::Playing && frames++ < kActionLimit) {
            Elapse(kComputerStepSeconds);
        }
        ASSERT_EQ(session.GetPhase(), SessionPhase::HandOver);
        const uint64_t version = session.Version();

        Elapse(kLongFrame);

        EXPECT_EQ(session.Version(), version);
    }

    // ----- the cover of a hot seat -----

    TEST_F(MahjongTableInputTest, TheCoverIsUpUntilThePersonPressesShowAndBlocksTheTilesAndTheButtons) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, kWaitingHand));
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending()) << "nobody has seen the table yet";
        EXPECT_TRUE(CoverShown());
        EXPECT_EQ(Var("handoffSeat"), "Seat 1 (East)");
        for (int i = 0; i < kButtonCount; ++i) {
            EXPECT_FALSE(_table->GetButtons().buttons[static_cast<size_t>(i)].shown) << i;
        }
        const std::string before = Snapshot(Hand());

        EXPECT_TRUE(ClickHandTile(IndexOf(kSeat0, T("9p")))) << "the modal canvas takes the click";
        EmitAction("MahjongDeclare", "on");
        EmitAction("MahjongWin");
        EmitAction("MahjongPass");
        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_FALSE(_table->DeclareNext());

        EXPECT_TRUE(ClickElement("Handoff", "Reveal"));
        Draw();

        EXPECT_FALSE(session.HandoffPending());
        EXPECT_EQ(session.Viewer(), kSeat0);
        EXPECT_FALSE(CoverShown());
        EXPECT_TRUE(_table->GetButtons()[ButtonKind::Declare].shown);
        EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("9p"))));
        EXPECT_EQ(Hand().Discards().front().tile, T("9p"));
    }

    TEST_F(MahjongTableInputTest, TheCoverSpeaksTheLanguageOfTheTableAndFollowsASwitchWhileItIsUp) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, kWaitingHand));
        ASSERT_TRUE(CoverShown());
        const auto textOf = [this](const char* element) { return DisplayText(Element("Handoff", element)); };
        Draw();
        EXPECT_EQ(textOf("HandoffText"), "Pass the screen to Seat 1 (East)");
        EXPECT_EQ(textOf("Reveal"), "Show");

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        Draw();

        EXPECT_EQ(Var("handoffSeat"), "Место 1 (Восток)") << "the seat is named in the new language at the next frame";
        EXPECT_EQ(textOf("HandoffText"), "Передайте экран: Место 1 (Восток)");
        EXPECT_EQ(textOf("Reveal"), "Показать");
        EXPECT_TRUE(CoverShown());
    }

    TEST_F(MahjongTableInputTest, AfterAPersonsDiscardTheCoverGoesUpForTheOtherPerson) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, kPlainHand));
        EmitAction("MahjongReveal");
        Draw();
        ASSERT_FALSE(_game->GetSession().HandoffPending());
        ClickHandTile(IndexOf(kSeat0, T("9p")));
        Draw();

        TableSession& session = _game->GetSession();
        ASSERT_EQ(session.Actor(), kSeat1);
        EXPECT_TRUE(session.HandoffPending());
        EXPECT_EQ(session.Viewer(), kNoSeat);
        EXPECT_TRUE(CoverShown());
        EXPECT_EQ(Var("handoffSeat"), "Seat 2 (South)");

        const std::string before = Snapshot(Hand());
        ClickHandTile(0);
        EXPECT_EQ(Snapshot(Hand()), before);

        EmitAction("MahjongReveal");
        Draw();
        EXPECT_FALSE(session.HandoffPending());
        EXPECT_EQ(session.Viewer(), kSeat1);
        EXPECT_FALSE(CoverShown());
        EXPECT_EQ(_table->GetLayout().viewer, kSeat1);

        const uint64_t version = session.Version();
        EmitAction("MahjongReveal");  // no cover is up: nothing to show
        EXPECT_EQ(session.Version(), version);
    }

    TEST_F(MahjongTableInputTest, OnePersonNeverGetsACover) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));

        EXPECT_FALSE(_game->GetSession().HandoffPending());
        EXPECT_FALSE(CoverShown());
        ClickHandTile(IndexOf(kSeat0, T("9p")));
        Draw();
        EXPECT_FALSE(CoverShown());
    }

    // ----- the control API -----

    TEST_F(MahjongTableInputTest, AnActionOfTheControlApiChangesWhatTheNextDrawShows) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup));
        const size_t pileBefore = Hand().Discards().size();

        const auto action = ParseJson(R"({"type": "discard", "tile": "9p", "declareCalling": false})").value();
        const auto outcome = _game->AsControllable()->PerformAction(action);
        ASSERT_TRUE(outcome.has_value());
        Draw();

        ASSERT_GT(Hand().Discards().size(), pileBefore);
        int discardSlots = 0;
        for (const TileSlot& slot : _table->GetLayout().slots) {
            discardSlots += slot.role == SlotRole::DiscardTile ? 1 : 0;
        }
        EXPECT_EQ(discardSlots, static_cast<int>(Hand().Discards().size()));
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0));
    }

}  // namespace BECore::Mahjong::Tests
