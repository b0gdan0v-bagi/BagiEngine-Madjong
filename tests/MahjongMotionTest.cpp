#include "MahjongTableTestSupport.h"

#include <Mahjong/Components/MahjongHudComponent.h>

/**
 * The motion of the real `Setup` canvas (`config/nodes/MahjongSetup.xml`, the `kMotion*` of `Table/UiPalette.h`) on the manual clock: closed, opened, a frame at the start of the show (nothing
 * visible yet, the click is taken and does nothing), settled (opaque, the click works), closed (the screen is free at once, the card goes after the hide). The numbers in the files are held to the
 * constants by `MahjongUiStyleTest`; the helper that runs the clock is `SettleTransitions` of the table fixture.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kDimMinShare = 0.9f;
        constexpr uint8_t kDimAlpha = 200;  // `color="0,0,0,200"` of the `Dim` element

        /// The alpha of the dim of `Setup` as drawn in the last frame (the rect of black that covers the view, or the 0.96 of it while the card rises); nullopt when it was not drawn.
        std::optional<uint8_t> DimAlpha(const TurnRecordingRenderer& renderer) {
            std::optional<uint8_t> alpha;
            for (const DrawCall& call : renderer.calls) {
                if (call.kind == DrawKind::Rect && call.color.r == 0 && call.color.g == 0 && call.color.b == 0 && call.rect.w >= kDimMinShare * kViewWidth &&
                    call.rect.h >= kDimMinShare * kViewHeight) {
                    alpha = call.color.a;
                }
            }
            return alpha;
        }

    }  // namespace

    class MahjongMotionTest : public MahjongTableTestBase {
    protected:
        void SetUp() override {
            MahjongTableTestBase::SetUp();
            ASSERT_NE(_game, nullptr);
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            Draw();
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }
    };

    TEST_F(MahjongMotionTest, TheCanvasCarriesTheNamedMotion) {
        const auto setup = Canvas("Setup");
        ASSERT_TRUE(setup);
        EXPECT_FLOAT_EQ(setup->_transitionDuration, kMotionShowSeconds);
        EXPECT_FLOAT_EQ(setup->_transitionHideDuration, kMotionHideSeconds);
        EXPECT_FLOAT_EQ(setup->_transitionScaleFrom, kMotionScaleFrom);
        EXPECT_EQ(EnumUtils<EaseType>::ToString(setup->_transitionEase), kMotionEase);
    }

    TEST_F(MahjongMotionTest, TheStartPanelRisesFromNothingAndTakesTheClickOnlyOnceItIsThere) {
        ASSERT_FALSE(SetupShown());
        ASSERT_FALSE(DimAlpha(*_renderer).has_value());
        const SeatSetup before = MahjongHudComponent::GetActive()->PendingSeats();

        _game->OpenSetup();
        Draw();
        EXPECT_TRUE(SetupShown()) << "a canvas that is showing consumes events, so the table below stays shielded";
        ASSERT_TRUE(DimAlpha(*_renderer).has_value());
        EXPECT_EQ(*DimAlpha(*_renderer), 0) << "the first frame of the show is at nothing";

        const UIElement* seat = Element("Setup", "Seat2Person");
        ASSERT_NE(seat, nullptr);
        const bool taken = Click(seat->rect.x + seat->rect.w * 0.5f, seat->rect.y + seat->rect.h * 0.5f);  // not ClickElement: it lets the show finish first
        Draw();
        EXPECT_TRUE(taken) << "the click is consumed";
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingSeats(), before) << "and delivers nothing";

        SettleTransitions();
        EXPECT_EQ(DimAlpha(*_renderer), kDimAlpha) << "after the show the card is opaque";
        ASSERT_TRUE(ClickElement("Setup", "Seat2Person"));
        Draw();
        EXPECT_NE(MahjongHudComponent::GetActive()->PendingSeats(), before) << "and takes the click";
    }

    TEST_F(MahjongMotionTest, TheHalfwayFrameIsBetweenNothingAndTheWholeCard) {
        _game->OpenSetup();
        Draw();
        CoreManager::GetTime().Advance(0.125);
        Draw();
        const std::optional<uint8_t> halfway = DimAlpha(*_renderer);
        ASSERT_TRUE(halfway.has_value());
        EXPECT_GT(*halfway, 0);
        EXPECT_LT(*halfway, kDimAlpha);
    }

    TEST_F(MahjongMotionTest, TheClosedPanelFreesTheScreenAtOnceAndIsGoneAfterTheHide) {
        _game->OpenSetup();
        Draw();
        SettleTransitions();
        ASSERT_TRUE(SetupShown());

        ASSERT_TRUE(ClickElement("Setup", "SetupClose"));
        Draw();
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(SetupShown()) << "a leaving modal frees the screen: input is released at once";
        EXPECT_EQ(DimAlpha(*_renderer), kDimAlpha) << "the card is still drawn in the first frame of the hide";

        SettleTransitions();
        EXPECT_FALSE(DimAlpha(*_renderer).has_value()) << "and not at all after it";
    }

}  // namespace BECore::Mahjong::Tests
