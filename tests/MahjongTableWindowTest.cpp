#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Events/SceneEvents.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The claim window on the table (`Components/MahjongTableComponent.h`): the mark and the frame on the discard do not tell a person that a
 * computer holds a claim, and the pause after a discard is one pause whether or not a window opened. The fixed hands come from
 * `FixedWall.h`, laid into the hand in progress.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seat 0 holds a hand that is far from waiting; as East it draws 5p.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// How far the mark (`kTurnMark` of `UiPalette.h`) behind the hand of the seat whose turn it is reaches past the hand.
        constexpr float kTurnMarkMargin = 4.0f;
        /// Seat 1 holds 4p 5p 5p 6p 7p: a discarded 5p gives it a pung and two chows, so it has a claim.
        constexpr const char* kSeat1HoldsPairOf5p = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
        constexpr SeatSetup kPersonAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        /// A fraction of the pause per event: three of them are more than a pause, two are less.
        constexpr float kPauseFraction = 0.4f;

        void Elapse(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

    }  // namespace

    /// A match where East discards a 5p: with `claimable` seat 1 holds a claim on it, otherwise nobody does.
    class MahjongTableWindowTest : public MahjongTableTestBase {
    protected:
        FixedWallSpec FiveSpec(bool claimable) const {
            FixedWallSpec spec = SpecOf(kPlainHand, "5p");
            if (claimable) {
                spec.hands[kSeat1] = kSeat1HoldsPairOf5p;
            }
            return spec;
        }

        /// The person (East) discards the 5p by a click.
        void PersonDiscardsFive(bool claimable) {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            Lay(MakeModel(FiveSpec(claimable)));
            Draw();
            ASSERT_GE(IndexOf(kSeat0, T("5p")), 0);
            ClickHandTile(IndexOf(kSeat0, T("5p")));
            Draw();
        }

        /// The computer East discards the 5p; the person at seat 1 is shown the table.
        void ComputerDiscardsFive(bool claimable) {
            ASSERT_NO_FATAL_FAILURE(Start(kPersonAtSeat1));
            Lay(MakeModel(FiveSpec(claimable)));
            ASSERT_TRUE(_game->GetMatch().Hand().Discard(kSeat0, T("5p")).has_value());
            Draw();
        }

        /// The rectangles drawn in `color`.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        /// Where the mark lies when it is behind the hand of `seat`.
        Rect MarkOf(Seat seat) const {
            const Rect bounds = BoundsOf(_table->GetLayout(), SlotRole::HandTile, seat);
            return Rect(bounds.x - kTurnMarkMargin, bounds.y - kTurnMarkMargin, bounds.w + 2.0f * kTurnMarkMargin, bounds.h + 2.0f * kTurnMarkMargin);
        }
    };

    TEST_F(MahjongTableWindowTest, WhileAComputerMayClaimTheMarkStaysOnTheDiscarderAndNotOnThatComputer) {
        ASSERT_NO_FATAL_FAILURE(PersonDiscardsFive(true));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1) << "the computer with the claim is the one asked first";

        const std::vector<Rect> marks = RectsOf(kTurnMark);

        ASSERT_EQ(marks.size(), 1u);
        const Rect expected = MarkOf(kSeat0);
        EXPECT_EQ(marks[0].x, expected.x);
        EXPECT_EQ(marks[0].y, expected.y);
        EXPECT_EQ(marks[0].w, expected.w);
        EXPECT_EQ(marks[0].h, expected.h);
    }

    TEST_F(MahjongTableWindowTest, TheFrameOnTheDiscardIsNotDrawnWhenOnlyAComputerMayClaim) {
        ASSERT_NO_FATAL_FAILURE(PersonDiscardsFive(true));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        ASSERT_EQ(Hand().PendingClaimSeats(), (eastl::vector<Seat>{kSeat1})) << "the only seat that may claim is a computer";

        EXPECT_TRUE(RectsOf(kPendingFrame).empty()) << "a frame would tell the person that a computer holds a claim";
    }

    TEST_F(MahjongTableWindowTest, TheFrameOnTheDiscardIsDrawnForAViewerWhoMayClaimIt) {
        ASSERT_NO_FATAL_FAILURE(ComputerDiscardsFive(true));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        ASSERT_EQ(Hand().PendingClaimSeats(), (eastl::vector<Seat>{kSeat1}));
        ASSERT_EQ(_table->GetLayout().viewer, kSeat1);

        const std::vector<Rect> frames = RectsOf(kPendingFrame);

        ASSERT_EQ(frames.size(), 1u);
        // It lies round the last tile of the pile
        const int last = static_cast<int>(Hand().Discards().size()) - 1;
        const auto slot = std::find_if(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end(), [last](const TileSlot& s) { return s.role == SlotRole::DiscardTile && s.index == last; });
        ASSERT_NE(slot, _table->GetLayout().slots.end());
        EXPECT_LT(frames[0].x, slot->rect.x);
        EXPECT_GT(frames[0].x + frames[0].w, slot->rect.x + slot->rect.w);
    }

    TEST_F(MahjongTableWindowTest, TheShadowOfThePendingDiscardLiesUnderItsFrameSoTheGoldIsEven) {
        ASSERT_NO_FATAL_FAILURE(ComputerDiscardsFive(true));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);

        const int last = static_cast<int>(Hand().Discards().size()) - 1;
        const auto slot = std::find_if(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end(), [last](const TileSlot& s) { return s.role == SlotRole::DiscardTile && s.index == last; });
        ASSERT_NE(slot, _table->GetLayout().slots.end());
        int shadowAt = -1;
        int frameAt = -1;
        for (size_t i = 0; i < _renderer->calls.size(); ++i) {
            const DrawCall& call = _renderer->calls[i];
            if (call.kind != DrawKind::Rect) {
                continue;
            }
            if (SameColor(call.color, kPendingFrame)) {
                frameAt = static_cast<int>(i);
            } else if (SameColor(call.color, kTileShadow) && call.rect.x == slot->rect.x + 2.0f && call.rect.y == slot->rect.y + 2.0f) {
                shadowAt = static_cast<int>(i);
            }
        }
        ASSERT_GE(frameAt, 0);
        ASSERT_GE(shadowAt, 0);
        EXPECT_LT(shadowAt, frameAt) << "a shadow drawn over the frame would eat its right and bottom edge";
    }

    TEST_F(MahjongTableWindowTest, TheFrameIsGoneWhenNobodyMayClaimOrTheViewerHasDecided) {
        ASSERT_NO_FATAL_FAILURE(ComputerDiscardsFive(false));
        EXPECT_NE(Hand().GetPhase(), Phase::Claim) << "no seat holds a claim, so no window opens";
        EXPECT_TRUE(RectsOf(kPendingFrame).empty());

        ASSERT_NO_FATAL_FAILURE(ComputerDiscardsFive(true));
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakePass()).has_value());
        Draw();
        EXPECT_TRUE(RectsOf(kPendingFrame).empty()) << "the viewer passed; the window is not theirs any more";
    }

    TEST_F(MahjongTableWindowTest, OnePauseSettlesTheWindowAndPlaysTheNextDiscardLikeAnUnclaimedDiscard) {
        ASSERT_NO_FATAL_FAILURE(PersonDiscardsFive(false));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);
        const size_t pile = Hand().Discards().size();
        Elapse(kComputerStepSeconds);
        const size_t plainSteps = Hand().Discards().size() - pile;

        ASSERT_NO_FATAL_FAILURE(PersonDiscardsFive(true));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        const size_t windowPile = Hand().Discards().size();
        Elapse(kComputerStepSeconds * kPauseFraction);
        EXPECT_EQ(Hand().GetPhase(), Phase::Claim) << "nothing before the pause is over";
        Elapse(kComputerStepSeconds * kPauseFraction);
        Elapse(kComputerStepSeconds * kPauseFraction);

        EXPECT_EQ(plainSteps, 1u);
        EXPECT_EQ(Hand().Discards().size() - windowPile, plainSteps) << "the same one discard after the same one pause";
        EXPECT_NE(Hand().GetPhase(), Phase::Claim);
    }

    TEST_F(MahjongTableWindowTest, AComputerThatClaimsAlsoDiscardsWithinThePause) {
        // Seat 1 holds a pair of red dragons and a hand that a pung of them brings no further from winning: it claims (ComputerClaimTest)
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        FixedWallSpec spec = SpecOf(kPlainHand, "Rd");
        spec.hands[kSeat1] = "3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw Rd Rd";
        Lay(MakeModel(spec));
        Draw();
        ClickHandTile(IndexOf(kSeat0, T("Rd")));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        const size_t pile = Hand().Discards().size();

        Elapse(kComputerStepSeconds);

        const PlayerHand& seat1 = Hand().GetHand(kSeat1);
        ASSERT_FALSE(seat1.melds.empty()) << "seat 1 claimed the red dragon";
        EXPECT_EQ(seat1.melds.front().first, T("Rd"));
        EXPECT_NE(Hand().GetPhase(), Phase::Claim);
        // The claimed tile left the pile and the claimer discarded in its place, within the one pause: the pile is as long as before
        EXPECT_EQ(Hand().Discards().size(), pile) << "the claim took one tile from the pile, the discard added one";
        EXPECT_NE(Hand().CurrentSeat(), kSeat1) << "the turn has gone on to the next seat";
    }

}  // namespace BECore::Mahjong::Tests
