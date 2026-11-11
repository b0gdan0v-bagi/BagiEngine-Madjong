#include <Mahjong/Table/TableSolver.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TableSolver.h`: the largest tile a zone holds for what the rules allow it to hold. Pure logic over rectangles. The sizes of the
 * landscape design are the ones the table had before the zones became data; the rest holds the rule (the largest width that fits, the tile
 * never leaves its zone) against a plain search, and a zone that is too small or no number gives size 0.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kInf = std::numeric_limits<float>::infinity();
        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr int kAllGaps = kMaxMelds - 1;

        /// The widest tile by trial: the rule written out, with no arithmetic to invert.
        int WidestTileByTrial(float along, float across, int tiles, int gaps) {
            int widest = 0;
            for (int width = 1; width <= kSolverMaxTileW; ++width) {
                const bool fitsAlong = static_cast<float>(tiles * width) + static_cast<float>(gaps) * kMeldGap <= along;
                const bool fitsAcross = static_cast<float>(TileHeightOf(width)) <= across;
                if (fitsAlong && fitsAcross) {
                    widest = width;
                }
            }
            return widest;
        }

        void ExpectInside(const Rect& inner, const Rect& zone) {
            EXPECT_GE(inner.x, zone.x);
            EXPECT_GE(inner.y, zone.y);
            EXPECT_LE(inner.x + inner.w, zone.x + zone.w);
            EXPECT_LE(inner.y + inner.h, zone.y + zone.h);
        }

        void ExpectNothing(const SolvedStrip& strip) {
            EXPECT_FALSE(strip.Fits());
            EXPECT_EQ(strip.tileW, 0.0f);
            EXPECT_EQ(strip.tileH, 0.0f);
            EXPECT_EQ(strip.slotW, 0.0f);
            EXPECT_EQ(strip.slotH, 0.0f);
            EXPECT_TRUE(std::isfinite(strip.x) && std::isfinite(strip.y) && std::isfinite(strip.capacity));
        }

        const Rect kBadZones[] = {Rect(0, 0, 0, 0),      Rect(0, 0, -5, 100),   Rect(0, 0, 100, -5),    Rect(0, 0, kNaN, 100),        Rect(0, 0, 100, kNaN),
                                  Rect(0, 0, kInf, 100), Rect(0, 0, 100, kInf), Rect(0, 0, -kInf, 100), Rect(kNaN, kNaN, kNaN, kNaN), Rect(0, 0, 0.5f, 0.5f)};

    }  // namespace

    TEST(TableSolverTest, TheTileHeightIsFourThirdsOfTheWidthRoundedDown) {
        EXPECT_EQ(TileHeightOf(48), 64);
        EXPECT_EQ(TileHeightOf(30), 40);
        EXPECT_EQ(TileHeightOf(32), 42);
        EXPECT_EQ(TileHeightOf(28), 37);
        EXPECT_EQ(TileHeightOf(24), 32);
        EXPECT_EQ(TileHeightOf(16), 21);
        EXPECT_EQ(TileHeightOf(78), 104);
        EXPECT_EQ(TileHeightOf(0), 0);
        EXPECT_EQ(TileHeightOf(-3), 0);
    }

    TEST(TableSolverTest, TheZonesOfTheLandscapeDesignGiveTheTilesOfThatDesign) {
        struct Case {
            const char* name;
            Rect zone;
            bool vertical;
            int tiles;
            int gaps;
            float tileW;
            float tileH;
        };
        const Case cases[] = {
            {"viewer hand", Rect(72, 648, 672, 64), false, kMaxConcealedTiles, 0, 48, 64}, {"viewer melds", Rect(764, 672, 504, 40), false, kMaxMeldTiles, kAllGaps, 30, 40},
            {"viewer bonus", Rect(72, 584, 192, 32), false, kMaxBonusTiles, 0, 24, 32},    {"top hand", Rect(444, 8, 448, 42), false, kMaxConcealedTiles, 0, 32, 42},
            {"side hand", Rect(8, 164, 37, 392), true, kMaxConcealedTiles, 0, 28, 37},     {"opponent melds", Rect(49, 156, 32, 408), true, kMaxMeldTiles, kAllGaps, 24, 32},
            {"opponent bonus", Rect(85, 192, 21, 128), true, kMaxBonusTiles, 0, 16, 21},   {"phone hand", Rect(20, 608, 1240, 104), false, kMaxConcealedTiles, 0, 78, 104},
        };
        for (const Case& test : cases) {
            SCOPED_TRACE(test.name);
            const SolvedStrip strip = SolveStrip(test.zone, ZoneAlign::Start, test.vertical, test.tiles, test.gaps);
            EXPECT_TRUE(strip.Fits());
            EXPECT_EQ(strip.tileW, test.tileW);
            EXPECT_EQ(strip.tileH, test.tileH);
        }
    }

    TEST(TableSolverTest, TheDiscardBlocksAndTheWallOfTheLandscapeDesignGiveTheirTiles) {
        const SolvedBlock bottom = SolveDiscardBlock(Rect(568, 452, 144, 160), false, kDiscardRowCapacity);
        EXPECT_EQ(bottom.tileW, 24.0f);
        EXPECT_EQ(bottom.tileH, 32.0f);
        EXPECT_EQ(bottom.blockW, 144.0f);
        EXPECT_EQ(bottom.blockH, 160.0f);

        const SolvedBlock side = SolveDiscardBlock(Rect(1006, 288, 160, 144), true, kDiscardRowCapacity);
        EXPECT_EQ(side.tileW, 24.0f);
        EXPECT_EQ(side.tileH, 32.0f);
        EXPECT_EQ(side.blockW, 160.0f);
        EXPECT_EQ(side.blockH, 144.0f);

        const SolvedWall wall = SolveWall(Rect(458, 268, 364, 168));
        EXPECT_EQ(wall.stack, 14.0f);
        EXPECT_EQ(wall.w, 364.0f);
        EXPECT_EQ(wall.h, 168.0f);
    }

    TEST(TableSolverTest, ALargerZoneGivesALargerTileAndASmallerOneASmallerTile) {
        const Rect base(0, 0, 672, 64);
        const SolvedStrip same = SolveStrip(base, ZoneAlign::Start, false, kMaxConcealedTiles, 0);
        const SolvedStrip wider = SolveStrip(Rect(0, 0, 840, 80), ZoneAlign::Start, false, kMaxConcealedTiles, 0);
        const SolvedStrip narrower = SolveStrip(Rect(0, 0, 560, 53), ZoneAlign::Start, false, kMaxConcealedTiles, 0);
        EXPECT_GT(wider.tileW, same.tileW);
        EXPECT_LT(narrower.tileW, same.tileW);
        EXPECT_EQ(wider.tileW, 60.0f);
        EXPECT_EQ(narrower.tileW, 40.0f);
        // growing only the length gains nothing when the height is the limit, and the other way round
        EXPECT_EQ(SolveStrip(Rect(0, 0, 2000, 64), ZoneAlign::Start, false, kMaxConcealedTiles, 0).tileW, 48.0f);
        EXPECT_EQ(SolveStrip(Rect(0, 0, 672, 200), ZoneAlign::Start, false, kMaxConcealedTiles, 0).tileW, 48.0f);
    }

    TEST(TableSolverTest, TheWidthIsTheLargestThatFitsAndTheTileNeverLeavesItsZone) {
        for (const bool vertical : {false, true}) {
            for (const int tiles : {1, 8, kMaxConcealedTiles, kMaxMeldTiles}) {
                for (const int gaps : {0, kAllGaps}) {
                    for (float along = 0.0f; along <= 700.0f; along += 6.75f) {
                        for (float across = 0.0f; across <= 120.0f; across += 4.25f) {
                            const Rect zone = vertical ? Rect(10.0f, 20.0f, across, along) : Rect(10.0f, 20.0f, along, across);
                            const SolvedStrip strip = SolveStrip(zone, ZoneAlign::Start, vertical, tiles, gaps);
                            const int widest = WidestTileByTrial(along, across, tiles, gaps);
                            ASSERT_EQ(static_cast<int>(strip.tileW), widest) << "along " << along << " across " << across << " tiles " << tiles << " gaps " << gaps << " vertical " << vertical;
                            if (widest == 0) {
                                ExpectNothing(strip);
                                continue;
                            }
                            // the whole line, first tile to last and the gaps, lies in the zone
                            const float span = static_cast<float>(tiles) * strip.PitchOf() + static_cast<float>(gaps) * kMeldGap;
                            const Rect line = vertical ? Rect(strip.x, strip.y, strip.slotW, span) : Rect(strip.x, strip.y, span, strip.slotH);
                            ExpectInside(line, zone);
                            EXPECT_EQ(strip.capacity, along);
                        }
                    }
                }
            }
        }
    }

    TEST(TableSolverTest, ATurnedStripSwapsTheAxes) {
        const SolvedStrip row = SolveStrip(Rect(0, 0, 392, 37), ZoneAlign::Start, false, kMaxConcealedTiles, 0);
        const SolvedStrip column = SolveStrip(Rect(0, 0, 37, 392), ZoneAlign::Start, true, kMaxConcealedTiles, 0);
        EXPECT_EQ(row.tileW, column.tileW);
        EXPECT_EQ(row.tileH, column.tileH);
        EXPECT_EQ(row.slotW, column.slotH);
        EXPECT_EQ(row.slotH, column.slotW);
        EXPECT_EQ(row.PitchOf(), row.slotW);
        EXPECT_EQ(column.PitchOf(), column.slotH);
        EXPECT_EQ(column.slotW, 37.0f);
        EXPECT_EQ(column.slotH, 28.0f);
    }

    TEST(TableSolverTest, TheAlignmentPutsTheTileAcrossTheLineAndNeverMovesItAlongIt) {
        const Rect zone(100.0f, 200.0f, 400.0f, 100.0f);
        const SolvedStrip start = SolveStrip(zone, ZoneAlign::Start, false, 8, 0);
        const SolvedStrip centre = SolveStrip(zone, ZoneAlign::Center, false, 8, 0);
        const SolvedStrip end = SolveStrip(zone, ZoneAlign::End, false, 8, 0);
        ASSERT_EQ(start.tileW, 50.0f);
        EXPECT_EQ(start.slotH, 66.0f);
        EXPECT_EQ(start.y, 200.0f);
        EXPECT_EQ(centre.y, 217.0f);
        EXPECT_EQ(end.y, 234.0f);
        EXPECT_EQ(start.x, 100.0f);
        EXPECT_EQ(centre.x, 100.0f);
        EXPECT_EQ(end.x, 100.0f);

        // a column: across is the horizontal axis
        const Rect column(100.0f, 200.0f, 100.0f, 400.0f);
        EXPECT_EQ(SolveStrip(column, ZoneAlign::End, true, 8, 0).x, 100.0f + (100.0f - 66.0f));
        EXPECT_EQ(SolveStrip(column, ZoneAlign::End, true, 8, 0).y, 200.0f);
    }

    TEST(TableSolverTest, AStripWithTheTileAskedForRestsWhereTheAlignmentSays) {
        const Rect zone(12.0f, 608.0f, 1256.0f, 104.0f);
        const SolvedStrip end = PlaceStrip(zone, ZoneAlign::End, 60.5f, 80.0f);
        EXPECT_EQ(end.tileW, 60.5f);
        EXPECT_EQ(end.slotW, 60.5f);
        EXPECT_EQ(end.slotH, 80.0f);
        EXPECT_EQ(end.x, 12.0f);
        EXPECT_EQ(end.y + end.slotH, zone.y + zone.h);
        EXPECT_EQ(end.capacity, 1256.0f);
        EXPECT_EQ(PlaceStrip(zone, ZoneAlign::Start, 60.5f, 80.0f).y, 608.0f);
        // a tile higher than the zone rests on its top edge, and a tile that is no size gives nothing
        EXPECT_EQ(PlaceStrip(zone, ZoneAlign::End, 90.0f, 120.0f).y, 608.0f);
        EXPECT_FALSE(PlaceStrip(zone, ZoneAlign::End, kNaN, 80.0f).Fits());
        EXPECT_FALSE(PlaceStrip(zone, ZoneAlign::End, 60.0f, -1.0f).Fits());
        EXPECT_FALSE(PlaceStrip(Rect(0, 0, 0, 104), ZoneAlign::End, 60.0f, 80.0f).Fits());
    }

    TEST(TableSolverTest, AMeldStripKeepsRoomForItsGaps) {
        // 16 tiles of 30 and three gaps need 504; one unit less is a tile of 29
        EXPECT_EQ(SolveStrip(Rect(0, 0, 504, 40), ZoneAlign::Start, false, kMaxMeldTiles, kAllGaps).tileW, 30.0f);
        EXPECT_EQ(SolveStrip(Rect(0, 0, 503, 40), ZoneAlign::Start, false, kMaxMeldTiles, kAllGaps).tileW, 29.0f);
        // and a negative number of gaps asks for none
        EXPECT_EQ(SolveStrip(Rect(0, 0, 672, 64), ZoneAlign::Start, false, kMaxConcealedTiles, -4).tileW, 48.0f);
    }

    TEST(TableSolverTest, ADiscardBlockWithFewerRowsKeepsItsTileInALowerZone) {
        // the phone's lower block: 144 wide, 120 high, three rows: the 24 tile of the full block
        const SolvedBlock phone = SolveDiscardBlock(Rect(568, 484, 144, 120), false, 3);
        EXPECT_EQ(phone.tileW, 24.0f);
        EXPECT_EQ(phone.blockH, 96.0f);
        EXPECT_EQ(phone.rows, 3);
        // the same zone for the default five rows would have to shrink the tile
        const SolvedBlock five = SolveDiscardBlock(Rect(568, 484, 144, 120), false, kDiscardRowCapacity);
        EXPECT_EQ(five.tileW, 18.0f);
        EXPECT_LT(five.tileW, phone.tileW);
    }

    TEST(TableSolverTest, ADiscardBlockIsTheLargestTileWithSixInARowAndTheRowsInTheHeight) {
        for (const bool vertical : {false, true}) {
            for (const int rows : {1, 3, kDiscardRowCapacity, 8}) {
                for (float along = 0.0f; along <= 400.0f; along += 7.5f) {
                    for (float across = 0.0f; across <= 400.0f; across += 11.25f) {
                        const Rect zone = vertical ? Rect(5.0f, 6.0f, across, along) : Rect(5.0f, 6.0f, along, across);
                        const SolvedBlock block = SolveDiscardBlock(zone, vertical, rows);
                        int widest = 0;
                        for (int width = 1; width <= 200; ++width) {
                            if (static_cast<float>(kDiscardsPerRow * width) <= along && static_cast<float>(TileHeightOf(width) * rows) <= across) {
                                widest = width;
                            }
                        }
                        ASSERT_EQ(static_cast<int>(block.tileW), widest) << "along " << along << " across " << across << " rows " << rows << " vertical " << vertical;
                        if (widest > 0) {
                            ExpectInside(Rect(block.x, block.y, block.blockW, block.blockH), zone);
                        } else {
                            EXPECT_EQ(block.blockW, 0.0f);
                            EXPECT_EQ(block.blockH, 0.0f);
                        }
                    }
                }
            }
        }
    }

    TEST(TableSolverTest, TheWallIsTheLargestSquareStackThatMakesTheRingFit) {
        EXPECT_EQ(SolveWall(Rect(0, 0, 364, 168)).stack, 14.0f);
        EXPECT_EQ(SolveWall(Rect(0, 0, 363, 168)).stack, 13.0f);
        EXPECT_EQ(SolveWall(Rect(0, 0, 364, 167)).stack, 13.0f);
        EXPECT_EQ(SolveWall(Rect(0, 0, 1000, 168)).stack, 14.0f);
        const SolvedWall wall = SolveWall(Rect(10, 20, 400, 200));
        EXPECT_EQ(wall.stack, 15.0f);
        EXPECT_EQ(wall.w, 15.0f * kWallRingColumns);
        EXPECT_EQ(wall.h, 15.0f * kWallRingRows);
        ExpectInside(Rect(wall.x, wall.y, wall.w, wall.h), Rect(10, 20, 400, 200));
    }

    TEST(TableSolverTest, AZoneThatIsTooSmallOrNoNumberGivesSizeZero) {
        for (const Rect& zone : kBadZones) {
            SCOPED_TRACE(testing::Message() << "zone " << zone.x << " " << zone.y << " " << zone.w << " x " << zone.h);
            for (const bool vertical : {false, true}) {
                ExpectNothing(SolveStrip(zone, ZoneAlign::Center, vertical, kMaxConcealedTiles, 0));
                ExpectNothing(SolveStrip(zone, ZoneAlign::End, vertical, 0, 0));
                const SolvedBlock block = SolveDiscardBlock(zone, vertical, kDiscardRowCapacity);
                EXPECT_FALSE(block.Fits());
                EXPECT_EQ(block.blockW, 0.0f);
                EXPECT_EQ(block.blockH, 0.0f);
            }
            const SolvedWall wall = SolveWall(zone);
            EXPECT_FALSE(wall.Fits());
            EXPECT_EQ(wall.w, 0.0f);
            EXPECT_EQ(wall.h, 0.0f);
        }
        EXPECT_FALSE(SolveDiscardBlock(Rect(0, 0, 144, 160), false, 0).Fits());
        EXPECT_FALSE(SolveDiscardBlock(Rect(0, 0, 144, 160), false, -2).Fits());
        ExpectNothing(SolveStrip(Rect(0, 0, 672, 64), ZoneAlign::Start, false, -1, 0));
    }

    TEST(TableSolverTest, AnAbsurdlyLargeZoneGivesTheGuardWidthAndNoOverflow) {
        const SolvedStrip huge = SolveStrip(Rect(0, 0, 1e30f, 1e30f), ZoneAlign::Start, false, kMaxConcealedTiles, 0);
        EXPECT_EQ(huge.tileW, static_cast<float>(kSolverMaxTileW));
        EXPECT_TRUE(std::isfinite(huge.tileH));
        const SolvedBlock block = SolveDiscardBlock(Rect(0, 0, 1e30f, 1e30f), false, 1000000);
        EXPECT_EQ(block.tileW, static_cast<float>(kSolverMaxTileW));
        EXPECT_TRUE(std::isfinite(block.blockH));
        EXPECT_EQ(SolveWall(Rect(0, 0, 1e30f, 1e30f)).stack, static_cast<float>(kSolverMaxTileW));
    }

    TEST(TableSolverTest, SnappingRoundsEachEdgeToTheGridSoThatTheNoiseOfAFractionIsGone) {
        // 8 / 720 of the height, written exactly and with seven digits
        const Rect exact = SnapZone(ZoneRect{0.0f, 8.0f / 720.0f, 0.5f, 64.0f / 720.0f}, kViewWidth, kViewHeight);
        const Rect noisy = SnapZone(ZoneRect{0.0f, 0.0111111f, 0.5f, 0.0888889f}, kViewWidth, kViewHeight);
        EXPECT_EQ(exact.y, 8.0f);
        EXPECT_EQ(exact.h, 64.0f);
        EXPECT_EQ(noisy.x, exact.x);
        EXPECT_EQ(noisy.y, exact.y);
        EXPECT_EQ(noisy.w, exact.w);
        EXPECT_EQ(noisy.h, exact.h);
        EXPECT_EQ(exact.w, 640.0f);

        // the edges are snapped, then the size is their difference: two zones that touch still touch
        const Rect left = SnapZone(ZoneRect{0.1f, 0.0f, 0.3333333f, 1.0f}, kViewWidth, kViewHeight);
        const Rect right = SnapZone(ZoneRect{0.1f + 0.3333333f, 0.0f, 0.2f, 1.0f}, kViewWidth, kViewHeight);
        EXPECT_EQ(left.x + left.w, right.x);
        for (const Rect& rect : {left, right, exact, noisy}) {
            EXPECT_EQ(std::fmod(rect.x, kZoneSnapGrid), 0.0f);
            EXPECT_EQ(std::fmod(rect.y, kZoneSnapGrid), 0.0f);
            EXPECT_EQ(std::fmod(rect.w, kZoneSnapGrid), 0.0f);
            EXPECT_EQ(std::fmod(rect.h, kZoneSnapGrid), 0.0f);
        }
    }

    TEST(TableSolverTest, SnappingAZoneThatIsNoZoneGivesAnEmptyRectangle) {
        for (const ZoneRect& zone : {ZoneRect{kNaN, 0, 1, 1}, ZoneRect{0, kInf, 1, 1}, ZoneRect{0, 0, -0.5f, 1}, ZoneRect{0, 0, 1, -0.5f}, ZoneRect{0, 0, kNaN, 1}}) {
            const Rect rect = SnapZone(zone, kViewWidth, kViewHeight);
            EXPECT_EQ(rect.w, 0.0f);
            EXPECT_EQ(rect.h, 0.0f);
        }
        const Rect noDesign = SnapZone(ZoneRect{0, 0, 1, 1}, kNaN, kViewHeight);
        EXPECT_EQ(noDesign.w, 0.0f);
        const Rect overflow = SnapZone(ZoneRect{0, 0, 1e38f, 1}, 1e38f, kViewHeight);
        EXPECT_TRUE(std::isfinite(overflow.w) && std::isfinite(overflow.h));
    }

    TEST(TableSolverTest, TheSolversAreDeterministic) {
        const Rect zone(13.0f, 17.0f, 411.0f, 53.0f);
        const SolvedStrip first = SolveStrip(zone, ZoneAlign::Center, false, kMaxBonusTiles, 0);
        const SolvedStrip second = SolveStrip(zone, ZoneAlign::Center, false, kMaxBonusTiles, 0);
        EXPECT_EQ(first.tileW, second.tileW);
        EXPECT_EQ(first.x, second.x);
        EXPECT_EQ(first.y, second.y);
    }

    TEST(TableSolverTest, AShapeThatIsLikeAnotherUsesItsZones) {
        TableLayoutSpec spec;
        EXPECT_EQ(ZonesOf(spec, TableShape::Landscape), nullptr);
        spec.Shape(TableShape::Landscape).present = true;
        spec.Shape(TableShape::Wide).present = true;
        spec.Shape(TableShape::Wide).like = true;
        spec.Shape(TableShape::Wide).likeShape = TableShape::Landscape;
        EXPECT_EQ(ZonesOf(spec, TableShape::Landscape), &spec.Shape(TableShape::Landscape));
        EXPECT_EQ(ZonesOf(spec, TableShape::Wide), &spec.Shape(TableShape::Landscape));
        EXPECT_EQ(ZonesOf(spec, TableShape::Portrait), nullptr);

        // a `like` of a shape that is missing, or that is a `like` itself, has no zones
        spec.Shape(TableShape::Landscape).present = false;
        EXPECT_EQ(ZonesOf(spec, TableShape::Wide), nullptr);
        spec.Shape(TableShape::Landscape).present = true;
        spec.Shape(TableShape::Landscape).like = true;
        spec.Shape(TableShape::Landscape).likeShape = TableShape::Wide;
        EXPECT_EQ(ZonesOf(spec, TableShape::Wide), nullptr);

        TableLayoutSpec other = spec;
        EXPECT_EQ(spec, other);
        other.Shape(TableShape::Portrait).seats[2].hand.minTileW = 1;
        EXPECT_FALSE(spec == other);
    }

}  // namespace BECore::Mahjong::Tests
