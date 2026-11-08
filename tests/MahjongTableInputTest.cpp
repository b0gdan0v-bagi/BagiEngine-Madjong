#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIToggle.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
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
                const bool veil = call.kind == DrawKind::Rect && call.color.r == kVeil.r && call.color.g == kVeil.g && call.color.b == kVeil.b && call.color.a == kVeil.a;
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
        const auto* declare = Element("Actions", "Declare")->Cast<UIToggle>();
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
