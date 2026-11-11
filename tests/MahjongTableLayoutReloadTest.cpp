#include "MahjongTableTestSupport.h"
#include "ShippedTableLayout.h"

#include <Mahjong/Table/TableLayout.h>

/**
 * The table draws the zones of the active spec every frame (`SetActiveTableLayoutSpec`), so a spec set while the game runs shows at the next
 * draw and no cache keeps the old layout. What reaches the active spec from the file (the poll of the file stamp, the reload of the config)
 * needs the game's `config/`, which the test environment does not mount; `TableLayoutSpecTest` holds the offer of a document (`TableLayoutOfferTest`).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kMovedHandX = 0.4f;

        class MahjongTableLayoutReloadTest : public MahjongTableTestBase {
        protected:
            void TearDown() override {
                SetActiveTableLayoutSpec(&ShippedTableLayoutSpec());
                MahjongTableTestBase::TearDown();
            }

            /// The hand row of the seat at the top of the table, as the last draw laid it out.
            Rect TopHand() const {
                const TableLayout& layout = _table->GetLayout();
                return BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(layout.viewer, 2));
            }

            TableLayoutSpec _moved;
        };

        TEST_F(MahjongTableLayoutReloadTest, ASpecSetWhileTheTableIsDrawnShowsAtTheNextDrawAndBackAgain) {
            Start(kAllComputers);
            Draw();
            const Rect shipped = TopHand();
            ASSERT_GT(shipped.w, 0.0f);

            _moved = ShippedTableLayoutSpec();
            _moved.Shape(TableShape::Landscape).seats[2].hand.rect.x = kMovedHandX;
            SetActiveTableLayoutSpec(&_moved);
            Draw();
            const Rect moved = TopHand();
            EXPECT_GT(moved.x, shipped.x + 1.0f);
            EXPECT_FLOAT_EQ(moved.w, shipped.w);  // the same tiles, the zone moved

            SetActiveTableLayoutSpec(&ShippedTableLayoutSpec());
            Draw();
            EXPECT_FLOAT_EQ(TopHand().x, shipped.x);
        }

        TEST_F(MahjongTableLayoutReloadTest, TheDrawKeepsPollingWhileThereIsNoConfigWithoutChangingTheSpec) {
            Start(kAllComputers);
            ASSERT_FALSE(CoreManager::GetConfigManager().HasConfig("TableLayout"_intern));  // the tests have no game config
            Draw();
            Draw();
            EXPECT_EQ(ActiveTableLayoutSpec(), &ShippedTableLayoutSpec());
        }

    }  // namespace

}  // namespace BECore::Mahjong::Tests
