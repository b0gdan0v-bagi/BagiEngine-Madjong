#include "ShippedTableLayout.h"

#include <Mahjong/Table/TableRegions.h>
#include <Mahjong/Table/TableSolver.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TableRegions.h`: the regions the table publishes. The zone regions are the layout's own plates (by screen position), centre panel
 * and wall ring; the free regions are the fractions of the spec fitted like the zones (design view, quarter-unit snap, uniform scale,
 * centred, Wide's wing). Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kTolerance = 0.001f;

        struct ViewSize {
            float width;
            float height;
        };

        /// The designs of the four shapes and fitted views of each (a larger one, a smaller one, a phone's).
        const ViewSize kViews[] = {{kViewWidth, kViewHeight},
                                   {1920.0f, 1080.0f},
                                   {640.0f, 360.0f},
                                   {kWideViewWidth, kViewHeight},
                                   {2560.0f, 1080.0f},
                                   {915.0f, 330.0f},
                                   {kNarrowViewWidth, kNarrowViewHeight},
                                   {800.0f, 600.0f},
                                   {kPortraitViewWidth, kPortraitViewHeight},
                                   {390.0f, 844.0f}};

        TableInput InputOf(Seat viewer) {
            TableInput input;
            input.viewer = viewer;
            input.concealedCount.fill(13);
            input.wallRemaining = 70;
            return input;
        }

        const TableRegion* Find(const eastl::vector<TableRegion>& regions, const char* name) {
            for (const TableRegion& region : regions) {
                if (region.name == name) {
                    return &region;
                }
            }
            return nullptr;
        }

        void ExpectSameRect(const Rect& actual, const Rect& expected, const std::string& where) {
            EXPECT_NEAR(actual.x, expected.x, kTolerance) << where;
            EXPECT_NEAR(actual.y, expected.y, kTolerance) << where;
            EXPECT_NEAR(actual.w, expected.w, kTolerance) << where;
            EXPECT_NEAR(actual.h, expected.h, kTolerance) << where;
        }

        std::string Where(const ViewSize& view, Seat viewer) {
            return std::to_string(view.width) + "x" + std::to_string(view.height) + " viewer " + std::to_string(viewer);
        }

        /// The fraction `zone` of the shape the spec borrows from, fitted into a view of `view` by hand: the rule of the layout written out.
        Rect FittedByHand(const TableLayoutSpec& spec, const ZoneRect& zone, const ViewSize& view) {
            const TableShape shape = ShapeOfView(view.width, view.height);
            const ShapeSpec& own = spec.Shape(shape);
            const TableShape zoneShape = own.like ? own.likeShape : shape;
            const Rect snapped = SnapZone(zone, DesignWidthOf(zoneShape), DesignHeightOf(zoneShape));
            const float wing = SnapZone(ZoneRect{own.inset, 0.0f, 0.0f, 0.0f}, DesignWidthOf(shape), DesignHeightOf(shape)).x;
            const float scale = LayoutScale(view.width, view.height);
            const float offsetX = (view.width - DesignWidthOf(shape) * scale) / 2.0f + wing * scale;
            const float offsetY = (view.height - DesignHeightOf(shape) * scale) / 2.0f;
            return Rect(offsetX + snapped.x * scale, offsetY + snapped.y * scale, snapped.w * scale, snapped.h * scale);
        }

    }  // namespace

    TEST(TableRegionsTest, TheZoneRegionsArePlatesByScreenPositionTheCentrePanelAndTheWall) {
        const TableLayoutSpec spec = ShippedSpecWithTestRegions();
        for (const ViewSize& view : kViews) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(InputOf(viewer), spec, view.width, view.height);
                const eastl::vector<TableRegion> regions = MakeTableRegions(layout);
                const std::string where = Where(view, viewer);
                ASSERT_GE(regions.size(), static_cast<size_t>(kSeatCount) + 2) << where;
                for (int position = 0; position < kSeatCount; ++position) {
                    EXPECT_STREQ(regions[static_cast<size_t>(position)].name.c_str(), ("table.plate." + std::to_string(position)).c_str()) << where;
                    ExpectSameRect(regions[static_cast<size_t>(position)].rect, layout.zones.seatPlates[SeatAtPosition(viewer, position)], where + " plate " + std::to_string(position));
                }
                EXPECT_STREQ(regions[kSeatCount].name.c_str(), "table.centerPanel");
                ExpectSameRect(regions[kSeatCount].rect, layout.zones.centerPanel, where + " centre panel");
                EXPECT_STREQ(regions[kSeatCount + 1].name.c_str(), "table.wall");
                ExpectSameRect(regions[kSeatCount + 1].rect, layout.zones.wall, where + " wall");
            }
        }
    }

    TEST(TableRegionsTest, ThePlateOfPositionZeroIsTheViewersOwnAndThePhoneProfileLiftsIt) {
        const TableLayoutSpec spec = ShippedTableLayoutSpec();
        TableInput desktop = InputOf(2);
        TableInput phone = desktop;
        phone.profile = TableProfile{true, 60.0f};
        const TableLayout desktopLayout = MakeTableLayout(desktop, spec);
        const TableLayout phoneLayout = MakeTableLayout(phone, spec);
        const eastl::vector<TableRegion> desktopRegions = MakeTableRegions(desktopLayout);
        const eastl::vector<TableRegion> phoneRegions = MakeTableRegions(phoneLayout);
        ASSERT_TRUE(phoneLayout.profile.phone);
        EXPECT_LT(phoneRegions[0].rect.y, desktopRegions[0].rect.y);
        ExpectSameRect(phoneRegions[0].rect, phoneLayout.zones.seatPlates[2], "phone plate");
        ExpectSameRect(phoneRegions[1].rect, desktopRegions[1].rect, "the other plates stay");
    }

    TEST(TableRegionsTest, ASpecWithoutFreeRegionsGivesTheZoneRegionsOnly) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        for (const TableShape shape : EnumUtils<TableShape>::Values()) {
            spec.Shape(shape).freeRegions.clear();
        }
        for (const ViewSize& view : kViews) {
            EXPECT_EQ(MakeTableRegions(MakeTableLayout(InputOf(0), spec, view.width, view.height)).size(), static_cast<size_t>(kSeatCount) + 2) << Where(view, 0);
        }
    }

    TEST(TableRegionsTest, TheFreeRegionsAreTheSpecsFractionsFittedLikeTheZonesInOrder) {
        const TableLayoutSpec spec = ShippedSpecWithTestRegions();
        for (const ViewSize& view : kViews) {
            const TableShape shape = ShapeOfView(view.width, view.height);
            const ShapeSpec* zones = ZonesOf(spec, shape);
            ASSERT_NE(zones, nullptr);
            ASSERT_GE(zones->freeRegions.size(), 2U) << Where(view, 0);
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const eastl::vector<TableRegion> regions = MakeTableRegions(MakeTableLayout(InputOf(viewer), spec, view.width, view.height));
                ASSERT_EQ(regions.size(), static_cast<size_t>(kSeatCount) + 2 + zones->freeRegions.size()) << Where(view, viewer);
                const Rect viewRect(0.0f, 0.0f, view.width, view.height);
                for (size_t index = 0; index < zones->freeRegions.size(); ++index) {
                    const FreeRegion& region = zones->freeRegions[index];
                    const TableRegion& made = regions[kSeatCount + 2 + index];
                    EXPECT_STREQ(made.name.c_str(), region.name.c_str());
                    ExpectSameRect(made.rect, FittedByHand(spec, region.rect, view), Where(view, viewer) + " " + region.name.c_str());
                    EXPECT_GE(made.rect.x, viewRect.x - kTolerance) << region.name.c_str();
                    EXPECT_GE(made.rect.y, viewRect.y - kTolerance) << region.name.c_str();
                    EXPECT_LE(made.rect.x + made.rect.w, viewRect.w + kTolerance) << region.name.c_str();
                    EXPECT_LE(made.rect.y + made.rect.h, viewRect.h + kTolerance) << region.name.c_str();
                }
            }
        }
    }

    TEST(TableRegionsTest, TheFreeRegionsOfTheViewDoNotDependOnTheViewer) {
        const TableLayoutSpec spec = ShippedSpecWithTestRegions();
        for (const ViewSize& view : kViews) {
            const eastl::vector<TableRegion> first = MakeTableRegions(MakeTableLayout(InputOf(0), spec, view.width, view.height));
            for (Seat viewer = 1; viewer < kSeatCount; ++viewer) {
                const eastl::vector<TableRegion> other = MakeTableRegions(MakeTableLayout(InputOf(viewer), spec, view.width, view.height));
                ASSERT_EQ(first.size(), other.size());
                for (size_t index = kSeatCount; index < first.size(); ++index) {
                    EXPECT_EQ(first[index].name, other[index].name);
                    ExpectSameRect(first[index].rect, other[index].rect, Where(view, viewer));
                }
            }
        }
    }

    TEST(TableRegionsTest, TheWideRegionsAreTheLandscapeOnesShiftedByTheWing) {
        const TableLayoutSpec spec = ShippedSpecWithTestRegions();
        const eastl::vector<TableRegion> landscape = MakeTableRegions(MakeTableLayout(InputOf(1), spec, kViewWidth, kViewHeight));
        const eastl::vector<TableRegion> wide = MakeTableRegions(MakeTableLayout(InputOf(1), spec, kWideViewWidth, kViewHeight));
        ASSERT_EQ(landscape.size(), wide.size());
        ASSERT_GT(landscape.size(), static_cast<size_t>(kSeatCount) + 2);
        for (size_t index = 0; index < landscape.size(); ++index) {
            EXPECT_EQ(landscape[index].name, wide[index].name);
            ExpectSameRect(wide[index].rect, Rect(landscape[index].rect.x + kWideWingWidth, landscape[index].rect.y, landscape[index].rect.w, landscape[index].rect.h), landscape[index].name.c_str());
        }
    }

    TEST(TableRegionsTest, AZoneNameIsRecognisedAndAFreeOneIsNot) {
        for (const char* name : {"table.plate.0", "table.plate.3", "table.centerPanel", "table.wall"}) {
            EXPECT_TRUE(IsZoneRegionName(name)) << name;
        }
        for (const char* name : {"table.plate", "table.plate.4", "table.hint", ""}) {
            EXPECT_FALSE(IsZoneRegionName(name)) << name;
        }
    }

    TEST(TableRegionsTest, EveryShapeOfTheShippedFileHasEveryNamedFreeRegionOnceAndNoZoneName) {
        const TableLayoutSpec& spec = ShippedTableLayoutSpec();
        for (const TableShape shape : EnumUtils<TableShape>::Values()) {
            const ShapeSpec* zones = ZonesOf(spec, shape);
            ASSERT_NE(zones, nullptr);
            for (const char* name : kFreeRegionNames) {
                int found = 0;
                for (const FreeRegion& region : zones->freeRegions) {
                    found += region.name == name ? 1 : 0;
                }
                EXPECT_EQ(found, 1) << name << " in " << EnumUtils<TableShape>::ToString(shape).data();
            }
            for (const FreeRegion& region : zones->freeRegions) {
                EXPECT_FALSE(IsZoneRegionName(region.name.c_str())) << region.name.c_str();
            }
        }
    }

    TEST(TableRegionsTest, TheShippedFreeRegionsAreFittedIntoEveryViewOfEveryShape) {
        for (const ViewSize& view : kViews) {
            const eastl::vector<TableRegion> regions = MakeTableRegions(MakeTableLayout(InputOf(0), view.width, view.height));
            for (const char* name : kFreeRegionNames) {
                const TableRegion* found = Find(regions, name);
                ASSERT_NE(found, nullptr) << name << " in " << Where(view, 0);
                EXPECT_GT(found->rect.w, 0.0f) << name;
                EXPECT_GT(found->rect.h, 0.0f) << name;
                EXPECT_GE(found->rect.x, -kTolerance) << name;
                EXPECT_LE(found->rect.x + found->rect.w, view.width + kTolerance) << name << " in " << Where(view, 0);
            }
        }
    }

    TEST(TableRegionsTest, TheTestRegionsPassTheCheckOfTheSpec) {
        for (const eastl::string& problem : CheckTableLayoutSpec(ShippedSpecWithTestRegions())) {
            ADD_FAILURE() << problem.c_str();
        }
    }

}  // namespace BECore::Mahjong::Tests
