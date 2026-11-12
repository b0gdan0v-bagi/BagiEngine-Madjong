#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "ShippedTableLayout.h"

#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/Table/TableRegions.h>

/**
 * The table publishes `MakeTableRegions` of its layout to the `SceneView` canvas of every node in `kRegionNodes` (`PublishRegions`), so that
 * after a draw each of them holds every region at the layout's rectangle, in every shape and profile, and follows a new spec.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        class MahjongTableRegionsTest : public MahjongTableTestBase {
        protected:
            void TearDown() override {
                SetActiveTableLayoutSpec(&ShippedTableLayoutSpec());
                MahjongTableTestBase::TearDown();
            }

            /// The windows the table is drawn in: the design of each shape and every window of the touch checks (the phone profile among them).
            static std::vector<MatrixWindow> Windows() {
                std::vector<MatrixWindow> windows;
                for (const TableShape shape : {TableShape::Landscape, TableShape::Portrait, TableShape::Narrow, TableShape::Wide}) {
                    windows.push_back(DesignWindowOf(shape));
                }
                ForEachTableWindow([&](const MatrixWindow& window) { windows.push_back(window); });
                return windows;
            }

            /// The canvas of `node` holds exactly the regions of the layout of the last draw, each at its rectangle.
            void ExpectHoldsTheRegionsOfTheLayout(const char* node, const std::string& where) {
                const auto canvas = Canvas(node);
                ASSERT_TRUE(canvas) << node;
                const eastl::vector<TableRegion> expected = MakeTableRegions(_table->GetLayout());
                ASSERT_FALSE(expected.empty()) << where;
                const UIRegionSet& held = canvas->GetRegions();
                EXPECT_EQ(held.Size(), expected.size()) << node << " " << where;
                for (const TableRegion& region : expected) {
                    const Rect* found = held.Find(PoolString::Intern(eastl::string_view(region.name.data(), region.name.size())));
                    ASSERT_NE(found, nullptr) << node << " lacks " << region.name.c_str() << " " << where;
                    EXPECT_FLOAT_EQ(found->x, region.rect.x) << node << " " << region.name.c_str() << " " << where;
                    EXPECT_FLOAT_EQ(found->y, region.rect.y) << node << " " << region.name.c_str() << " " << where;
                    EXPECT_FLOAT_EQ(found->w, region.rect.w) << node << " " << region.name.c_str() << " " << where;
                    EXPECT_FLOAT_EQ(found->h, region.rect.h) << node << " " << region.name.c_str() << " " << where;
                }
            }

            void ExpectEveryCanvasHoldsTheRegions(const std::string& where) {
                for (const char* node : kRegionNodes) {
                    ExpectHoldsTheRegionsOfTheLayout(node, where);
                }
            }

            TableLayoutSpec _changed;
        };

        TEST_F(MahjongTableRegionsTest, EveryTableCanvasHoldsTheRegionsOfTheLayoutInEveryShapeAndProfile) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            bool phoneSeen = false;
            for (const MatrixWindow& window : Windows()) {
                SetWindow(*_renderer, window);
                Draw();
                phoneSeen = phoneSeen || _table->GetLayout().profile.phone;
                ASSERT_NO_FATAL_FAILURE(ExpectEveryCanvasHoldsTheRegions(window.label.c_str()));
            }
            EXPECT_TRUE(phoneSeen) << "no window drew the phone profile";
        }

        TEST_F(MahjongTableRegionsTest, EveryNodeHoldsTheSameSetAndAnotherViewerTurnsThePlates) {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            Draw();
            ASSERT_NO_FATAL_FAILURE(ExpectEveryCanvasHoldsTheRegions("viewer 0"));
            const auto hud = Canvas("Hud");
            for (const char* node : kRegionNodes) {
                EXPECT_TRUE(hud->GetRegions() == Canvas(node)->GetRegions()) << node;
            }
            const PoolString bottom = PoolString::Intern(kPlateRegionNames[0]);
            const Rect plate = *hud->GetRegions().Find(bottom);
            const TableLayout& layout = _table->GetLayout();
            EXPECT_FLOAT_EQ(plate.x, layout.zones.seatPlates[layout.viewer].x);
        }

        TEST_F(MahjongTableRegionsTest, ASpecOfferedWhileTheTableIsDrawnMovesTheRegionsAtTheNextDraw) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            Draw();
            ASSERT_NO_FATAL_FAILURE(ExpectEveryCanvasHoldsTheRegions("shipped"));
            const Rect before = *Canvas("Hud")->GetRegions().Find(PoolString::Intern(kMoveLineRegionName));

            _changed = ShippedTableLayoutSpec();
            ShapeSpec& shape = _changed.Shape(TableShape::Landscape);
            auto it = std::find_if(shape.freeRegions.begin(), shape.freeRegions.end(), [](const FreeRegion& region) { return region.name == kMoveLineRegionName; });
            ASSERT_NE(it, shape.freeRegions.end());
            it->rect.x += 0.01f;
            SetActiveTableLayoutSpec(&_changed);
            Draw();

            ASSERT_NO_FATAL_FAILURE(ExpectEveryCanvasHoldsTheRegions("moved"));
            const Rect after = *Canvas("Hud")->GetRegions().Find(PoolString::Intern(kMoveLineRegionName));
            EXPECT_GT(after.x, before.x);

            SetActiveTableLayoutSpec(&ShippedTableLayoutSpec());
            Draw();
            ASSERT_NO_FATAL_FAILURE(ExpectEveryCanvasHoldsTheRegions("back"));
            EXPECT_FLOAT_EQ(Canvas("Hud")->GetRegions().Find(PoolString::Intern(kMoveLineRegionName))->x, before.x);
        }

        TEST_F(MahjongTableRegionsTest, ARegionThatLeavesTheSpecIsWithdrawnFromTheCanvases) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            Draw();
            ASSERT_NE(Canvas("Actions")->GetRegions().Find(PoolString::Intern(kHintRegionName)), nullptr);

            _changed = ShippedTableLayoutSpec();
            ShapeSpec& shape = _changed.Shape(TableShape::Landscape);
            shape.freeRegions.erase(std::remove_if(shape.freeRegions.begin(), shape.freeRegions.end(), [](const FreeRegion& region) { return region.name == kHintRegionName; }),
                                    shape.freeRegions.end());
            SetActiveTableLayoutSpec(&_changed);
            Draw();

            ASSERT_NO_FATAL_FAILURE(ExpectEveryCanvasHoldsTheRegions("without table.hint"));
            for (const char* node : kRegionNodes) {
                EXPECT_EQ(Canvas(node)->GetRegions().Find(PoolString::Intern(kHintRegionName)), nullptr) << node;
            }
        }

    }  // namespace

}  // namespace BECore::Mahjong::Tests
