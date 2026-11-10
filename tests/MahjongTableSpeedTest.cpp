#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Events/SceneEvents.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The speed of the table on the table itself (`Components/MahjongTableComponent.h`, "Pace"; the factors are `Table/AnimationSpeed.h`):
 * the pause of the computers, the length of a flight and the blink and the length of the mark of a set just laid follow
 * `MahjongGame::GetAnimationSpeed()`, and the control API does not wait at any speed.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// Not quite the whole of a pause, then the rest of it with some to spare.
        constexpr float kMostOf = 0.9f;
        constexpr float kTheRest = 0.2f;

        void Elapse(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

    }  // namespace

    class MahjongTableSpeedTest : public MahjongTableTestBase {
    protected:
        /// A match with seat 1 (a computer) to act on a plain hand, drawn.
        void StartWithComputerToAct(AnimationSpeed speed) {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            _game->SetAnimationSpeed(speed);
            Lay(MakeModel(SpecOf(kPlainHand, "9p"), kSeat1));
            Draw();
        }

        /// How many rectangles of `color` the last draw has.
        size_t RectCount(const Color& color) const {
            size_t count = 0;
            for (const DrawCall& call : _renderer->calls) {
                count += call.kind == DrawKind::Rect && SameColor(call.color, color) ? 1 : 0;
            }
            return count;
        }
    };

    TEST_F(MahjongTableSpeedTest, AComputerWaitsItsPauseAtTheSpeedOfTheGame) {
        for (const AnimationSpeed speed : EnumUtils<AnimationSpeed>::Values()) {
            SCOPED_TRACE(SpeedText(speed).data());
            ASSERT_NO_FATAL_FAILURE(StartWithComputerToAct(speed));
            const float pause = ScaledSeconds(kComputerStepSeconds, speed);
            const uint64_t version = _game->GetSession().Version();

            Elapse(pause * kMostOf);
            EXPECT_EQ(_game->GetSession().Version(), version) << "the pause is not over";
            Elapse(pause * kTheRest);
            EXPECT_EQ(_game->GetSession().Version(), version + 1) << "exactly one action once it is";
        }
    }

    TEST_F(MahjongTableSpeedTest, ATileFliesForTheFlightTimeAtTheSpeedOfTheGame) {
        for (const AnimationSpeed speed : EnumUtils<AnimationSpeed>::Values()) {
            SCOPED_TRACE(SpeedText(speed).data());
            ASSERT_NO_FATAL_FAILURE(StartWithComputerToAct(speed));
            UseFlights();
            Elapse(ScaledSeconds(kComputerStepSeconds, speed) * (kMostOf + kTheRest));  // the computer discards
            Draw();
            ASSERT_GT(_table->FlightCount(), 0u) << "the discard flies";

            const float flight = ScaledSeconds(kTileFlightSeconds, speed);
            Elapse(flight * kMostOf);
            Draw();
            EXPECT_GT(_table->FlightCount(), 0u) << "still in the air before the scaled time";
            Elapse(flight * kTheRest);
            Draw();
            EXPECT_EQ(_table->FlightCount(), 0u) << "landed once the scaled time is over";
        }
    }

    TEST_F(MahjongTableSpeedTest, TheSetJustLaidBlinksAndEndsAtTheSpeedOfTheGame) {
        for (const AnimationSpeed speed : EnumUtils<AnimationSpeed>::Values()) {
            SCOPED_TRACE(SpeedText(speed).data());
            ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
            _game->SetAnimationSpeed(speed);
            Draw();
            Lay(OpenPungModel(kSeat1Hand));
            Draw();
            ASSERT_EQ(RectCount(kClaimHighlight), 1u) << "on at the moment of the move";

            // The mark lasts four blink periods; a little into the last one it is still running and on
            const float period = ScaledSeconds(kClaimBlinkSeconds, speed);
            const float total = ScaledSeconds(kClaimHighlightSeconds, speed);
            ASSERT_FLOAT_EQ(total, 4.0f * period) << "the blink and the mark scale together";
            Elapse(total - period + period * 0.1f);
            Draw();
            EXPECT_EQ(RectCount(kClaimHighlight), 1u) << "still blinking in the last period";
            Elapse(period * 0.2f);
            Draw();
            EXPECT_EQ(RectCount(kClaimHighlight), 1u) << "and still on (half a period on)";
            Elapse(period * 0.5f);
            Draw();
            EXPECT_EQ(RectCount(kClaimHighlight), 0u) << "gone with the scaled time (or in its off half)";
            Elapse(period);
            Draw();
            EXPECT_EQ(RectCount(kClaimHighlight), 0u) << "over for good";
        }
    }

    TEST_F(MahjongTableSpeedTest, TheControlApiPlaysTheComputersAtOnceAtEverySpeed) {
        for (const AnimationSpeed speed : EnumUtils<AnimationSpeed>::Values()) {
            SCOPED_TRACE(SpeedText(speed).data());
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            _game->SetAnimationSpeed(speed);
            EXPECT_GT(_game->GetSession().RunComputers(), 0);
            EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver) << "no clock is needed";
        }
    }

}  // namespace BECore::Mahjong::Tests
