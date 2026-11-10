#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Table/UiPalette.h>

/**
 * The rack under the viewer's hand (`kHandRack`, `kHandRackLip`, `MahjongTableComponent`): one plate for the seat at the bottom, drawn
 * before the turn backdrop and the tiles, wooden and never gold, and clear of the seat plate and of the action area.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kRimUnits = 8.0f;

        bool IsGold(const Color& color) {
            return color.r == kTurnMark.r && color.g == kTurnMark.g && color.b == kTurnMark.b;
        }

    }  // namespace

    class MahjongHandRackTest : public MahjongTableTestBase {
    protected:
        std::vector<size_t> CallsOf(const Color& color) const {
            std::vector<size_t> found;
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                if (_renderer->calls[i].kind == DrawKind::Rect && SameColor(_renderer->calls[i].color, color)) {
                    found.push_back(i);
                }
            }
            return found;
        }

        /// The first call that is a tile: a shadow or a texture other than the felt.
        size_t FirstTile() const {
            for (size_t i = 1; i < _renderer->calls.size(); ++i) {
                const DrawCall& call = _renderer->calls[i];
                if (call.kind == DrawKind::Texture || (call.kind == DrawKind::Rect && SameColor(call.color, kTileShadowSoft))) {
                    return i;
                }
            }
            return _renderer->calls.size();
        }

        /// The plate and the body of the rack of the last draw; fails when it is not drawn exactly once.
        void RackOf(Rect& plate, Rect& body) const {
            const std::vector<size_t> lip = CallsOf(kHandRackLip);
            const std::vector<size_t> wood = CallsOf(kHandRack);
            ASSERT_EQ(lip.size(), 1u);
            ASSERT_EQ(wood.size(), 1u);
            EXPECT_EQ(wood[0], lip[0] + 1) << "the body is drawn right on the plate";
            plate = _renderer->calls[lip[0]].rect;
            body = _renderer->calls[wood[0]].rect;
        }
    };

    TEST_F(MahjongHandRackTest, TheRackIsOnePlateUnderTheViewersHandDrawnBeforeTheBackdropAndTheTiles) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        Rect plate;
        Rect body;
        ASSERT_NO_FATAL_FAILURE(RackOf(plate, body));
        const TableLayout& layout = _table->GetLayout();
        const Rect hand = BoundsOf(layout, SlotRole::HandTile, layout.viewer);
        ASSERT_GT(hand.w, 0.0f);
        // The hand and its padding lie on the plate (at the bottom the plate ends at the rim, which is as far as the hand's padding goes)
        EXPECT_FLOAT_EQ(plate.x, hand.x - kRackPadding);
        EXPECT_FLOAT_EQ(plate.y, hand.y - kRackPadding);
        EXPECT_FLOAT_EQ(plate.w, hand.w + 2.0f * kRackPadding);
        EXPECT_FLOAT_EQ(plate.y + plate.h, std::min(hand.y + hand.h + kRackPadding, kViewHeight - kRimUnits));
        EXPECT_GE(plate.y + plate.h, hand.y + hand.h);
        // The body is the lighter top of it: the edge shows left, right and below, not above
        EXPECT_FLOAT_EQ(body.x, plate.x + kHandRackLipWidth);
        EXPECT_FLOAT_EQ(body.y, plate.y);
        EXPECT_FLOAT_EQ(body.w, plate.w - 2.0f * kHandRackLipWidth);
        EXPECT_FLOAT_EQ(body.y + body.h, plate.y + plate.h - kHandRackLipWidth);

        // After the felt, the rim and its lip; before the backdrop of the hand and the first shadow or tile
        const size_t rack = CallsOf(kHandRackLip)[0];
        EXPECT_GE(rack, 9u);
        EXPECT_LT(rack, FirstTile());
        // The turn tints are the seat plate of the seat to move and the backdrop behind its hand, the last of them
        ASSERT_EQ(CallsOf(kTurnMark).size(), 2u);
        EXPECT_LT(rack, CallsOf(kTurnMark).back());
    }

    TEST_F(MahjongHandRackTest, TheRackIsPublicSoItIsDrawnForASpectatorToo) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();

        Rect plate;
        Rect body;
        ASSERT_NO_FATAL_FAILURE(RackOf(plate, body));
        const TableLayout& layout = _table->GetLayout();
        EXPECT_FLOAT_EQ(plate.x, BoundsOf(layout, SlotRole::HandTile, layout.viewer).x - kRackPadding);
    }

    TEST_F(MahjongHandRackTest, TheRackIsWoodAndTakesNoGold) {
        EXPECT_FALSE(IsGold(kHandRack));
        EXPECT_FALSE(IsGold(kHandRackLip));
        EXPECT_FALSE(SameColor(kHandRack, kHandRackLip));
        EXPECT_GT(kHandRack.r + kHandRack.g + kHandRack.b, kHandRackLip.r + kHandRackLip.g + kHandRackLip.b) << "the lip is the darker";
        EXPECT_EQ(kHandRack.a, 255);
        EXPECT_EQ(kHandRackLip.a, 255);
    }

    TEST_F(MahjongHandRackTest, TheWidestRackIsClearOfTheBottomSeatPlateAndOfTheActionArea) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const TableLayout worst = MakeWorstCaseTableLayout();
        const Rect hand = BoundsOf(worst, SlotRole::HandTile, worst.viewer);
        const Rect rack(hand.x - kRackPadding, hand.y - kRackPadding, hand.w + 2.0f * kRackPadding, hand.h + 2.0f * kRackPadding);

        EXPECT_FALSE(Intersects(rack, worst.zones.seatPlates[worst.viewer])) << "the seat plate of the bottom position";
        EXPECT_GE(rack.x, kRimUnits);
        EXPECT_LE(rack.x + rack.w, kViewWidth - kRimUnits);
        EXPECT_GE(rack.y, 0.0f);

        const auto actions = Canvas("Actions");
        const auto hints = Canvas("HintsButton");
        ASSERT_TRUE(actions && hints);
        ASSERT_TRUE(hints->SetElementVisible("HintButton"_intern, true));
        actions->UpdateLayout(kViewWidth, kViewHeight);
        hints->UpdateLayout(kViewWidth, kViewHeight);
        for (const char* name : {"ActionRow", "Hint"}) {
            const UIElement* element = Element("Actions", name);
            ASSERT_NE(element, nullptr) << name;
            EXPECT_FALSE(Intersects(rack, element->rect)) << name;
        }
        const UIElement* hint = Element("HintsButton", "HintButton");
        ASSERT_NE(hint, nullptr);
        EXPECT_FALSE(Intersects(rack, hint->rect));
    }

}  // namespace BECore::Mahjong::Tests
