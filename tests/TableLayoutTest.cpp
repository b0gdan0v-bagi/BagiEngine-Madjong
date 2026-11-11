#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TableLayoutSpec.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/UiPalette.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TableLayout.h`: the pure layout of the table in view units. Where each seat sits for each viewer, the viewer's hand in one row,
 * the worst cases (14 concealed tiles, four kongs, 8 bonus tiles, a pile of 100 discards, the 144-tile wall) inside the view and no zone
 * over another, the discard rows, the wall, the hit test, other view sizes. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kTolerance = 0.001f;
        /// The most discards the plan asks the layout to survive, all by one seat, and spread over the seats.
        constexpr int kWorstPile = 100;
        constexpr int kWorstWall = 144;
        constexpr int kAllSlotRoles = 5;

        struct ViewSize {
            float width;
            float height;
        };
        /// Landscape, wide, narrow (4:3, 3:2, 5:4) and portrait views: the designs, fitted ones, the wide boundary (2000 x 1000), a square (portrait) and a phone's 390 x 844.
        const ViewSize kViewSizes[] = {{kViewWidth, kViewHeight}, {1920.0f, 1080.0f},
                                       {800.0f, 800.0f},          {640.0f, 360.0f},
                                       {1000.0f, 400.0f},         {kWideViewWidth, kViewHeight},
                                       {2560.0f, 1080.0f},        {3200.0f, 900.0f},
                                       {2000.0f, 1000.0f},        {kPortraitViewWidth, kPortraitViewHeight},
                                       {390.0f, 844.0f},          {360.0f, 640.0f},
                                       {768.0f, 1024.0f},         {480.0f, 480.0f}};
        /// The designs of the shapes, the views the layout is made in unscaled.
        const ViewSize kDesigns[] = {{kViewWidth, kViewHeight}, {kWideViewWidth, kViewHeight}, {kNarrowViewWidth, kNarrowViewHeight}, {kPortraitViewWidth, kPortraitViewHeight}};
        const SlotRole kSeatRoles[] = {SlotRole::HandTile, SlotRole::MeldTile, SlotRole::BonusTile, SlotRole::DiscardTile};

        Meld MakeMeld(MeldKind kind) {
            Meld meld;
            meld.kind = kind;
            return meld;
        }

        /// Every zone at its limit for every seat; the pile is `pile` discards by `discardSeat` (or spread over the seats when kNoSeat).
        TableInput WorstInput(Seat viewer, int pile, Seat discardSeat) {
            TableInput input;
            input.viewer = viewer;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                input.concealedCount[seat] = kMaxConcealedTiles;
                input.bonusCount[seat] = kMaxBonusTiles;
                for (int meld = 0; meld < kMaxMelds; ++meld) {
                    input.melds[seat].push_back(MakeMeld(MeldKind::Kong));
                }
            }
            for (int entry = 0; entry < pile; ++entry) {
                input.discardSeats.push_back(discardSeat == kNoSeat ? entry % kSeatCount : discardSeat);
            }
            input.wallRemaining = kWorstWall;
            return input;
        }

        TableInput QuietInput(Seat viewer) {
            TableInput input;
            input.viewer = viewer;
            input.concealedCount = {14, 13, 13, 13};
            return input;
        }

        /// A hand of 14 with melds and bonus tiles beside it, for every seat.
        TableInput BusyInput(Seat viewer) {
            TableInput input = QuietInput(viewer);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                input.concealedCount[seat] = 14;
                input.melds[seat] = {MakeMeld(MeldKind::Pung), MakeMeld(MeldKind::Kong)};
                input.bonusCount[seat] = 3;
            }
            input.wallRemaining = 60;
            return input;
        }

        std::vector<TileSlot> SlotsOf(const TableLayout& layout, SlotRole role, Seat seat) {
            std::vector<TileSlot> found;
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == role && slot.seat == seat) {
                    found.push_back(slot);
                }
            }
            return found;
        }

        bool Overlaps(const Rect& a, const Rect& b) {
            return a.x < b.x + b.w - kTolerance && b.x < a.x + a.w - kTolerance && a.y < b.y + b.h - kTolerance && b.y < a.y + a.h - kTolerance;
        }

        bool Inside(const Rect& rect, float width, float height) {
            return rect.x >= -kTolerance && rect.y >= -kTolerance && rect.x + rect.w <= width + kTolerance && rect.y + rect.h <= height + kTolerance;
        }

        void ExpectSlotsInsideTheView(const TableLayout& layout) {
            for (const TileSlot& slot : layout.slots) {
                EXPECT_GT(slot.rect.w, 0.0f);
                EXPECT_GT(slot.rect.h, 0.0f);
                EXPECT_TRUE(Inside(slot.rect, layout.viewWidth, layout.viewHeight))
                    << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index << " at " << slot.rect.x << "," << slot.rect.y;
            }
        }

        /// The zones are the groups of slots of one role and seat (the wall is one); none may overlap another.
        void ExpectNoZoneOverlaps(const TableLayout& layout) {
            std::vector<std::pair<std::string, Rect>> zones;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (const SlotRole role : kSeatRoles) {
                    const Rect bounds = BoundsOf(layout, role, seat);
                    if (!bounds.IsEmpty()) {
                        zones.emplace_back("role " + std::to_string(static_cast<int>(role)) + " of seat " + std::to_string(seat), bounds);
                    }
                }
            }
            const Rect wall = BoundsOf(layout, SlotRole::WallStack, 0);
            if (!wall.IsEmpty()) {
                zones.emplace_back("wall", wall);
            }
            for (size_t first = 0; first < zones.size(); ++first) {
                for (size_t second = first + 1; second < zones.size(); ++second) {
                    EXPECT_FALSE(Overlaps(zones[first].second, zones[second].second)) << zones[first].first << " overlaps " << zones[second].first;
                }
            }
        }

        bool Holds(const Rect& outer, const Rect& inner) {
            return inner.x >= outer.x - kTolerance && inner.y >= outer.y - kTolerance && inner.x + inner.w <= outer.x + outer.w + kTolerance && inner.y + inner.h <= outer.y + outer.h + kTolerance;
        }

        /// The five zones of the plates: the discard block of each seat, then the wall ring.
        std::vector<Rect> PlateZones(const TableLayout& layout) {
            std::vector<Rect> zones(layout.zones.discards.begin(), layout.zones.discards.end());
            zones.push_back(layout.zones.wall);
            return zones;
        }

        void ExpectZonesAreSound(const TableLayout& layout) {
            const std::vector<Rect> zones = PlateZones(layout);
            for (size_t first = 0; first < zones.size(); ++first) {
                EXPECT_GT(zones[first].w, 0.0f) << "zone " << first;
                EXPECT_GT(zones[first].h, 0.0f) << "zone " << first;
                EXPECT_TRUE(Inside(zones[first], layout.viewWidth, layout.viewHeight)) << "zone " << first;
                for (size_t second = first + 1; second < zones.size(); ++second) {
                    EXPECT_FALSE(Overlaps(zones[first], zones[second])) << "zone " << first << " overlaps zone " << second;
                }
            }
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::DiscardTile) {
                    EXPECT_TRUE(Holds(layout.zones.discards[slot.seat], slot.rect)) << "discard " << slot.index << " of seat " << slot.seat;
                } else if (slot.role == SlotRole::WallStack) {
                    EXPECT_TRUE(Holds(layout.zones.wall, slot.rect)) << "wall stack " << slot.index;
                } else {
                    for (size_t zone = 0; zone < zones.size(); ++zone) {
                        EXPECT_FALSE(Overlaps(zones[zone], slot.rect)) << "role " << static_cast<int>(slot.role) << " of seat " << slot.seat << " over zone " << zone;
                    }
                }
            }
        }

        /// Whether `seat` sits at the left or the right of the screen for `viewer`: its tiles are turned.
        bool IsSideSeat(Seat viewer, Seat seat) {
            const int position = SeatPosition(viewer, seat);
            return position == 1 || position == 3;
        }

        void ExpectSameSlots(const TableLayout& a, const TableLayout& b) {
            ASSERT_EQ(a.slots.size(), b.slots.size());
            for (size_t i = 0; i < a.slots.size(); ++i) {
                EXPECT_EQ(a.slots[i].rect.x, b.slots[i].rect.x);
                EXPECT_EQ(a.slots[i].rect.y, b.slots[i].rect.y);
                EXPECT_EQ(a.slots[i].rect.w, b.slots[i].rect.w);
                EXPECT_EQ(a.slots[i].rect.h, b.slots[i].rect.h);
                EXPECT_EQ(a.slots[i].role, b.slots[i].role);
                EXPECT_EQ(a.slots[i].seat, b.slots[i].seat);
                EXPECT_EQ(a.slots[i].index, b.slots[i].index);
                EXPECT_EQ(a.slots[i].claimEdge, b.slots[i].claimEdge);
                EXPECT_EQ(a.slots[i].part, b.slots[i].part);
                EXPECT_EQ(a.slots[i].rotation, b.slots[i].rotation);
            }
        }

        /// The clear space between two rects (0 when they overlap or touch): the larger of the gaps along the two axes.
        float ClearSpace(const Rect& a, const Rect& b) {
            const float gapX = eastl::max(a.x - (b.x + b.w), b.x - (a.x + a.w));
            const float gapY = eastl::max(a.y - (b.y + b.h), b.y - (a.y + a.h));
            return eastl::max(0.0f, eastl::max(gapX, gapY));
        }

        Rect GrownBy(const Rect& rect, float margin) {
            return Rect(rect.x - margin, rect.y - margin, rect.w + 2.0f * margin, rect.h + 2.0f * margin);
        }

        /// The four seat plates of `layout` are of one size, inside the view, hold no slot, keep `kPlatePadding` clear of the zones
        /// (the zones grown by it) and of each other; `scale` is the factor of the view over the design.
        void ExpectSeatPlatesAreSound(const TableLayout& layout, float scale) {
            const float unit = static_cast<float>(kGridUnit) * scale;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const Rect& plate = layout.zones.seatPlates[seat];
                EXPECT_NEAR(plate.w, kSeatPlateW * scale, kTolerance) << "seat " << seat;
                EXPECT_NEAR(plate.h, kSeatPlateH * scale, kTolerance) << "seat " << seat;
                EXPECT_TRUE(Inside(plate, layout.viewWidth, layout.viewHeight)) << "seat " << seat;
                for (const TileSlot& slot : layout.slots) {
                    EXPECT_FALSE(Overlaps(plate, slot.rect)) << "plate of seat " << seat << " over role " << static_cast<int>(slot.role) << " of seat " << slot.seat << " index " << slot.index;
                }
                for (const Rect& zone : PlateZones(layout)) {
                    EXPECT_GE(ClearSpace(plate, GrownBy(zone, kPlatePadding * scale)), unit - kTolerance) << "plate of seat " << seat;
                }
                for (Seat other = seat + 1; other < kSeatCount; ++other) {
                    EXPECT_GE(ClearSpace(plate, layout.zones.seatPlates[other]), unit - kTolerance) << "plates of seats " << seat << " and " << other;
                }
            }
        }

        /// Everything a table of any input and any view size must keep: every slot inside the view, no zone over another, each zone
        /// holds its slots and no hand, meld or bonus slot lies over a zone, the plates clear of everything.
        void ExpectTableSound(const TableLayout& layout) {
            ExpectSlotsInsideTheView(layout);
            ExpectNoZoneOverlaps(layout);
            ExpectZonesAreSound(layout);
            ExpectSeatPlatesAreSound(layout, LayoutScale(layout.viewWidth, layout.viewHeight));
        }

        /// A tile of the layout as designed, upright: what the zone's solver chose, whatever turn the seat's slots have.
        struct TileSize {
            float w = 0.0f;
            float h = 0.0f;
        };

        /// The upright size of the first slot of `role` of `seat`; zero when the layout has none. A tile does not depend on how many lie
        /// in its zone, so the first slot of any layout that has one says it.
        TileSize TileSizeOf(const TableLayout& layout, SlotRole role, Seat seat) {
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == role && slot.seat == seat) {
                    const Rect upright = UprightRect(slot.rect, slot.rotation);
                    return TileSize{upright.w, upright.h};
                }
            }
            return TileSize{};
        }

        /// The size of the tiles of `role` at screen `position` of the landscape table of the active spec (`position` 0 is the viewer's).
        TileSize TileSizeAt(SlotRole role, int position) {
            const TableLayout layout = MakeTableLayout(WorstInput(0, kSeatCount, kNoSeat));
            return TileSizeOf(layout, role, SeatAtPosition(0, position));
        }

        /// The smallest tile width the active spec lets the zone of `role` of `seat` solve to (the discards and the wall have none).
        int MinTileWOf(const TableLayout& layout, SlotRole role, Seat seat) {
            const TableLayoutSpec* spec = ActiveTableLayoutSpec();
            const ShapeSpec* zones = spec != nullptr ? ZonesOf(*spec, layout.shape) : nullptr;
            if (zones == nullptr) {
                return 0;
            }
            const SeatSpec& seatSpec = zones->seats[SeatPosition(layout.viewer, seat)];
            switch (role) {
                case SlotRole::HandTile:
                    return seatSpec.hand.minTileW;
                case SlotRole::MeldTile:
                    return seatSpec.melds.minTileW;
                case SlotRole::BonusTile:
                    return seatSpec.bonus.minTileW;
                default:
                    return 0;
            }
        }

        /// A tile has the proportions of an atlas cell, 3 : 4, to the whole unit (the height is the width times 4/3, rounded down).
        void ExpectAtlasProportions(const TileSize& tile) {
            EXPECT_GT(tile.w, 0.0f);
            EXPECT_LE(tile.h, tile.w * 4.0f / 3.0f + kTolerance);
            EXPECT_GT(tile.h, tile.w * 4.0f / 3.0f - 1.0f - kTolerance);
        }

        /// Every slot of `role` of `seat` is one tile size (a square for a wall stack, else the proportions of an atlas cell) and the zone's
        /// minimum is kept; returns that size.
        TileSize ExpectOneTileSize(const TableLayout& layout, SlotRole role, Seat seat) {
            const TileSize first = TileSizeOf(layout, role, seat);
            if (role == SlotRole::WallStack) {
                EXPECT_GT(first.w, 0.0f);
                EXPECT_EQ(first.w, first.h) << "a wall stack is a square";
            } else {
                ExpectAtlasProportions(first);
            }
            EXPECT_GE(first.w, static_cast<float>(MinTileWOf(layout, role, seat))) << "role " << static_cast<int>(role) << " seat " << seat;
            for (const TileSlot& slot : SlotsOf(layout, role, seat)) {
                const Rect upright = UprightRect(slot.rect, slot.rotation);
                EXPECT_EQ(upright.w, first.w) << "role " << static_cast<int>(role) << " seat " << seat << " index " << slot.index;
                EXPECT_EQ(upright.h, first.h) << "role " << static_cast<int>(role) << " seat " << seat << " index " << slot.index;
            }
            return first;
        }

    }  // namespace

    TEST(TableLayoutTest, SeatsSitCounterClockwiseFromTheBottom) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            EXPECT_EQ(SeatPosition(viewer, viewer), 0);
            EXPECT_EQ(SeatPosition(viewer, NextSeat(viewer)), 1) << "viewer " << viewer;
            EXPECT_EQ(SeatPosition(viewer, (viewer + 2) % kSeatCount), 2);
            EXPECT_EQ(SeatPosition(viewer, (viewer + 3) % kSeatCount), 3);
            for (int position = 0; position < kSeatCount; ++position) {
                EXPECT_EQ(SeatPosition(viewer, SeatAtPosition(viewer, position)), position);
            }
        }
        // South sits to East's right when East is at the bottom, West opposite, North on the left.
        EXPECT_EQ(SeatAtPosition(0, 1), 1);
        EXPECT_EQ(SeatAtPosition(0, 2), 2);
        EXPECT_EQ(SeatAtPosition(0, 3), 3);
        EXPECT_EQ(SeatAtPosition(2, 1), 3);
    }

    TEST(TableLayoutTest, EachSeatIsInItsZoneOfTheView) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = MakeTableLayout(QuietInput(viewer));
            const Rect bottom = BoundsOf(layout, SlotRole::HandTile, viewer);
            const Rect right = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 1));
            const Rect top = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 2));
            const Rect left = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 3));
            EXPECT_GT(bottom.y, kViewHeight * 0.8f) << "viewer " << viewer;
            EXPECT_LT(top.y + top.h, kViewHeight * 0.2f);
            EXPECT_GT(right.x, kViewWidth * 0.9f);
            EXPECT_LT(left.x + left.w, kViewWidth * 0.1f);
            // the side hands are columns of turned tiles, the others rows
            EXPECT_GT(right.h, right.w);
            EXPECT_GT(left.h, left.w);
            EXPECT_GT(top.w, top.h);
            EXPECT_GT(bottom.w, bottom.h);
        }
    }

    TEST(TableLayoutTest, TheViewersHandIsOneRowInInputOrderAndLarge) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const int count : {1, 13, 14}) {
                TableInput input = QuietInput(viewer);
                input.concealedCount[viewer] = count;
                const TableLayout layout = MakeTableLayout(input);
                const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, viewer);
                ASSERT_EQ(static_cast<int>(hand.size()), count);
                const TileSize tile = ExpectOneTileSize(layout, SlotRole::HandTile, viewer);
                EXPECT_GE(tile.w, kViewerHandTileW) << "large enough to touch";
                EXPECT_GE(tile.h, kViewerHandTileH);
                for (int i = 0; i < count; ++i) {
                    EXPECT_EQ(hand[i].index, i);
                    EXPECT_EQ(hand[i].rect.y, hand[0].rect.y);
                    if (i > 0) {
                        const float gap = hand[i].rect.x - (hand[i - 1].rect.x + hand[i - 1].rect.w);
                        EXPECT_GE(gap, -kTolerance) << "tiles overlap";
                        EXPECT_LE(gap, tile.w) << "a gap wider than a tile";
                    }
                }
            }
        }
    }

    TEST(TableLayoutTest, TheOpponentsHandsAreSmallerThanTheViewersAndAsManyAsTheirConcealedTiles) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableInput input = QuietInput(viewer);
            const TableLayout layout = MakeTableLayout(input);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, seat);
                EXPECT_EQ(static_cast<int>(hand.size()), input.concealedCount[seat]);
                const TileSize tile = ExpectOneTileSize(layout, SlotRole::HandTile, seat);
                if (seat != viewer) {
                    const TileSize own = ExpectOneTileSize(layout, SlotRole::HandTile, viewer);
                    EXPECT_LT(tile.w, own.w) << "an opponent's hand is smaller than the viewer's";
                }
                for (const TileSlot& slot : hand) {
                    // a side seat's tile is turned: the screen rectangle is the tile on its side
                    EXPECT_EQ(slot.rect.w, IsSideSeat(viewer, seat) ? tile.h : tile.w);
                    EXPECT_EQ(slot.rect.h, IsSideSeat(viewer, seat) ? tile.w : tile.h);
                }
                for (size_t i = 1; i < hand.size(); ++i) {
                    EXPECT_FALSE(Overlaps(hand[i - 1].rect, hand[i].rect));
                }
            }
        }
    }

    TEST(TableLayoutTest, TheWorstTableFitsTheViewAndNoZoneOverlapsAnother) {
        for (const ViewSize& design : kDesigns) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                for (const Seat discardSeat : {kNoSeat, 0, 1, 2, 3}) {
                    const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, discardSeat), design.width, design.height);
                    ASSERT_NO_FATAL_FAILURE(ExpectTableSound(layout)) << "view " << design.width << "x" << design.height << " viewer " << viewer;
                }
            }
        }
    }

    TEST(TableLayoutTest, TheWorstTableHasTheSlotsItAsksFor) {
        for (const ViewSize& design : kDesigns) {
            const TableLayout layout = MakeTableLayout(WorstInput(1, kWorstPile, kNoSeat), design.width, design.height);
            const int perSeat = kMaxConcealedTiles + kMaxMeldTiles + kMaxBonusTiles + kWorstPile / kSeatCount;
            EXPECT_EQ(static_cast<int>(layout.slots.size()), kSeatCount * perSeat + kWallRingCells);
            int roles[kAllSlotRoles] = {};
            for (const TileSlot& slot : layout.slots) {
                ++roles[static_cast<int>(slot.role)];
            }
            EXPECT_EQ(roles[static_cast<int>(SlotRole::HandTile)], kSeatCount * kMaxConcealedTiles);
            EXPECT_EQ(roles[static_cast<int>(SlotRole::MeldTile)], kSeatCount * kMaxMeldTiles);
            EXPECT_EQ(roles[static_cast<int>(SlotRole::BonusTile)], kSeatCount * kMaxBonusTiles);
            EXPECT_EQ(roles[static_cast<int>(SlotRole::DiscardTile)], kWorstPile);
            EXPECT_EQ(roles[static_cast<int>(SlotRole::WallStack)], kWallRingCells);
        }
    }

    TEST(TableLayoutTest, TooManyTilesAreSqueezedInsteadOfLeavingTheirZone) {
        TableInput input = WorstInput(0, kWorstPile, 2);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            input.concealedCount[seat] = 40;
            input.bonusCount[seat] = 20;
            for (int meld = 0; meld < 3; ++meld) {
                input.melds[seat].push_back(MakeMeld(MeldKind::Pung));
            }
        }
        input.wallRemaining = 1000;
        for (const ViewSize& design : kDesigns) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                input.viewer = viewer;
                const TableLayout layout = MakeTableLayout(input, design.width, design.height);
                ASSERT_NO_FATAL_FAILURE(ExpectTableSound(layout)) << "view " << design.width << "x" << design.height << " viewer " << viewer;
                EXPECT_EQ(static_cast<int>(SlotsOf(layout, SlotRole::WallStack, 0).size()), kWallRingCells);
            }
        }
    }

    TEST(TableLayoutTest, EverySlotIsInsideOtherViewSizesToo) {
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, kNoSeat), size.width, size.height);
                EXPECT_EQ(layout.viewWidth, size.width);
                EXPECT_EQ(layout.viewHeight, size.height);
                ASSERT_NO_FATAL_FAILURE(ExpectTableSound(layout)) << "view " << size.width << "x" << size.height << " viewer " << viewer;
            }
        }
    }

    TEST(TableLayoutTest, TheZonesHoldTheirSlotsAndOverlapNothingElse) {
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                for (const Seat discardSeat : {kNoSeat, 0, 1, 2, 3}) {
                    ASSERT_NO_FATAL_FAILURE(ExpectTableSound(MakeTableLayout(WorstInput(viewer, kWorstPile, discardSeat), size.width, size.height)))
                        << "view " << size.width << "x" << size.height << " viewer " << viewer;
                }
                // The zones are there whatever the table holds
                ASSERT_NO_FATAL_FAILURE(ExpectTableSound(MakeTableLayout(QuietInput(viewer), size.width, size.height)));
            }
        }
    }

    TEST(TableLayoutTest, TheDiscardZoneOfASeatFollowsItsPositionAndTheViewerThatIsNoSeatIsSeatZero) {
        const TableLayout seat0 = MakeTableLayout(QuietInput(0));
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = MakeTableLayout(QuietInput(viewer));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const Rect& expected = seat0.zones.discards[SeatAtPosition(0, SeatPosition(viewer, seat))];
                EXPECT_EQ(layout.zones.discards[seat].x, expected.x) << "viewer " << viewer << " seat " << seat;
                EXPECT_EQ(layout.zones.discards[seat].y, expected.y) << "viewer " << viewer << " seat " << seat;
            }
            EXPECT_EQ(layout.zones.wall.x, seat0.zones.wall.x);
        }
        for (const Seat noSeat : {kNoSeat, 4, -2}) {
            const TableLayout layout = MakeTableLayout(QuietInput(noSeat));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(layout.zones.discards[seat].x, seat0.zones.discards[seat].x);
                EXPECT_EQ(layout.zones.discards[seat].y, seat0.zones.discards[seat].y);
            }
        }
    }

    TEST(TableLayoutTest, TheSeatPlatesAreFourRectanglesOfOneSizeOnTheGridClearOfEverything) {
        const TableLayout design = MakeTableLayout(QuietInput(0));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const Rect& plate = design.zones.seatPlates[seat];
            EXPECT_EQ(std::fmod(plate.x, static_cast<float>(kGridUnit)), 0.0f) << "seat " << seat;
            EXPECT_EQ(std::fmod(plate.y, static_cast<float>(kGridUnit)), 0.0f) << "seat " << seat;
            EXPECT_EQ(std::fmod(plate.w, static_cast<float>(kGridUnit)), 0.0f);
            EXPECT_EQ(std::fmod(plate.h, static_cast<float>(kGridUnit)), 0.0f);
            EXPECT_EQ(plate.w, kSeatPlateW);
            EXPECT_EQ(plate.h, kSeatPlateH);
        }
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const Seat discardSeat : {kNoSeat, 0, 1, 2, 3}) {
                ASSERT_NO_FATAL_FAILURE(ExpectTableSound(MakeTableLayout(WorstInput(viewer, kWorstPile, discardSeat)))) << "viewer " << viewer;
            }
            ASSERT_NO_FATAL_FAILURE(ExpectTableSound(MakeTableLayout(QuietInput(viewer)))) << "viewer " << viewer;
            ASSERT_NO_FATAL_FAILURE(ExpectTableSound(MakeTableLayout(BusyInput(viewer)))) << "viewer " << viewer;
        }
    }

    TEST(TableLayoutTest, TheSeatPlateOfASeatLiesAtItsScreenPositionForEveryViewer) {
        const TableLayout seat0 = MakeTableLayout(QuietInput(0));
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = MakeTableLayout(QuietInput(viewer));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                // The plate of the seat at position p for this viewer is the plate that seat p has for viewer 0
                const Rect& expected = seat0.zones.seatPlates[SeatPosition(viewer, seat)];
                EXPECT_EQ(layout.zones.seatPlates[seat].x, expected.x) << "viewer " << viewer << " seat " << seat;
                EXPECT_EQ(layout.zones.seatPlates[seat].y, expected.y) << "viewer " << viewer << " seat " << seat;
            }
            // The same four rectangles, permuted: the bottom plate is the viewer's own
            EXPECT_EQ(layout.zones.seatPlates[viewer].y, seat0.zones.seatPlates[0].y);
            EXPECT_EQ(layout.zones.seatPlates[viewer].x, seat0.zones.seatPlates[0].x);
        }
        for (const Seat noSeat : {kNoSeat, 4, -2}) {
            const TableLayout layout = MakeTableLayout(QuietInput(noSeat));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(layout.zones.seatPlates[seat].x, seat0.zones.seatPlates[seat].x);
                EXPECT_EQ(layout.zones.seatPlates[seat].y, seat0.zones.seatPlates[seat].y);
            }
        }
        // The four are at four sides: bottom lowest, top highest, left of right
        const auto& plates = seat0.zones.seatPlates;
        EXPECT_GT(plates[0].y, plates[1].y);
        EXPECT_LT(plates[2].y, plates[1].y);
        EXPECT_LT(plates[3].x, plates[1].x);
    }

    TEST(TableLayoutTest, TheCenterPanelIsTheInsideOfTheWallRingAndTheSameForEveryViewer) {
        const TableLayout design = MakeTableLayout(WorstInput(0, 0, kNoSeat));
        const Rect& wall = design.zones.wall;
        const Rect& panel = design.zones.centerPanel;
        const TileSize stack = TileSizeOf(design, SlotRole::WallStack, 0);
        EXPECT_EQ(stack.w, stack.h) << "a wall stack is a square";
        EXPECT_LE(static_cast<float>(kWallRingColumns) * stack.w, wall.w + kTolerance);
        EXPECT_LE(static_cast<float>(kWallRingRows) * stack.h, wall.h + kTolerance);
        EXPECT_NEAR(panel.x, wall.x + stack.w, kTolerance);
        EXPECT_NEAR(panel.y, wall.y + stack.h, kTolerance);
        EXPECT_NEAR(panel.w, wall.w - 2.0f * stack.w, kTolerance);
        EXPECT_NEAR(panel.h, wall.h - 2.0f * stack.h, kTolerance);
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const Rect& other = MakeTableLayout(QuietInput(viewer)).zones.centerPanel;
            EXPECT_EQ(other.x, panel.x);
            EXPECT_EQ(other.y, panel.y);
            EXPECT_EQ(other.w, panel.w);
            EXPECT_EQ(other.h, panel.h);
        }
    }

    TEST(TableLayoutTest, TheCenterPanelHoldsNoSlotAndNoZoneAndKeepsTheWallStacksOutside) {
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                for (const Seat discardSeat : {kNoSeat, 0, 1, 2, 3}) {
                    const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, discardSeat), size.width, size.height);
                    const Rect& panel = layout.zones.centerPanel;
                    ASSERT_GT(panel.w, 0.0f);
                    ASSERT_GT(panel.h, 0.0f);
                    EXPECT_TRUE(Inside(panel, layout.viewWidth, layout.viewHeight));
                    EXPECT_TRUE(Holds(layout.zones.wall, panel));
                    for (const TileSlot& slot : layout.slots) {
                        EXPECT_FALSE(Overlaps(panel, slot.rect)) << "role " << static_cast<int>(slot.role) << " of seat " << slot.seat << " index " << slot.index << " view " << size.width << "x"
                                                                 << size.height;
                    }
                    for (const Rect& zone : layout.zones.discards) {
                        EXPECT_FALSE(Overlaps(panel, zone));
                    }
                    for (const Rect& plate : layout.zones.seatPlates) {
                        EXPECT_FALSE(Overlaps(panel, plate));
                    }
                }
            }
        }
    }

    TEST(TableLayoutTest, TheSeatPlatesScaleWithAnotherViewSize) {
        const TableInput input = WorstInput(1, 30, kNoSeat);
        for (const ViewSize& size : kViewSizes) {
            const TableLayout layout = MakeTableLayout(input, size.width, size.height);
            const float scale = LayoutScale(size.width, size.height);
            const TableShape shape = ShapeOfView(size.width, size.height);
            const TableLayout shapeDesign = MakeTableLayout(input, DesignWidthOf(shape), DesignHeightOf(shape));
            ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(layout, scale)) << "view " << size.width << "x" << size.height;
            const float offsetX = (size.width - DesignWidthOf(shape) * scale) / 2.0f;
            const float offsetY = (size.height - DesignHeightOf(shape) * scale) / 2.0f;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_NEAR(layout.zones.seatPlates[seat].x, offsetX + shapeDesign.zones.seatPlates[seat].x * scale, kTolerance) << "seat " << seat;
                EXPECT_NEAR(layout.zones.seatPlates[seat].y, offsetY + shapeDesign.zones.seatPlates[seat].y * scale, kTolerance) << "seat " << seat;
            }
        }
    }

    TEST(TableLayoutTest, AnotherViewSizeScalesTheDesignUniformlyAndCentresIt) {
        const TableInput input = WorstInput(0, 30, kNoSeat);
        const TableLayout design = MakeTableLayout(input);
        const TableLayout doubled = MakeTableLayout(input, 2.0f * kViewWidth, 2.0f * kViewHeight);
        ASSERT_EQ(design.slots.size(), doubled.slots.size());
        for (size_t i = 0; i < design.slots.size(); ++i) {
            EXPECT_NEAR(doubled.slots[i].rect.x, design.slots[i].rect.x * 2.0f, kTolerance);
            EXPECT_NEAR(doubled.slots[i].rect.y, design.slots[i].rect.y * 2.0f, kTolerance);
            EXPECT_NEAR(doubled.slots[i].rect.w, design.slots[i].rect.w * 2.0f, kTolerance);
            EXPECT_NEAR(doubled.slots[i].rect.h, design.slots[i].rect.h * 2.0f, kTolerance);
        }
        // A view twice as wide (a wide view) keeps the scale of the height and puts the table in the middle: the wing is the same ground.
        const TableLayout wide = MakeTableLayout(input, 2.0f * kViewWidth, kViewHeight);
        ASSERT_EQ(design.slots.size(), wide.slots.size());
        for (size_t i = 0; i < design.slots.size(); ++i) {
            EXPECT_NEAR(wide.slots[i].rect.x, design.slots[i].rect.x + kViewWidth / 2.0f, kTolerance);
            EXPECT_NEAR(wide.slots[i].rect.y, design.slots[i].rect.y, kTolerance);
            EXPECT_NEAR(wide.slots[i].rect.w, design.slots[i].rect.w, kTolerance);
        }
        // A portrait view scales the portrait design the same way.
        const TableLayout portrait = MakeTableLayout(input, kPortraitViewWidth, kPortraitViewHeight);
        const TableLayout portraitTall = MakeTableLayout(input, kPortraitViewWidth, kPortraitViewHeight + 200.0f);
        const TableLayout portraitDoubled = MakeTableLayout(input, 2.0f * kPortraitViewWidth, 2.0f * kPortraitViewHeight);
        ASSERT_EQ(portrait.slots.size(), portraitTall.slots.size());
        ASSERT_EQ(portrait.slots.size(), portraitDoubled.slots.size());
        for (size_t i = 0; i < portrait.slots.size(); ++i) {
            EXPECT_NEAR(portraitTall.slots[i].rect.x, portrait.slots[i].rect.x, kTolerance);
            EXPECT_NEAR(portraitTall.slots[i].rect.y, portrait.slots[i].rect.y + 100.0f, kTolerance);
            EXPECT_NEAR(portraitDoubled.slots[i].rect.x, portrait.slots[i].rect.x * 2.0f, kTolerance);
            EXPECT_NEAR(portraitDoubled.slots[i].rect.w, portrait.slots[i].rect.w * 2.0f, kTolerance);
        }
    }

    TEST(TableLayoutTest, DiscardsLieInRowsOfSixInTheOrderDiscarded) {
        TableInput input = QuietInput(0);
        for (int entry = 0; entry < 7; ++entry) {
            input.discardSeats.push_back(2);
        }
        const TableLayout layout = MakeTableLayout(input);
        const std::vector<TileSlot> pile = SlotsOf(layout, SlotRole::DiscardTile, 2);
        ASSERT_EQ(pile.size(), 7u);
        const TileSize tile = ExpectOneTileSize(layout, SlotRole::DiscardTile, 2);
        for (int i = 0; i < kDiscardsPerRow; ++i) {
            EXPECT_EQ(pile[i].rect.y, pile[0].rect.y);
            EXPECT_EQ(pile[i].rect.x, pile[0].rect.x + static_cast<float>(i) * tile.w);
        }
        // the 7th tile starts a new row, under the first
        EXPECT_EQ(pile[kDiscardsPerRow].rect.x, pile[0].rect.x);
        EXPECT_EQ(pile[kDiscardsPerRow].rect.y, pile[0].rect.y + tile.h);
        EXPECT_EQ(pile[kDiscardsPerRow].index, kDiscardsPerRow);
    }

    namespace {

        /// Where cell `cell` of a pile of at most `kDiscardRowCapacity` rows lies, written out from how the seat reads it: the bottom and top
        /// seats row by row from the left, the next row below; the left seat down a column, the next column to its right; the right seat up
        /// a column starting at the right-hand edge of the block, the next column to its left.
        Rect ExpectedDiscardRect(const Rect& zone, const TileSize& tile, int position, int cell) {
            const float column = static_cast<float>(cell % kDiscardsPerRow);
            const float row = static_cast<float>(cell / kDiscardsPerRow);
            switch (position) {
                case 3:
                    return Rect(zone.x + row * tile.h, zone.y + column * tile.w, tile.h, tile.w);
                case 1:
                    return Rect(zone.x + zone.w - (row + 1.0f) * tile.h, zone.y + zone.h - (column + 1.0f) * tile.w, tile.h, tile.w);
                default:
                    return Rect(zone.x + column * tile.w, zone.y + row * tile.h, tile.w, tile.h);
            }
        }

        void ExpectDiscardCell(const TableLayout& layout, const TileSlot& slot, int cell) {
            const Rect expected = ExpectedDiscardRect(layout.zones.discards[slot.seat], TileSizeOf(layout, SlotRole::DiscardTile, slot.seat), SeatPosition(layout.viewer, slot.seat), cell);
            EXPECT_NEAR(slot.rect.x, expected.x, kTolerance) << "seat " << slot.seat << " cell " << cell;
            EXPECT_NEAR(slot.rect.y, expected.y, kTolerance) << "seat " << slot.seat << " cell " << cell;
            EXPECT_NEAR(slot.rect.w, expected.w, kTolerance) << "seat " << slot.seat << " cell " << cell;
            EXPECT_NEAR(slot.rect.h, expected.h, kTolerance) << "seat " << slot.seat << " cell " << cell;
        }

    }  // namespace

    TEST(TableLayoutTest, TheDiscardsOfASideSeatRunTheWayThatSeatReadsThemAndTheNextRowLiesTowardTheMiddle) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            TableInput input = QuietInput(viewer);
            for (int entry = 0; entry < kSeatCount * (kDiscardsPerRow + 1); ++entry) {  // seven each: a full row and one more
                input.discardSeats.push_back(entry % kSeatCount);
            }
            const TableLayout layout = MakeTableLayout(input);
            const Seat right = SeatAtPosition(viewer, 1);
            const Seat left = SeatAtPosition(viewer, 3);
            const std::vector<TileSlot> rightPile = SlotsOf(layout, SlotRole::DiscardTile, right);
            const std::vector<TileSlot> leftPile = SlotsOf(layout, SlotRole::DiscardTile, left);
            ASSERT_EQ(rightPile.size(), 7u);
            ASSERT_EQ(leftPile.size(), 7u);
            const TileSize leftTile = ExpectOneTileSize(layout, SlotRole::DiscardTile, left);
            const TileSize rightTile = ExpectOneTileSize(layout, SlotRole::DiscardTile, right);
            // the left seat reads top to bottom, the right seat bottom to top
            for (int i = 1; i < kDiscardsPerRow; ++i) {
                EXPECT_EQ(leftPile[i].rect.x, leftPile[0].rect.x) << "viewer " << viewer;
                EXPECT_NEAR(leftPile[i].rect.y - leftPile[i - 1].rect.y, leftTile.w, kTolerance);
                EXPECT_EQ(rightPile[i].rect.x, rightPile[0].rect.x);
                EXPECT_NEAR(rightPile[i - 1].rect.y - rightPile[i].rect.y, rightTile.w, kTolerance);
            }
            // row 0 is at the outer side, the next row toward the middle of the table
            EXPECT_LT(leftPile[0].rect.x, leftPile[kDiscardsPerRow].rect.x);
            EXPECT_NEAR(leftPile[kDiscardsPerRow].rect.x - leftPile[0].rect.x, leftTile.h, kTolerance);
            EXPECT_EQ(leftPile[kDiscardsPerRow].rect.y, leftPile[0].rect.y);
            EXPECT_GT(rightPile[0].rect.x, rightPile[kDiscardsPerRow].rect.x);
            EXPECT_NEAR(rightPile[0].rect.x - rightPile[kDiscardsPerRow].rect.x, rightTile.h, kTolerance);
            EXPECT_EQ(rightPile[kDiscardsPerRow].rect.y, rightPile[0].rect.y);
            // the first tile of the left seat is at the top of its block, that of the right seat at the bottom, and both rows start at the outer edge
            EXPECT_NEAR(leftPile[0].rect.y, layout.zones.discards[left].y, kTolerance);
            EXPECT_NEAR(leftPile[0].rect.x, layout.zones.discards[left].x, kTolerance);
            EXPECT_NEAR(rightPile[0].rect.y + rightPile[0].rect.h, layout.zones.discards[right].y + layout.zones.discards[right].h, kTolerance);
            EXPECT_NEAR(rightPile[0].rect.x + rightPile[0].rect.w, layout.zones.discards[right].x + layout.zones.discards[right].w, kTolerance);
            for (size_t i = 0; i < leftPile.size(); ++i) {
                ExpectDiscardCell(layout, leftPile[i], static_cast<int>(i));
                ExpectDiscardCell(layout, rightPile[i], static_cast<int>(i));
            }
        }
    }

    TEST(TableLayoutTest, ThePileOfASideSeatSqueezesItsRowsAlongTheHorizontalAndNeverLeavesItsBlock) {
        for (const ViewSize& design : kDesigns) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                for (const int position : {1, 3}) {
                    const Seat seat = SeatAtPosition(viewer, position);
                    const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, seat), design.width, design.height);
                    const std::vector<TileSlot> pile = SlotsOf(layout, SlotRole::DiscardTile, seat);
                    const Rect& zone = layout.zones.discards[seat];
                    const int rows = (static_cast<int>(pile.size()) + kDiscardsPerRow - 1) / kDiscardsPerRow;
                    ASSERT_GT(rows, kDiscardRowCapacity) << "the worst pile is longer than the block holds";
                    const TileSize tile = ExpectOneTileSize(layout, SlotRole::DiscardTile, seat);
                    const float pitch = tile.h * (kDiscardRowCapacity - 1) / static_cast<float>(rows - 1);
                    for (size_t i = 0; i < pile.size(); ++i) {
                        const float row = static_cast<float>(i / kDiscardsPerRow);
                        EXPECT_TRUE(Holds(zone, pile[i].rect)) << "viewer " << viewer << " position " << position << " tile " << i;
                        const float expectedX = position == 3 ? zone.x + row * pitch : zone.x + zone.w - tile.h - row * pitch;
                        EXPECT_NEAR(pile[i].rect.x, expectedX, kTolerance) << "tile " << i;
                    }
                }
            }
        }
    }

    TEST(TableLayoutTest, EachSeatsDiscardsStayInItsBlockWhateverTheOrderOfThePile) {
        TableInput input = QuietInput(1);
        // the pile interleaves the seats; each slot's index is the position in the pile
        const Seat order[] = {0, 1, 1, 3, 2, 0, 1, 1, 1, 0, 3, 3, 1, 1, 1, 1, 1};
        for (const Seat seat : order) {
            input.discardSeats.push_back(seat);
        }
        const TableLayout layout = MakeTableLayout(input);
        int seen = 0;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const std::vector<TileSlot> pile = SlotsOf(layout, SlotRole::DiscardTile, seat);
            int own = 0;
            for (size_t entry = 0; entry < input.discardSeats.size(); ++entry) {
                if (input.discardSeats[entry] != seat) {
                    continue;
                }
                ASSERT_LT(own, static_cast<int>(pile.size()));
                EXPECT_EQ(pile[own].index, static_cast<int>(entry));
                ExpectDiscardCell(layout, pile[own], own);
                ++own;
            }
            EXPECT_EQ(own, static_cast<int>(pile.size()));
            seen += own;
        }
        EXPECT_EQ(seen, static_cast<int>(input.discardSeats.size()));
    }

    TEST(TableLayoutTest, ADiscardOfNoSeatHasNoSlot) {
        TableInput input = QuietInput(0);
        input.discardSeats = {0, kNoSeat, 9, 1};
        const TableLayout layout = MakeTableLayout(input);
        int discards = 0;
        for (const TileSlot& slot : layout.slots) {
            if (slot.role == SlotRole::DiscardTile) {
                ++discards;
            }
        }
        EXPECT_EQ(discards, 2);
    }

    TEST(TableLayoutTest, MeldsTakeThreeTilesAndAKongFour) {
        TableInput input = QuietInput(0);
        input.melds[0] = {MakeMeld(MeldKind::Chow), MakeMeld(MeldKind::Kong), MakeMeld(MeldKind::Pung)};
        input.concealedCount[0] = 4;
        const TableLayout layout = MakeTableLayout(input);
        const std::vector<TileSlot> melds = SlotsOf(layout, SlotRole::MeldTile, 0);
        ASSERT_EQ(melds.size(), 10u);
        const TileSize tile = ExpectOneTileSize(layout, SlotRole::MeldTile, 0);
        EXPECT_LT(tile.w, TileSizeOf(layout, SlotRole::HandTile, 0).w) << "a meld tile is smaller than a hand tile";
        const int expectedMeld[] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2};
        const int expectedPart[] = {0, 1, 2, 0, 1, 2, 3, 0, 1, 2};
        for (size_t i = 0; i < melds.size(); ++i) {
            EXPECT_EQ(melds[i].index, expectedMeld[i]);
            EXPECT_EQ(melds[i].part, expectedPart[i]);
            EXPECT_EQ(melds[i].rect.w, tile.w);
            EXPECT_EQ(melds[i].rect.y, melds[0].rect.y);
            if (i > 0) {
                const float gap = melds[i].rect.x - (melds[i - 1].rect.x + melds[i - 1].rect.w);
                EXPECT_NEAR(gap, melds[i].index == melds[i - 1].index ? 0.0f : kMeldGap, kTolerance);
            }
        }
        // the melds lie to the right of the hand
        const Rect hand = BoundsOf(MakeTableLayout(input), SlotRole::HandTile, 0);
        EXPECT_GT(melds[0].rect.x, hand.x + hand.w);
    }

    TEST(TableLayoutTest, BonusTilesAreASmallRow) {
        TableInput input = QuietInput(0);
        input.bonusCount = {3, 2, 0, 8};
        const TableLayout layout = MakeTableLayout(input);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const std::vector<TileSlot> bonus = SlotsOf(layout, SlotRole::BonusTile, seat);
            ASSERT_EQ(static_cast<int>(bonus.size()), input.bonusCount[seat]);
            if (bonus.empty()) {
                continue;
            }
            const TileSize tile = ExpectOneTileSize(layout, SlotRole::BonusTile, seat);
            EXPECT_LT(tile.w, TileSizeOf(layout, SlotRole::HandTile, seat).w) << "a bonus tile is smaller than a hand tile";
            for (int i = 0; i < input.bonusCount[seat]; ++i) {
                EXPECT_EQ(bonus[i].index, i);
            }
        }
    }

    TEST(TableLayoutTest, TheWallIsOneStackPerTwoTilesAroundTheMiddle) {
        EXPECT_EQ(WallStackCount(0), 0);
        EXPECT_EQ(WallStackCount(-5), 0);
        EXPECT_EQ(WallStackCount(1), 1);
        EXPECT_EQ(WallStackCount(2), 1);
        EXPECT_EQ(WallStackCount(3), 2);
        EXPECT_EQ(WallStackCount(kWorstWall), kWallRingCells);
        EXPECT_EQ(WallStackCount(100000), kWallRingCells);
        EXPECT_EQ(kWallRingCells, kWorstWall / 2);
        for (const int remaining : {0, 1, 2, 3, 57, 92, 143, kWorstWall}) {
            TableInput input = QuietInput(2);
            input.wallRemaining = remaining;
            const std::vector<TileSlot> wall = SlotsOf(MakeTableLayout(input), SlotRole::WallStack, 0);
            ASSERT_EQ(static_cast<int>(wall.size()), (remaining + 1) / 2) << "remaining " << remaining;
            for (size_t i = 0; i < wall.size(); ++i) {
                EXPECT_EQ(wall[i].index, static_cast<int>(i));
                for (size_t j = i + 1; j < wall.size(); ++j) {
                    EXPECT_FALSE(Overlaps(wall[i].rect, wall[j].rect)) << "stacks " << i << " and " << j;
                }
            }
        }
    }

    TEST(TableLayoutTest, TheWallRingIsAClosedLoopOfNeighbouringCells) {
        TableInput input = QuietInput(0);
        input.wallRemaining = kWorstWall;
        const TableLayout layout = MakeTableLayout(input);
        const std::vector<TileSlot> wall = SlotsOf(layout, SlotRole::WallStack, 0);
        ASSERT_EQ(static_cast<int>(wall.size()), kWallRingCells);
        const TileSize stack = ExpectOneTileSize(layout, SlotRole::WallStack, 0);
        for (size_t i = 0; i < wall.size(); ++i) {
            const Rect& a = wall[i].rect;
            const Rect& b = wall[(i + 1) % wall.size()].rect;
            EXPECT_NEAR(std::abs(a.x - b.x) + std::abs(a.y - b.y), stack.w, kTolerance) << "cell " << i;
        }
    }

    TEST(TableLayoutTest, HitHandTileFindsTheTileAtItsCentreAndNothingOutside) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = MakeTableLayout(BusyInput(viewer));
            const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, viewer);
            ASSERT_EQ(hand.size(), 14u);
            for (const TileSlot& slot : hand) {
                const std::optional<int> hit = HitHandTile(layout, slot.rect.x + slot.rect.w / 2.0f, slot.rect.y + slot.rect.h / 2.0f);
                ASSERT_TRUE(hit.has_value());
                EXPECT_EQ(*hit, slot.index);
            }
            const Rect bounds = BoundsOf(layout, SlotRole::HandTile, viewer);
            EXPECT_FALSE(HitHandTile(layout, bounds.x - 1.0f, bounds.y + bounds.h / 2.0f).has_value());
            EXPECT_FALSE(HitHandTile(layout, bounds.x + bounds.w + 1.0f, bounds.y + bounds.h / 2.0f).has_value());
            EXPECT_FALSE(HitHandTile(layout, bounds.x + bounds.w / 2.0f, bounds.y - 1.0f).has_value());
            EXPECT_FALSE(HitHandTile(layout, bounds.x + bounds.w / 2.0f, layout.viewHeight + 1.0f).has_value());
            EXPECT_FALSE(HitHandTile(layout, -10.0f, -10.0f).has_value());
        }
    }

    TEST(TableLayoutTest, APointInTheGapBetweenTilesHitsTheNearestTile) {
        // the shipped layout has no gap in the hand row, so the rule is tried on a row with two gaps
        TableLayout layout;
        layout.viewer = 0;
        const float xs[] = {100.0f, 200.0f, 400.0f};
        for (int index = 0; index < 3; ++index) {
            layout.slots.push_back(TileSlot{Rect(xs[index], 600.0f, kViewerHandTileW, kViewerHandTileH), SlotRole::HandTile, 0, index});
        }
        const float y = 630.0f;
        // gap 1: 148..200, middle 174
        EXPECT_EQ(HitHandTile(layout, 160.0f, y), 0);
        EXPECT_EQ(HitHandTile(layout, 190.0f, y), 1);
        EXPECT_EQ(HitHandTile(layout, 174.0f, y), 1) << "on a tie the later tile";
        // gap 2: 248..400
        EXPECT_EQ(HitHandTile(layout, 300.0f, y), 1);
        EXPECT_EQ(HitHandTile(layout, 330.0f, y), 2);
        // the gaps count in the margin under the tiles too
        EXPECT_EQ(HitHandTile(layout, 160.0f, 668.0f), 0);
        EXPECT_FALSE(HitHandTile(layout, 160.0f, 599.0f).has_value());
    }

    TEST(TableLayoutTest, WhereTheHandIsSqueezedTheLaterOfTheNearestTilesWinsInTheMargin) {
        TableInput input = QuietInput(0);
        input.concealedCount[0] = 40;
        const TableLayout layout = MakeTableLayout(input);
        const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, 0);
        ASSERT_EQ(hand.size(), 40u);
        const float below = hand[0].rect.y + hand[0].rect.h + 2.0f;
        EXPECT_EQ(HitHandTile(layout, hand[10].rect.x + 0.5f, below), 10);
        EXPECT_EQ(HitHandTile(layout, hand.back().rect.x + hand.back().rect.w - 0.5f, below), 39);
    }

    TEST(TableLayoutTest, TheMarginUnderTheTilesAndTheWholeHeightOfTheStripHitTheTileAbove) {
        for (const ViewSize& size : kViewSizes) {
            const TableLayout layout = MakeTableLayout(BusyInput(0), size.width, size.height);
            const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, 0);
            ASSERT_EQ(hand.size(), 14u);
            // the bottom of the table fitted into the view: the table is centred, so there may be a bar under it
            const float fit = LayoutScale(size.width, size.height);
            const float viewBottom = (size.height + DesignHeightOf(ShapeOfView(size.width, size.height)) * fit) / 2.0f;
            for (const TileSlot& slot : hand) {
                const float x = slot.rect.x + slot.rect.w / 2.0f;
                EXPECT_EQ(HitHandTile(layout, x, slot.rect.y + 0.1f), slot.index) << "view " << size.width << "x" << size.height;
                EXPECT_EQ(HitHandTile(layout, x, slot.rect.y + slot.rect.h + 0.5f), slot.index) << "the margin under the tile";
                EXPECT_EQ(HitHandTile(layout, x, viewBottom - 0.1f), slot.index) << "the bottom of the view";
                EXPECT_FALSE(HitHandTile(layout, x, viewBottom + 0.1f).has_value()) << "under the view";
                EXPECT_FALSE(HitHandTile(layout, x, slot.rect.y - 0.1f).has_value()) << "above the tiles";
            }
        }
    }

    TEST(TableLayoutTest, TheHandStripStopsAtTheEdgesOfTheFirstAndTheLastTile) {
        const TableLayout layout = MakeTableLayout(BusyInput(0));
        const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, 0);
        const float y = hand[0].rect.y + hand[0].rect.h / 2.0f;
        EXPECT_FALSE(HitHandTile(layout, hand.front().rect.x - 0.5f, y).has_value());
        EXPECT_EQ(HitHandTile(layout, hand.front().rect.x + 0.1f, y), 0);
        EXPECT_EQ(HitHandTile(layout, hand.back().rect.x + hand.back().rect.w - 0.1f, y), 13);
        EXPECT_FALSE(HitHandTile(layout, hand.back().rect.x + hand.back().rect.w + 0.5f, y).has_value());
        // the same for the margin under the tiles
        const float below = hand[0].rect.y + hand[0].rect.h + 2.0f;
        EXPECT_FALSE(HitHandTile(layout, hand.front().rect.x - 0.5f, below).has_value());
        EXPECT_FALSE(HitHandTile(layout, hand.back().rect.x + hand.back().rect.w + 0.5f, below).has_value());
    }

    TEST(TableLayoutTest, NothingHitsWhereTheViewerHasNoHandTiles) {
        TableInput input = BusyInput(0);
        input.concealedCount[0] = 0;
        const TableLayout layout = MakeTableLayout(input);
        for (float x = 0.0f; x < kViewWidth; x += 16.0f) {
            for (float y = 0.0f; y < kViewHeight; y += 16.0f) {
                ASSERT_FALSE(HitHandTile(layout, x, y).has_value()) << x << "," << y;
            }
        }
    }

    TEST(TableLayoutTest, TheHitRegionOfTheHandReachesNeitherTheBonusRowNorTheMeldsNorAZone) {
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, kNoSeat), size.width, size.height);
                const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, viewer);
                ASSERT_EQ(hand.size(), 14u);
                // the strip as the rule defines it, down to the bottom of the fitted view
                const Rect handBounds = BoundsOf(layout, SlotRole::HandTile, viewer);
                const float scale = handBounds.h / kViewerHandTileH;
                const Rect strip(handBounds.x, handBounds.y, handBounds.w, handBounds.h + 8.0f * scale);
                for (const TileSlot& slot : layout.slots) {
                    if (slot.role == SlotRole::HandTile && slot.seat == viewer) {
                        continue;
                    }
                    if (slot.role == SlotRole::DiscardTile || slot.role == SlotRole::WallStack || slot.seat == viewer) {
                        EXPECT_FALSE(Overlaps(strip, slot.rect)) << "role " << static_cast<int>(slot.role) << " of seat " << slot.seat << " index " << slot.index;
                    }
                }
                for (const Rect& zone : PlateZones(layout)) {
                    EXPECT_FALSE(Overlaps(strip, zone));
                }
                EXPECT_FALSE(Overlaps(strip, layout.zones.centerPanel));
                // a point in each bonus tile and each meld tile of the viewer, and in every other seat's slot, hits nothing
                for (const TileSlot& slot : layout.slots) {
                    if (slot.role == SlotRole::HandTile && slot.seat == viewer) {
                        continue;
                    }
                    EXPECT_FALSE(HitHandTile(layout, slot.rect.x + slot.rect.w / 2.0f, slot.rect.y + slot.rect.h / 2.0f).has_value())
                        << "role " << static_cast<int>(slot.role) << " of seat " << slot.seat;
                }
            }
        }
    }

    TEST(TableLayoutTest, HitHandTileIgnoresEverythingButTheViewersHand) {
        const TableLayout layout = MakeTableLayout(WorstInput(0, 20, kNoSeat));
        for (const TileSlot& slot : layout.slots) {
            if (slot.role == SlotRole::HandTile && slot.seat == 0) {
                continue;
            }
            const float x = slot.rect.x + slot.rect.w / 2.0f;
            const float y = slot.rect.y + slot.rect.h / 2.0f;
            const std::optional<int> hit = HitHandTile(layout, x, y);
            if (hit) {
                ADD_FAILURE() << "a slot of role " << static_cast<int>(slot.role) << " of seat " << slot.seat << " hits hand tile " << *hit;
            }
        }
    }

    TEST(TableLayoutTest, WhereTilesOverlapTheLaterTileIsHit) {
        TableInput input = QuietInput(0);
        input.concealedCount[0] = 40;
        const TableLayout layout = MakeTableLayout(input);
        const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, 0);
        ASSERT_EQ(hand.size(), 40u);
        ASSERT_TRUE(Overlaps(hand[0].rect, hand[1].rect));
        // the point where tile 1 begins is over tile 0 too: the later one wins
        const std::optional<int> hit = HitHandTile(layout, hand[1].rect.x + kTolerance, hand[1].rect.y + 1.0f);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(*hit, 1);
        // the last tile is hit at its centre, and the first where nothing covers it
        const TileSlot& last = hand.back();
        EXPECT_EQ(HitHandTile(layout, last.rect.x + last.rect.w / 2.0f, last.rect.y + last.rect.h / 2.0f), 39);
        EXPECT_EQ(HitHandTile(layout, hand[0].rect.x + kTolerance, hand[0].rect.y + 1.0f), 0);
    }

    TEST(TableLayoutTest, TheLayoutIsDeterministic) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableInput input = WorstInput(viewer, 57, kNoSeat);
            ExpectSameSlots(MakeTableLayout(input), MakeTableLayout(input));
        }
    }

    TEST(TableLayoutTest, AViewerThatIsNoSeatIsLaidOutAsSeatZero) {
        for (const Seat viewer : {kNoSeat, 4, 100}) {
            TableInput input = WorstInput(0, 10, kNoSeat);
            const TableLayout expected = MakeTableLayout(input);
            input.viewer = viewer;
            const TableLayout layout = MakeTableLayout(input);
            EXPECT_EQ(layout.viewer, 0);
            ExpectSameSlots(expected, layout);
        }
    }

    TEST(TableLayoutTest, NegativeCountsLayOutNothing) {
        TableInput input;
        input.concealedCount = {-1, -5, 0, 0};
        input.bonusCount = {-2, 0, 0, 0};
        input.wallRemaining = -9;
        EXPECT_TRUE(MakeTableLayout(input).slots.empty());
    }

    // The claimed tile of an open set.

    namespace {

        Tile Parsed(std::string_view text) {
            return TryParse(text).value();
        }

        Meld ClaimedMeld(MeldKind kind, Seat from, std::string_view first, std::string_view claimed) {
            Meld meld = MakeMeld(kind);
            meld.first = Parsed(first);
            meld.claimed = Parsed(claimed);
            meld.from = from;
            meld.fromDiscard = true;
            return meld;
        }

        /// The edge of the claimed tile by the table of the plan: rows the owner's screen position, columns the source is the next
        /// seat, across, the previous seat.
        Edge ExpectedEdge(int position, int offset) {
            constexpr Edge kTable[4][3] = {
                {Edge::Right, Edge::Top, Edge::Left},
                {Edge::Top, Edge::Left, Edge::Bottom},
                {Edge::Left, Edge::Bottom, Edge::Right},
                {Edge::Bottom, Edge::Right, Edge::Top},
            };
            return kTable[position][offset - 1];
        }

        /// The (part, edge) of every meld tile of `owner`'s meld `meld` that has an edge.
        std::vector<std::pair<int, Edge>> ClaimedTiles(const TableLayout& layout, Seat owner, int meld) {
            std::vector<std::pair<int, Edge>> found;
            for (const TileSlot& slot : SlotsOf(layout, SlotRole::MeldTile, owner)) {
                if (slot.index == meld && slot.claimEdge != Edge::None) {
                    found.emplace_back(slot.part, slot.claimEdge);
                }
            }
            return found;
        }

    }  // namespace

    TEST(TableLayoutTest, TheViewersHandStripOfTheWorstTableIsOneRowOf14Tiles) {
        // no gap for a drawn tile: the strip ends where 14 tiles end, and the melds start after it
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, kNoSeat));
            const Rect hand = BoundsOf(layout, SlotRole::HandTile, viewer);
            const Rect melds = BoundsOf(layout, SlotRole::MeldTile, viewer);
            const TileSize tile = ExpectOneTileSize(layout, SlotRole::HandTile, viewer);
            EXPECT_NEAR(hand.w, static_cast<float>(kMaxConcealedTiles) * tile.w, kTolerance) << "14 tiles side by side, none left over";
            EXPECT_LE(hand.x + hand.w, melds.x + kTolerance);
            const std::vector<TileSlot> tiles = SlotsOf(layout, SlotRole::HandTile, viewer);
            ASSERT_EQ(static_cast<int>(tiles.size()), kMaxConcealedTiles);
            for (size_t i = 1; i < tiles.size(); ++i) {
                EXPECT_NEAR(tiles[i].rect.x - tiles[i - 1].rect.x, tile.w, kTolerance) << "tile " << i;
            }
        }
    }

    TEST(TableLayoutTest, TheClaimedTileOfAPungOrKongFacesTheSeatItCameFrom) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (Seat owner = 0; owner < kSeatCount; ++owner) {
                for (const MeldKind kind : {MeldKind::Pung, MeldKind::Kong}) {
                    for (int offset = 1; offset < kSeatCount; ++offset) {
                        TableInput input = QuietInput(viewer);
                        input.melds[owner] = {MakeMeld(MeldKind::Pung), ClaimedMeld(kind, (owner + offset) % kSeatCount, "5p", "5p")};
                        const TableLayout layout = MakeTableLayout(input);
                        const int expectedPart = 3 - offset;  // the previous seat: first tile, across: middle, the next seat: last
                        const std::vector<std::pair<int, Edge>> claimed = ClaimedTiles(layout, owner, 1);
                        ASSERT_EQ(claimed.size(), 1u) << "viewer " << viewer << " owner " << owner << " offset " << offset;
                        EXPECT_EQ(claimed[0].first, expectedPart);
                        EXPECT_EQ(claimed[0].second, ExpectedEdge(SeatPosition(viewer, owner), offset)) << "viewer " << viewer << " owner " << owner << " offset " << offset;
                        EXPECT_TRUE(ClaimedTiles(layout, owner, 0).empty()) << "the meld that was not claimed";
                    }
                }
            }
        }
    }

    TEST(TableLayoutTest, TheClaimedTileOfAChowIsTheOneThatWasClaimedAndFacesThePreviousSeat) {
        const char* const kClaimed[] = {"4p", "5p", "6p"};
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (Seat owner = 0; owner < kSeatCount; ++owner) {
                for (int part = 0; part < 3; ++part) {
                    TableInput input = QuietInput(viewer);
                    input.melds[owner] = {ClaimedMeld(MeldKind::Chow, (owner + 3) % kSeatCount, "4p", kClaimed[part])};
                    const std::vector<std::pair<int, Edge>> claimed = ClaimedTiles(MakeTableLayout(input), owner, 0);
                    ASSERT_EQ(claimed.size(), 1u);
                    EXPECT_EQ(claimed[0].first, part);
                    EXPECT_EQ(claimed[0].second, ExpectedEdge(SeatPosition(viewer, owner), 3));
                }
            }
        }
    }

    TEST(TableLayoutTest, NoClaimedTileOnAConcealedKongAMeldNotFromADiscardOrACorruptOne) {
        TableInput input = QuietInput(0);
        Meld concealedKong = ClaimedMeld(MeldKind::Kong, 1, "5p", "5p");
        concealedKong.concealed = true;
        concealedKong.fromDiscard = false;
        Meld notFromDiscard = ClaimedMeld(MeldKind::Pung, 1, "5p", "5p");
        notFromDiscard.fromDiscard = false;
        Meld fromItself = ClaimedMeld(MeldKind::Pung, 0, "5p", "5p");
        Meld claimedOutsideTheChow = ClaimedMeld(MeldKind::Chow, 3, "4p", "9p");
        Meld noTile = MakeMeld(MeldKind::Chow);
        noTile.fromDiscard = true;
        noTile.from = 3;
        input.melds[0] = {concealedKong, notFromDiscard, fromItself, claimedOutsideTheChow, noTile};
        for (const TileSlot& slot : MakeTableLayout(input).slots) {
            EXPECT_EQ(slot.claimEdge, Edge::None) << "role " << static_cast<int>(slot.role) << " meld " << slot.index << " part " << slot.part;
        }
    }

    TEST(TableLayoutTest, AnUpgradedKongKeepsTheMarkOfItsPungAndAnyViewSizeKeepsTheEdges) {
        TableInput input = QuietInput(0);
        input.melds[2] = {ClaimedMeld(MeldKind::Kong, 1, "5p", "5p")};  // the pung of seat 2 was claimed from the seat before it
        for (const ViewSize& size : kViewSizes) {
            const std::vector<std::pair<int, Edge>> claimed = ClaimedTiles(MakeTableLayout(input, size.width, size.height), 2, 0);
            ASSERT_EQ(claimed.size(), 1u);
            EXPECT_EQ(claimed[0].first, 0);
            EXPECT_EQ(claimed[0].second, Edge::Right);  // seat 2 is at the top for the viewer 0; the previous seat: right
        }
    }

    TEST(TableLayoutTest, BoundsOfMeldIsTheRectangleRoundTheTilesOfThatMeld) {
        TableInput input = QuietInput(0);
        input.melds[0] = {MakeMeld(MeldKind::Chow), MakeMeld(MeldKind::Kong)};
        input.melds[2] = {MakeMeld(MeldKind::Pung)};
        const TableLayout layout = MakeTableLayout(input);
        const std::vector<TileSlot> melds = SlotsOf(layout, SlotRole::MeldTile, 0);
        const TileSize tile = ExpectOneTileSize(layout, SlotRole::MeldTile, 0);
        const Rect chow = BoundsOfMeld(layout, 0, 0);
        const Rect kong = BoundsOfMeld(layout, 0, 1);
        EXPECT_NEAR(chow.x, melds[0].rect.x, kTolerance);
        EXPECT_NEAR(chow.w, 3.0f * tile.w, kTolerance);
        EXPECT_NEAR(chow.h, tile.h, kTolerance);
        EXPECT_NEAR(kong.x, melds[3].rect.x, kTolerance);
        EXPECT_NEAR(kong.w, 4.0f * tile.w, kTolerance);
        EXPECT_FALSE(Overlaps(chow, kong));
        // the top seat's meld is laid in a row of its own size
        const TileSize topTile = ExpectOneTileSize(layout, SlotRole::MeldTile, 2);
        EXPECT_NEAR(BoundsOfMeld(layout, 2, 0).w, 3.0f * topTile.w, kTolerance);
        // a side seat's meld is a column of turned tiles
        input.melds[1] = {MakeMeld(MeldKind::Pung)};
        const TableLayout withColumn = MakeTableLayout(input);
        const TileSize sideTile = ExpectOneTileSize(withColumn, SlotRole::MeldTile, 1);
        const Rect column = BoundsOfMeld(withColumn, 1, 0);
        EXPECT_NEAR(column.h, 3.0f * sideTile.w, kTolerance);
        EXPECT_NEAR(column.w, sideTile.h, kTolerance);
    }

    TEST(TableLayoutTest, BoundsOfMeldIsEmptyWhenThereIsNoSuchMeld) {
        TableInput input = QuietInput(0);
        input.melds[0] = {MakeMeld(MeldKind::Pung)};
        const TableLayout layout = MakeTableLayout(input);
        EXPECT_TRUE(BoundsOfMeld(layout, 0, 1).IsEmpty());
        EXPECT_TRUE(BoundsOfMeld(layout, 0, -1).IsEmpty());
        EXPECT_TRUE(BoundsOfMeld(layout, 1, 0).IsEmpty());
        EXPECT_TRUE(BoundsOfMeld(layout, kNoSeat, 0).IsEmpty());
    }

    // The turned side seats.

    TEST(TableLayoutTest, TheTilesOfTheSideSeatsAreTurnedAndWiderThanHighTheOthersAreUpright) {
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, kNoSeat), size.width, size.height);
                for (const TileSlot& slot : layout.slots) {
                    const int position = slot.role == SlotRole::WallStack ? 0 : SeatPosition(viewer, slot.seat);
                    const float expected = position == 3 ? kLeftSeatRotation : position == 1 ? kRightSeatRotation : 0.0f;
                    EXPECT_EQ(slot.rotation, expected) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                    if (position == 1 || position == 3) {
                        EXPECT_GT(slot.rect.w, slot.rect.h) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                    } else if (slot.role != SlotRole::WallStack) {  // a wall stack is a square
                        EXPECT_LT(slot.rect.w, slot.rect.h);
                    }
                }
            }
        }
        EXPECT_EQ(kLeftSeatRotation, 90.0f);
        EXPECT_EQ(kRightSeatRotation, 270.0f);
    }

    TEST(TableLayoutTest, TheSideStripsRunDownTheScreenWithThePitchOfTheTurnedTile) {
        TableInput input = QuietInput(0);
        input.concealedCount = {13, 13, 13, 13};
        input.bonusCount = {0, 3, 0, 3};
        input.melds[1] = {MakeMeld(MeldKind::Pung), MakeMeld(MeldKind::Chow)};
        input.melds[3] = {MakeMeld(MeldKind::Kong), MakeMeld(MeldKind::Pung)};
        const TableLayout layout = MakeTableLayout(input);
        for (const Seat seat : {1, 3}) {
            const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, seat);
            ASSERT_EQ(hand.size(), 13u);
            const TileSize handTile = ExpectOneTileSize(layout, SlotRole::HandTile, seat);
            for (size_t i = 0; i < hand.size(); ++i) {
                EXPECT_EQ(hand[i].rect.w, handTile.h) << "seat " << seat << ": the tile lies on its side";
                EXPECT_EQ(hand[i].rect.h, handTile.w);
                EXPECT_EQ(hand[i].rect.x, hand[0].rect.x);
                if (i > 0) {
                    EXPECT_NEAR(hand[i].rect.y - hand[i - 1].rect.y, hand[i].rect.h, kTolerance) << "the pitch is the height of the turned tile";
                }
            }
            const std::vector<TileSlot> bonus = SlotsOf(layout, SlotRole::BonusTile, seat);
            ASSERT_EQ(bonus.size(), 3u);
            ExpectOneTileSize(layout, SlotRole::BonusTile, seat);
            for (size_t i = 1; i < bonus.size(); ++i) {
                EXPECT_EQ(bonus[i].rect.x, bonus[0].rect.x);
                EXPECT_NEAR(bonus[i].rect.y - bonus[i - 1].rect.y, bonus[i].rect.h, kTolerance);
            }
            const std::vector<TileSlot> melds = SlotsOf(layout, SlotRole::MeldTile, seat);
            ASSERT_GE(melds.size(), 6u);
            ExpectOneTileSize(layout, SlotRole::MeldTile, seat);
            for (size_t i = 1; i < melds.size(); ++i) {
                EXPECT_EQ(melds[i].rect.x, melds[0].rect.x);
                EXPECT_EQ(melds[i].rect.w, melds[0].rect.w);
                const float gap = melds[i].rect.y - (melds[i - 1].rect.y + melds[i - 1].rect.h);
                EXPECT_NEAR(gap, melds[i].index == melds[i - 1].index ? 0.0f : kMeldGap, kTolerance) << "seat " << seat << " tile " << i;
            }
        }
    }

    TEST(TableLayoutTest, TheSideColumnsLieHandMeldsBonusBlockFromTheEdgeInwardsAndTheBlockIsAsLaid) {
        const TableLayout layout = MakeTableLayout(BusyInput(0));
        const Rect leftHand = BoundsOf(layout, SlotRole::HandTile, 3);
        const Rect leftMelds = BoundsOf(layout, SlotRole::MeldTile, 3);
        const Rect leftBonus = BoundsOf(layout, SlotRole::BonusTile, 3);
        const Rect rightHand = BoundsOf(layout, SlotRole::HandTile, 1);
        const Rect rightMelds = BoundsOf(layout, SlotRole::MeldTile, 1);
        const Rect rightBonus = BoundsOf(layout, SlotRole::BonusTile, 1);
        EXPECT_LT(leftHand.x + leftHand.w, leftMelds.x);
        EXPECT_LT(leftMelds.x + leftMelds.w, leftBonus.x);
        EXPECT_LT(leftBonus.x + leftBonus.w, layout.zones.discards[3].x);
        EXPECT_GT(rightHand.x, rightMelds.x + rightMelds.w);
        EXPECT_GT(rightMelds.x, rightBonus.x + rightBonus.w);
        EXPECT_GT(rightBonus.x, layout.zones.discards[1].x + layout.zones.discards[1].w);
        // the block holds six by five tiles, turned for the side seats
        const TableLayout worstLayout = MakeTableLayout(WorstInput(0, kWorstPile, kNoSeat));
        for (const Seat seat : {1, 3}) {
            const TileSize tile = ExpectOneTileSize(worstLayout, SlotRole::DiscardTile, seat);
            EXPECT_GE(worstLayout.zones.discards[seat].w, tile.h * kDiscardRowCapacity - kTolerance);
            EXPECT_GE(worstLayout.zones.discards[seat].h, tile.w * kDiscardsPerRow - kTolerance);
        }
        const TileSize bottom = ExpectOneTileSize(worstLayout, SlotRole::DiscardTile, 0);
        EXPECT_GE(worstLayout.zones.discards[0].w, bottom.w * kDiscardsPerRow - kTolerance);
        const TileSize top = ExpectOneTileSize(worstLayout, SlotRole::DiscardTile, 2);
        EXPECT_GE(worstLayout.zones.discards[2].w, top.w * kDiscardsPerRow - kTolerance);
        EXPECT_GE(worstLayout.zones.discards[2].h, top.h * kDiscardRowCapacity - kTolerance);
    }

    TEST(TableLayoutTest, TheUprightRectangleOfATurnedSlotSwapsWidthAndHeightAboutTheCentre) {
        const Rect screen(100.0f, 50.0f, 32.0f, 24.0f);
        for (const float rotation : {90.0f, 270.0f, -90.0f, -270.0f, 450.0f}) {
            const Rect upright = UprightRect(screen, rotation);
            EXPECT_EQ(upright.w, 24.0f) << rotation;
            EXPECT_EQ(upright.h, 32.0f) << rotation;
            EXPECT_NEAR(RectCentreX(upright), RectCentreX(screen), kTolerance) << rotation;
            EXPECT_NEAR(RectCentreY(upright), RectCentreY(screen), kTolerance) << rotation;
        }
        EXPECT_EQ(RectCentreX(screen), 116.0f);
        EXPECT_EQ(RectCentreY(screen), 62.0f);
        for (const float rotation : {0.0f, 180.0f, 360.0f, 45.0f, std::nanf("")}) {
            const Rect upright = UprightRect(screen, rotation);
            EXPECT_EQ(upright.x, screen.x);
            EXPECT_EQ(upright.y, screen.y);
            EXPECT_EQ(upright.w, screen.w);
            EXPECT_EQ(upright.h, screen.h);
        }
    }

    TEST(TableLayoutTest, TheUprightRectangleOfASideSlotIsTheTileAsDesignedTurnedBackAboutItsCentre) {
        const TableLayout layout = MakeTableLayout(QuietInput(0));
        for (const TileSlot& slot : SlotsOf(layout, SlotRole::HandTile, 1)) {
            const Rect upright = UprightRect(slot.rect, slot.rotation);
            EXPECT_EQ(upright.w, slot.rect.h) << "the tile as designed, on its side on the screen";
            EXPECT_EQ(upright.h, slot.rect.w);
            ExpectAtlasProportions(TileSize{upright.w, upright.h});
            EXPECT_GE(upright.w, static_cast<float>(MinTileWOf(layout, SlotRole::HandTile, 1)));
        }
        for (const TileSlot& slot : SlotsOf(layout, SlotRole::HandTile, 2)) {
            EXPECT_EQ(UprightRect(slot.rect, slot.rotation).x, slot.rect.x);
        }
    }

    // The phone profile: the viewer's hand large, one row across the view.

    namespace {

        /// The widths the phone profile is tried at: the desktop tile (the least), two in between, the widest it holds.
        const float kPhoneTileWidths[] = {kViewerHandTileW, 70.0f, 77.0f, kPhoneHandTileMaxW};

        TableInput PhoneInput(TableInput input, float handTileW) {
            input.profile = TableProfile{true, handTileW};
            return input;
        }

        /// Every table the phone profile has to hold for `viewer`: the worst ones (the pile of each seat and spread), a busy and a quiet one.
        std::vector<TableInput> PhoneTables(Seat viewer, float handTileW) {
            std::vector<TableInput> tables;
            for (const Seat discardSeat : {kNoSeat, 0, 1, 2, 3}) {
                tables.push_back(PhoneInput(WorstInput(viewer, kWorstPile, discardSeat), handTileW));
            }
            tables.push_back(PhoneInput(BusyInput(viewer), handTileW));
            tables.push_back(PhoneInput(QuietInput(viewer), handTileW));
            return tables;
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        /// The slots of every seat but `viewer`, and the wall, in layout order.
        std::vector<TileSlot> SlotsApartFromTheViewers(const TableLayout& layout) {
            std::vector<TileSlot> found;
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::WallStack || slot.seat != layout.viewer) {
                    found.push_back(slot);
                }
            }
            return found;
        }

    }  // namespace

    TEST(TableLayoutTest, TheDefaultLayoutIsTheExplicitDesktopOneWhateverTheTileWidthOfAnUnusedPhoneProfile) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const TableInput& input : {WorstInput(viewer, kWorstPile, kNoSeat), BusyInput(viewer), QuietInput(viewer)}) {
                const TableLayout layout = MakeTableLayout(input);
                EXPECT_FALSE(layout.profile.phone);
                EXPECT_EQ(layout.profile.handTileW, kViewerHandTileW);
                for (const TableProfile& profile : {TableProfile{false, kViewerHandTileW}, TableProfile{false, 70.0f}, TableProfile{false, 0.0f}}) {
                    TableInput explicitInput = input;
                    explicitInput.profile = profile;
                    const TableLayout other = MakeTableLayout(explicitInput);
                    ExpectSameSlots(layout, other);
                    EXPECT_EQ(other.profile, TableProfile{});
                    for (Seat seat = 0; seat < kSeatCount; ++seat) {
                        EXPECT_TRUE(SameRect(layout.zones.discards[seat], other.zones.discards[seat]));
                        EXPECT_TRUE(SameRect(layout.zones.seatPlates[seat], other.zones.seatPlates[seat]));
                    }
                    EXPECT_TRUE(SameRect(layout.zones.wall, other.zones.wall));
                    EXPECT_TRUE(SameRect(layout.zones.centerPanel, other.zones.centerPanel));
                }
            }
        }
    }

    TEST(TableLayoutTest, ThePhoneProfileKeepsEveryZoneSoundAndEveryPlateClearAtEveryTileWidthForEveryViewer) {
        for (const float handTileW : kPhoneTileWidths) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                for (const TableInput& input : PhoneTables(viewer, handTileW)) {
                    const TableLayout layout = MakeTableLayout(input);
                    ASSERT_TRUE(layout.profile.phone);
                    ASSERT_NO_FATAL_FAILURE(ExpectZonesAreSound(layout)) << "width " << handTileW << " viewer " << viewer;
                    ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(layout, 1.0f)) << "width " << handTileW << " viewer " << viewer;
                    ASSERT_NO_FATAL_FAILURE(ExpectSlotsInsideTheView(layout)) << "width " << handTileW << " viewer " << viewer;
                    ASSERT_NO_FATAL_FAILURE(ExpectNoZoneOverlaps(layout)) << "width " << handTileW << " viewer " << viewer;
                }
            }
        }
    }

    TEST(TableLayoutTest, ThePhoneProfileHoldsTheSqueezedWorstTableAtEveryViewSizeToo) {
        TableInput input = WorstInput(0, kWorstPile, 0);
        input.profile = TableProfile{true, kPhoneHandTileMaxW};
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                input.viewer = viewer;
                const TableLayout layout = MakeTableLayout(input, size.width, size.height);
                ASSERT_NO_FATAL_FAILURE(ExpectZonesAreSound(layout)) << "view " << size.width << "x" << size.height << " viewer " << viewer;
                ASSERT_NO_FATAL_FAILURE(ExpectSlotsInsideTheView(layout));
                ASSERT_NO_FATAL_FAILURE(ExpectNoZoneOverlaps(layout));
                ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(layout, LayoutScale(size.width, size.height)));
            }
        }
    }

    TEST(TableLayoutTest, ThePhoneHandIsOneRowOfFourteenTilesOfTheWidthAskedForSideBySideFromTheLeft) {
        for (const float handTileW : kPhoneTileWidths) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(PhoneInput(WorstInput(viewer, kWorstPile, kNoSeat), handTileW));
                const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, viewer);
                ASSERT_EQ(static_cast<int>(hand.size()), kMaxConcealedTiles);
                for (int i = 0; i < kMaxConcealedTiles; ++i) {
                    EXPECT_EQ(hand[i].index, i);
                    EXPECT_NEAR(hand[i].rect.w, handTileW, kTolerance);
                    EXPECT_NEAR(hand[i].rect.h, handTileW * 4.0f / 3.0f, kTolerance) << "a tile of the atlas is 3 : 4";
                    EXPECT_EQ(hand[i].rect.y, hand[0].rect.y);
                    if (i > 0) {
                        EXPECT_NEAR(hand[i].rect.x, hand[i - 1].rect.x + hand[i - 1].rect.w, kTolerance) << "side by side as on the desktop, width " << handTileW;
                    }
                    // a click at the centre of a tile finds it
                    EXPECT_EQ(HitHandTile(layout, RectCentreX(hand[i].rect), RectCentreY(hand[i].rect)), i);
                }
                const Rect bounds = BoundsOf(layout, SlotRole::HandTile, viewer);
                EXPECT_NEAR(bounds.x, static_cast<float>(kRimWidth + kRackPadding), kTolerance) << "from the left, the rack past the hand ends at the rim";
                EXPECT_LE(bounds.x + bounds.w, kViewWidth - kRimWidth - kRackPadding + kTolerance);
                EXPECT_NEAR(bounds.y + bounds.h, kViewHeight - kRimWidth, kTolerance) << "the hand rests on the rim";
            }
        }
    }

    TEST(TableLayoutTest, AShortPhoneHandKeepsItsTilesWhereTheFullOneHasThem) {
        for (const int count : {1, 13}) {
            TableInput input = PhoneInput(QuietInput(0), 70.0f);
            const std::vector<TileSlot> full = SlotsOf(MakeTableLayout(input), SlotRole::HandTile, 0);
            input.concealedCount[0] = count;
            const std::vector<TileSlot> hand = SlotsOf(MakeTableLayout(input), SlotRole::HandTile, 0);
            ASSERT_EQ(static_cast<int>(hand.size()), count);
            for (int i = 0; i < count; ++i) {
                EXPECT_EQ(hand[i].rect.x, full[i].rect.x);
            }
        }
        // more tiles than a hand holds are squeezed into the row, not past it
        TableInput input = PhoneInput(QuietInput(0), kPhoneHandTileMaxW);
        input.concealedCount[0] = 40;
        const Rect bounds = BoundsOf(MakeTableLayout(input), SlotRole::HandTile, 0);
        EXPECT_LE(bounds.x + bounds.w, kViewWidth - kRimWidth - kRackPadding + kTolerance);
    }

    TEST(TableLayoutTest, ThePhoneProfileLeavesTheOpponentsTheWallAndTheCentrePanelWhereTheyAre) {
        for (const float handTileW : kPhoneTileWidths) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableInput desktopInput = WorstInput(viewer, kWorstPile, kNoSeat);
                const TableLayout desktop = MakeTableLayout(desktopInput);
                const TableLayout phone = MakeTableLayout(PhoneInput(desktopInput, handTileW));
                const std::vector<TileSlot> expected = SlotsApartFromTheViewers(desktop);
                const std::vector<TileSlot> slots = SlotsApartFromTheViewers(phone);
                ASSERT_EQ(expected.size(), slots.size());
                for (size_t i = 0; i < expected.size(); ++i) {
                    EXPECT_TRUE(SameRect(expected[i].rect, slots[i].rect)) << "slot " << i << " of seat " << expected[i].seat << " viewer " << viewer;
                }
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    if (seat != viewer) {
                        EXPECT_TRUE(SameRect(desktop.zones.discards[seat], phone.zones.discards[seat])) << "seat " << seat;
                        EXPECT_TRUE(SameRect(desktop.zones.seatPlates[seat], phone.zones.seatPlates[seat])) << "seat " << seat;
                    }
                }
                EXPECT_TRUE(SameRect(desktop.zones.wall, phone.zones.wall));
                EXPECT_TRUE(SameRect(desktop.zones.centerPanel, phone.zones.centerPanel));
            }
        }
    }

    TEST(TableLayoutTest, ThePhoneMeldsAndBonusTilesLieInARowAboveTheHandRightOfTheBottomPlate) {
        for (const float handTileW : kPhoneTileWidths) {
            const TableLayout layout = MakeTableLayout(PhoneInput(WorstInput(2, kWorstPile, kNoSeat), handTileW));
            const Rect hand = BoundsOf(layout, SlotRole::HandTile, 2);
            const Rect melds = BoundsOf(layout, SlotRole::MeldTile, 2);
            const Rect bonus = BoundsOf(layout, SlotRole::BonusTile, 2);
            const Rect& plate = layout.zones.seatPlates[2];
            const Rect& discards = layout.zones.discards[2];
            EXPECT_LE(melds.y + melds.h, hand.y + kTolerance);
            EXPECT_LE(bonus.y + bonus.h, hand.y + kTolerance);
            EXPECT_GE(bonus.x, plate.x + plate.w);
            EXPECT_GE(melds.x, bonus.x + bonus.w);
            EXPECT_LE(discards.y + discards.h, melds.y + kTolerance) << "the discards end above the row";
            EXPECT_LE(discards.y + discards.h, bonus.y + kTolerance);
            EXPECT_GE(discards.y, layout.zones.wall.y + layout.zones.wall.h - kTolerance) << "and start below the wall ring";
            EXPECT_LT(discards.h, MakeTableLayout(WorstInput(2, kWorstPile, kNoSeat)).zones.discards[2].h) << "the lower block is lower than the desktop one";
            EXPECT_LE(plate.y + plate.h, kViewHeight - kRimWidth - kPhoneHandTileMaxW * 4.0f / 3.0f + kTolerance) << "the plate ends where the tallest hand tile starts";
        }
    }

    TEST(TableLayoutTest, ThePhoneDiscardsOfTheViewerRestInTheirLowerBlockAndSqueezeBeforeLeavingIt) {
        for (const int pile : {1, 6, 18, 19, 40, kWorstPile}) {
            const TableLayout layout = MakeTableLayout(PhoneInput(WorstInput(0, pile, 0), kPhoneHandTileMaxW));
            const Rect& zone = layout.zones.discards[0];
            const std::vector<TileSlot> discards = SlotsOf(layout, SlotRole::DiscardTile, 0);
            ASSERT_EQ(static_cast<int>(discards.size()), pile);
            for (const TileSlot& slot : discards) {
                EXPECT_TRUE(Holds(zone, slot.rect)) << "pile " << pile << " tile " << slot.index;
            }
            EXPECT_EQ(discards[0].rect.x, zone.x);
            EXPECT_EQ(discards[0].rect.y, zone.y);
            if (pile > kDiscardsPerRow) {
                EXPECT_LE(discards[kDiscardsPerRow].rect.y - discards[0].rect.y, discards[0].rect.h + kTolerance) << "a row pitch is at most a tile's height";
            }
        }
        // three rows lie as the desktop block's rows do, without overlap: the block is as high as its three rows
        const std::vector<TileSlot> three = SlotsOf(MakeTableLayout(PhoneInput(WorstInput(0, 18, 0), kPhoneHandTileMaxW)), SlotRole::DiscardTile, 0);
        EXPECT_EQ(three[kDiscardsPerRow].rect.y - three[0].rect.y, three[0].rect.h);
    }

    TEST(TableLayoutTest, ThePhoneTileWidthIsClampedBetweenTheDesktopTileAndTheWidestTheViewHolds) {
        EXPECT_EQ(MakeTableLayout(PhoneInput(QuietInput(0), 500.0f)).profile, (TableProfile{true, kPhoneHandTileMaxW}));
        EXPECT_EQ(MakeTableLayout(PhoneInput(QuietInput(0), 10.0f)).profile, (TableProfile{true, kViewerHandTileW}));
        EXPECT_EQ(MakeTableLayout(PhoneInput(QuietInput(0), std::nanf(""))).profile, (TableProfile{true, kViewerHandTileW}));
        EXPECT_EQ(MakeTableLayout(PhoneInput(QuietInput(0), 70.0f)).profile, (TableProfile{true, 70.0f}));
        ExpectSameSlots(MakeTableLayout(PhoneInput(WorstInput(1, 30, kNoSeat), 500.0f)), MakeTableLayout(PhoneInput(WorstInput(1, 30, kNoSeat), kPhoneHandTileMaxW)));
    }

    TEST(TableLayoutTest, TheShapeOfAViewIsPortraitNarrowWideOrLandscapeByItsAspect) {
        EXPECT_EQ(ShapeOfView(kViewWidth, kViewHeight), TableShape::Landscape);
        EXPECT_EQ(ShapeOfView(1920.0f, 1080.0f), TableShape::Landscape);
        EXPECT_EQ(ShapeOfView(kPortraitViewWidth, kPortraitViewHeight), TableShape::Portrait);
        EXPECT_EQ(ShapeOfView(480.0f, 480.0f), TableShape::Portrait) << "a square is portrait, as `orientation:portrait`";
        EXPECT_EQ(ShapeOfView(480.0f, 479.0f), TableShape::Narrow) << "just under a square is the narrowest landscape";
        EXPECT_EQ(ShapeOfView(390.0f, 844.0f), TableShape::Portrait);
        EXPECT_EQ(ShapeOfView(1024.0f, 768.0f), TableShape::Narrow) << "4:3";
        EXPECT_EQ(ShapeOfView(1200.0f, 800.0f), TableShape::Narrow) << "3:2";
        EXPECT_EQ(ShapeOfView(1280.0f, 1024.0f), TableShape::Narrow) << "5:4";
        EXPECT_EQ(ShapeOfView(kNarrowViewWidth, kNarrowViewHeight), TableShape::Narrow);
        EXPECT_EQ(ShapeOfView(1500.0f, 1000.0f), TableShape::Narrow) << "3:2 again";
        EXPECT_EQ(ShapeOfView(1600.0f, 1000.0f), TableShape::Landscape) << "16:10 is not narrow";
        EXPECT_EQ(ShapeOfView(1999.0f, 1000.0f), TableShape::Landscape);
        EXPECT_EQ(ShapeOfView(2000.0f, 1000.0f), TableShape::Wide) << "the boundary is wide, as `minAspect:2`";
        EXPECT_EQ(ShapeOfView(2560.0f, 1080.0f), TableShape::Wide);
        EXPECT_EQ(ShapeOfView(3200.0f, 900.0f), TableShape::Wide);
        EXPECT_EQ(ShapeOfView(kWideViewWidth, kViewHeight), TableShape::Wide);
        EXPECT_EQ(ShapeOfView(915.0f, 330.0f), TableShape::Wide) << "a landscape phone";
    }

    TEST(TableLayoutTest, AViewThatIsNotPositiveIsLandscape) {
        EXPECT_EQ(ShapeOfView(0.0f, 0.0f), TableShape::Landscape);
        EXPECT_EQ(ShapeOfView(0.0f, 100.0f), TableShape::Landscape);
        EXPECT_EQ(ShapeOfView(100.0f, 0.0f), TableShape::Landscape);
        EXPECT_EQ(ShapeOfView(-5.0f, -1.0f), TableShape::Landscape) << "-1 >= -5, still not a view";
        EXPECT_EQ(ShapeOfView(-100.0f, 50.0f), TableShape::Landscape);
        EXPECT_EQ(ShapeOfView(std::nanf(""), 100.0f), TableShape::Landscape);
    }

    TEST(TableLayoutTest, TheShapeOfAnOutputIsTheShapeOfAViewOfThatSize) {
        const ViewSize outputs[] = {{1280.0f, 720.0f},  {720.0f, 1280.0f},  {480.0f, 480.0f},   {1.0f, 1.0f},      {0.0f, 0.0f},     {390.0f, 844.0f},
                                    {2560.0f, 1080.0f}, {2000.0f, 1000.0f}, {1600.0f, 1000.0f}, {1024.0f, 768.0f}, {1200.0f, 800.0f}};
        for (const ViewSize& output : outputs) {
            EXPECT_EQ(ShapeOfOutput(output.width, output.height), ShapeOfView(output.width, output.height)) << output.width << " x " << output.height;
        }
        EXPECT_EQ(ShapeOfOutput(1.0f, 1.0f), TableShape::Portrait);
        EXPECT_EQ(ShapeOfOutput(0.0f, 0.0f), TableShape::Landscape);
        EXPECT_EQ(ShapeOfOutput(2560.0f, 1080.0f), TableShape::Wide);
        EXPECT_EQ(ShapeOfOutput(1024.0f, 768.0f), TableShape::Narrow);
    }

    TEST(TableLayoutTest, TheDesignOfAShapeIsItsViewSize) {
        EXPECT_EQ(DesignWidthOf(TableShape::Landscape), kViewWidth);
        EXPECT_EQ(DesignHeightOf(TableShape::Landscape), kViewHeight);
        EXPECT_EQ(DesignWidthOf(TableShape::Portrait), 720.0f);
        EXPECT_EQ(DesignHeightOf(TableShape::Portrait), 1280.0f);
        EXPECT_EQ(DesignWidthOf(TableShape::Wide), 1560.0f);
        EXPECT_EQ(DesignHeightOf(TableShape::Wide), kViewHeight);
        EXPECT_EQ(kWideWingWidth, 140.0f);
        EXPECT_EQ(DesignWidthOf(TableShape::Narrow), 1080.0f);
        EXPECT_EQ(DesignHeightOf(TableShape::Narrow), 768.0f);
        EXPECT_EQ(ShapeOfView(DesignWidthOf(TableShape::Narrow), DesignHeightOf(TableShape::Narrow)), TableShape::Narrow);
        for (const TableShape shape : {TableShape::Landscape, TableShape::Portrait, TableShape::Wide, TableShape::Narrow}) {
            EXPECT_EQ(ShapeOfView(DesignWidthOf(shape), DesignHeightOf(shape)), shape) << "each design size maps to its own shape";
        }
        EXPECT_FALSE(IsLandscapeLike(TableShape::Narrow));
        EXPECT_EQ(ShapeOfView(DesignWidthOf(TableShape::Wide), DesignHeightOf(TableShape::Wide)), TableShape::Wide);
        EXPECT_TRUE(IsLandscapeLike(TableShape::Landscape));
        EXPECT_TRUE(IsLandscapeLike(TableShape::Wide));
        EXPECT_FALSE(IsLandscapeLike(TableShape::Portrait));
    }

    TEST(TableLayoutTest, TheLayoutScaleFitsTheDesignOfTheShapeOfTheViewIntoIt) {
        EXPECT_FLOAT_EQ(LayoutScale(kViewWidth, kViewHeight), 1.0f);
        EXPECT_FLOAT_EQ(LayoutScale(640.0f, 360.0f), 0.5f);
        EXPECT_FLOAT_EQ(LayoutScale(1000.0f, 400.0f), 400.0f / 720.0f) << "height limited";
        EXPECT_FLOAT_EQ(LayoutScale(kPortraitViewWidth, kPortraitViewHeight), 1.0f);
        EXPECT_FLOAT_EQ(LayoutScale(360.0f, 640.0f), 0.5f);
        EXPECT_FLOAT_EQ(LayoutScale(390.0f, 844.0f), 390.0f / 720.0f) << "width limited";
        EXPECT_FLOAT_EQ(LayoutScale(800.0f, 800.0f), 800.0f / 1280.0f) << "a square is portrait: the height limits";
        EXPECT_FLOAT_EQ(LayoutScale(kWideViewWidth, kViewHeight), 1.0f);
        EXPECT_FLOAT_EQ(LayoutScale(kNarrowViewWidth, kNarrowViewHeight), 1.0f);
        EXPECT_FLOAT_EQ(LayoutScale(1024.0f, 768.0f), 1024.0f / 1080.0f) << "4:3: the width limits";
        EXPECT_FLOAT_EQ(LayoutScale(1200.0f, 800.0f), 800.0f / 768.0f) << "3:2: the height limits";
        EXPECT_FLOAT_EQ(LayoutScale(640.0f, 480.0f), 640.0f / 1080.0f);
        EXPECT_FLOAT_EQ(LayoutScale(2560.0f, 1080.0f), 1.5f) << "wide: the height limits";
        EXPECT_FLOAT_EQ(LayoutScale(1000.0f, 400.0f), 400.0f / 720.0f);
        EXPECT_FLOAT_EQ(LayoutScale(915.0f, 330.0f), 330.0f / 720.0f) << "a landscape phone is wider than 19.5:9: the height limits";
        EXPECT_FLOAT_EQ(LayoutScale(2000.0f, 1000.0f), 2000.0f / 1560.0f) << "the boundary of the wide view is narrower than 19.5:9: the width limits";
        EXPECT_EQ(LayoutScale(0.0f, 720.0f), 0.0f);
        EXPECT_EQ(LayoutScale(1280.0f, -1.0f), 0.0f);
        EXPECT_EQ(LayoutScale(std::nanf(""), 720.0f), 0.0f);
    }

    // The two shapes.

    namespace {

        TableLayout InPortrait(const TableInput& input) {
            return MakeTableLayout(input, kPortraitViewWidth, kPortraitViewHeight);
        }

        TableLayout InLandscape(const TableInput& input) {
            return MakeTableLayout(input, kViewWidth, kViewHeight);
        }

        const TableInput kAllTables[] = {
            WorstInput(0, kWorstPile, kNoSeat), WorstInput(0, kWorstPile, 0), WorstInput(0, kWorstPile, 1), WorstInput(0, kWorstPile, 2), WorstInput(0, kWorstPile, 3), BusyInput(0), QuietInput(0)};

        TableInput WithViewer(TableInput input, Seat viewer) {
            input.viewer = viewer;
            return input;
        }

    }  // namespace

    TEST(TableLayoutTest, TheLayoutRecordsTheShapeOfItsView) {
        for (const ViewSize& size : kViewSizes) {
            EXPECT_EQ(MakeTableLayout(QuietInput(0), size.width, size.height).shape, ShapeOfView(size.width, size.height)) << size.width << "x" << size.height;
        }
        EXPECT_EQ(InLandscape(QuietInput(0)).shape, TableShape::Landscape);
        EXPECT_EQ(InPortrait(QuietInput(0)).shape, TableShape::Portrait);
        EXPECT_EQ(MakeTableLayout(QuietInput(0), 480.0f, 480.0f).shape, TableShape::Portrait);
        EXPECT_EQ(MakeTableLayout(QuietInput(0), 0.0f, 0.0f).shape, TableShape::Landscape);
    }

    TEST(TableLayoutTest, TheWideLayoutIsTheLandscapeOneShiftedByTheWingAtTheScaleOfTheView) {
        const auto expectShifted = [](const TableInput& input, float width, float height) {
            const TableShape shape = ShapeOfView(width, height);
            ASSERT_EQ(shape, TableShape::Wide) << width << "x" << height;
            const TableLayout landscape = InLandscape(input);
            const TableLayout wide = MakeTableLayout(input, width, height);
            const float scale = LayoutScale(width, height);
            const float offsetX = (width - kWideViewWidth * scale) / 2.0f + kWideWingWidth * scale;
            const float offsetY = (height - kViewHeight * scale) / 2.0f;
            const auto expectRect = [&](const Rect& actual, const Rect& expected, const char* what) {
                EXPECT_NEAR(actual.x, offsetX + expected.x * scale, kTolerance) << what << " " << width << "x" << height;
                EXPECT_NEAR(actual.y, offsetY + expected.y * scale, kTolerance) << what << " " << width << "x" << height;
                EXPECT_NEAR(actual.w, expected.w * scale, kTolerance) << what << " " << width << "x" << height;
                EXPECT_NEAR(actual.h, expected.h * scale, kTolerance) << what << " " << width << "x" << height;
            };
            EXPECT_EQ(wide.shape, TableShape::Wide);
            EXPECT_EQ(wide.profile, landscape.profile);
            ASSERT_EQ(wide.slots.size(), landscape.slots.size());
            for (size_t i = 0; i < wide.slots.size(); ++i) {
                expectRect(wide.slots[i].rect, landscape.slots[i].rect, "slot");
                EXPECT_EQ(wide.slots[i].role, landscape.slots[i].role);
                EXPECT_EQ(wide.slots[i].seat, landscape.slots[i].seat);
                EXPECT_EQ(wide.slots[i].index, landscape.slots[i].index);
                EXPECT_EQ(wide.slots[i].part, landscape.slots[i].part);
                EXPECT_EQ(wide.slots[i].claimEdge, landscape.slots[i].claimEdge);
                EXPECT_EQ(wide.slots[i].rotation, landscape.slots[i].rotation);
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                expectRect(wide.zones.discards[seat], landscape.zones.discards[seat], "discards");
                expectRect(wide.zones.seatPlates[seat], landscape.zones.seatPlates[seat], "plate");
            }
            expectRect(wide.zones.wall, landscape.zones.wall, "wall");
            expectRect(wide.zones.centerPanel, landscape.zones.centerPanel, "panel");
        };
        const ViewSize wideViews[] = {{kWideViewWidth, kViewHeight}, {2560.0f, 1080.0f}, {3200.0f, 900.0f}, {2000.0f, 1000.0f}, {915.0f, 330.0f}};
        for (const ViewSize& size : wideViews) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                for (const TableInput& table : kAllTables) {
                    expectShifted(WithViewer(table, viewer), size.width, size.height);
                    expectShifted(PhoneInput(WithViewer(table, viewer), 60.0f), size.width, size.height);
                    expectShifted(PhoneInput(WithViewer(table, viewer), kPhoneHandTileMaxW), size.width, size.height);
                }
            }
        }
    }

    TEST(TableLayoutTest, TheWideTableIsCentredOnItsViewAWingFromTheLeftEdge) {
        const TableLayout layout = MakeTableLayout(QuietInput(0), kWideViewWidth, kViewHeight);
        EXPECT_NEAR(RectCentreX(layout.zones.wall), kWideViewWidth / 2.0f, kTolerance);
        EXPECT_NEAR(RectCentreX(layout.zones.centerPanel), kWideViewWidth / 2.0f, kTolerance);
        for (const TileSlot& slot : layout.slots) {
            EXPECT_GE(slot.rect.x, kWideWingWidth - kTolerance) << "no tile in the wing";
            EXPECT_LE(slot.rect.x + slot.rect.w, kWideWingWidth + kViewWidth + kTolerance);
        }
    }

    TEST(TableLayoutTest, TheWideViewHoldsThePhoneProfileLikeTheLandscapeOne) {
        TableInput input = WorstInput(0, kWorstPile, kNoSeat);
        input.profile = TableProfile{true, 60.0f};
        const TableLayout wide = MakeTableLayout(input, kWideViewWidth, kViewHeight);
        EXPECT_TRUE(wide.profile.phone);
        EXPECT_EQ(wide.profile.handTileW, 60.0f);
        const Rect hand = BoundsOf(wide, SlotRole::HandTile, 0);
        EXPECT_NEAR(hand.y + hand.h, kViewHeight - kRimWidth, kTolerance);
        const Rect landscapeHand = BoundsOf(MakeTableLayout(input, kViewWidth, kViewHeight), SlotRole::HandTile, 0);
        EXPECT_NEAR(hand.x, landscapeHand.x + kWideWingWidth, kTolerance);
        EXPECT_NEAR(hand.w, landscapeHand.w, kTolerance);
        EXPECT_LE(hand.x + hand.w, kWideWingWidth + kViewWidth);
    }

    // The narrow view: the landscape table's middle and side columns, the viewer's side in rows of its own.

    namespace {

        TableLayout InNarrow(const TableInput& input) {
            return MakeTableLayout(input, kNarrowViewWidth, kNarrowViewHeight);
        }

        TableInput WithPhoneProfile(TableInput input) {
            input.profile = TableProfile{true, 60.0f};
            return input;
        }

    }  // namespace

    TEST(TableLayoutTest, TheNarrowLayoutRecordsItsShapeAndIgnoresTheProfileOfThePhone) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const TableInput& table : kAllTables) {
                const TableInput desktop = WithViewer(table, viewer);
                const TableLayout expected = InNarrow(desktop);
                EXPECT_EQ(expected.shape, TableShape::Narrow);
                EXPECT_EQ(expected.profile, TableProfile{});
                ExpectSameSlots(expected, InNarrow(WithPhoneProfile(desktop)));
                ExpectSameSlots(expected, InNarrow(PhoneInput(desktop, kPhoneHandTileMaxW)));
                EXPECT_EQ(InNarrow(WithPhoneProfile(desktop)).profile, TableProfile{});
                EXPECT_TRUE(SameRect(expected.zones.seatPlates[viewer], InNarrow(WithPhoneProfile(desktop)).zones.seatPlates[viewer]));
                // a window of the shape, not only its design
                EXPECT_EQ(MakeTableLayout(WithPhoneProfile(desktop), 1024.0f, 768.0f).profile, TableProfile{});
            }
        }
    }

    TEST(TableLayoutTest, TheNarrowViewersHandIsOneRowOfFourteenTilesOfTheDesktopSizeInsideTheRackAndTheRimAndHitsByItsIndex) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = InNarrow(WorstInput(viewer, kWorstPile, kNoSeat));
            const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, viewer);
            ASSERT_EQ(static_cast<int>(hand.size()), kMaxConcealedTiles);
            for (int i = 0; i < kMaxConcealedTiles; ++i) {
                EXPECT_EQ(hand[i].rect.w, kViewerHandTileW);
                EXPECT_EQ(hand[i].rect.h, kViewerHandTileH);
                EXPECT_EQ(hand[i].rect.y, hand[0].rect.y);
                EXPECT_EQ(hand[i].rotation, 0.0f);
                if (i > 0) {
                    EXPECT_GE(hand[i].rect.x - (hand[i - 1].rect.x + hand[i - 1].rect.w), -kTolerance) << "tiles overlap";
                }
                EXPECT_EQ(HitHandTile(layout, RectCentreX(hand[i].rect), RectCentreY(hand[i].rect)), i);
            }
            const Rect bounds = BoundsOf(layout, SlotRole::HandTile, viewer);
            EXPECT_NEAR(bounds.y + bounds.h, kNarrowViewHeight - kRimWidth, kTolerance) << "on the rim";
            EXPECT_NEAR(RectCentreX(bounds), kNarrowViewWidth / 2.0f, kTolerance) << "centred";
            EXPECT_GE(bounds.x, kRimWidth + kRackPadding);
            EXPECT_LE(bounds.x + bounds.w, kNarrowViewWidth - kRimWidth - kRackPadding + kTolerance);
        }
    }

    TEST(TableLayoutTest, TheNarrowBonusTilesAndMeldsLieInOneRowAboveTheHandClearOfASelectedTileTheBottomPlateAndTheDiscardBlock) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = InNarrow(WorstInput(viewer, kWorstPile, kNoSeat));
            const Rect hand = BoundsOf(layout, SlotRole::HandTile, viewer);
            const Rect bonus = BoundsOf(layout, SlotRole::BonusTile, viewer);
            const Rect melds = BoundsOf(layout, SlotRole::MeldTile, viewer);
            const Rect& plate = layout.zones.seatPlates[viewer];
            const Rect& discards = layout.zones.discards[viewer];
            EXPECT_LE(bonus.y + bonus.h, hand.y - kSelectedTileRise + kTolerance) << "viewer " << viewer;
            EXPECT_LE(melds.y + melds.h, hand.y - kSelectedTileRise + kTolerance);
            EXPECT_NEAR(bonus.y + bonus.h, melds.y + melds.h, kTolerance) << "the two share the bottom line of the row";
            EXPECT_LE(bonus.x + bonus.w, melds.x) << "the bonus tiles are at the left of the melds";
            EXPECT_GE(bonus.x, kRimWidth);
            EXPECT_LE(melds.x + melds.w, kNarrowViewWidth - kRimWidth + kTolerance);
            EXPECT_GE(std::min(bonus.y, melds.y), plate.y + plate.h + kPlatePadding - kTolerance) << "clear of the bottom plate";
            EXPECT_GE(std::min(bonus.y, melds.y), discards.y + discards.h - kTolerance) << "under the discard block";
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::HandTile && slot.seat == viewer) {
                    const Rect raised = RaisedRect(slot.rect);
                    EXPECT_FALSE(Overlaps(raised, bonus)) << "tile " << slot.index;
                    EXPECT_FALSE(Overlaps(raised, melds)) << "tile " << slot.index;
                }
            }
            EXPECT_FALSE(Overlaps(plate, hand));
            EXPECT_FALSE(Overlaps(plate, bonus));
            EXPECT_FALSE(Overlaps(plate, melds));
        }
    }

    TEST(TableLayoutTest, TheNarrowMiddleOfTheTableIsTheLandscapeOneCentredInTheNarrowerView) {
        TableInput input = BusyInput(1);
        input.wallRemaining = kWorstWall;
        const TableLayout landscape = InLandscape(input);
        const TableLayout narrow = InNarrow(input);
        const float shiftX = (kNarrowViewWidth - kViewWidth) / 2.0f;
        for (const Seat seat : {static_cast<Seat>(0), static_cast<Seat>(2), static_cast<Seat>(1), static_cast<Seat>(3)}) {
            const int position = SeatPosition(input.viewer, seat);
            const Rect& before = landscape.zones.discards[seat];
            const Rect& after = narrow.zones.discards[seat];
            EXPECT_EQ(after.w, before.w);
            EXPECT_EQ(after.h, before.h);
            if (position == 0 || position == 2) {
                EXPECT_NEAR(after.x, before.x + shiftX, kTolerance) << "the top and the bottom block stay in the middle";
                EXPECT_NEAR(after.y, before.y, kTolerance);
            }
        }
        EXPECT_NEAR(narrow.zones.wall.x, landscape.zones.wall.x + shiftX, kTolerance);
        EXPECT_NEAR(narrow.zones.wall.y, landscape.zones.wall.y, kTolerance);
        EXPECT_EQ(narrow.zones.wall.w, landscape.zones.wall.w);
        EXPECT_EQ(narrow.zones.wall.h, landscape.zones.wall.h);
        EXPECT_NEAR(RectCentreX(narrow.zones.wall), kNarrowViewWidth / 2.0f, kTolerance);
        const std::vector<TileSlot> landscapeWall = SlotsOf(landscape, SlotRole::WallStack, 0);
        const std::vector<TileSlot> narrowWall = SlotsOf(narrow, SlotRole::WallStack, 0);
        ASSERT_EQ(landscapeWall.size(), narrowWall.size());
        for (size_t i = 0; i < narrowWall.size(); ++i) {
            EXPECT_EQ(narrowWall[i].index, landscapeWall[i].index);
            EXPECT_NEAR(narrowWall[i].rect.x, landscapeWall[i].rect.x + shiftX, kTolerance) << "cell " << i;
            EXPECT_NEAR(narrowWall[i].rect.y, landscapeWall[i].rect.y, kTolerance) << "cell " << i;
        }
        const Rect& panel = narrow.zones.centerPanel;
        const Rect& wall = narrow.zones.wall;
        const float stack = TileSizeOf(narrow, SlotRole::WallStack, 0).w;
        EXPECT_NEAR(panel.x, wall.x + stack, kTolerance);
        EXPECT_NEAR(panel.y, wall.y + stack, kTolerance);
        EXPECT_NEAR(panel.w, wall.w - 2.0f * stack, kTolerance);
        EXPECT_NEAR(panel.h, wall.h - 2.0f * stack, kTolerance);
    }

    TEST(TableLayoutTest, TheNarrowSideSeatsKeepTheirTurnsAndTheirColumnsAtTheEdgesOfTheView) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = InNarrow(WorstInput(viewer, kWorstPile, kNoSeat));
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::WallStack) {
                    EXPECT_EQ(slot.rotation, 0.0f);
                    continue;
                }
                const int position = SeatPosition(viewer, slot.seat);
                const float expected = position == 3 ? kLeftSeatRotation : position == 1 ? kRightSeatRotation : 0.0f;
                EXPECT_EQ(slot.rotation, expected) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
            }
            const Rect left = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 3));
            const Rect right = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 1));
            EXPECT_NEAR(left.x, kRimWidth, kTolerance);
            EXPECT_NEAR(right.x + right.w, kNarrowViewWidth - kRimWidth, kTolerance);
            EXPECT_NEAR(RectCentreY(left), kNarrowViewHeight / 2.0f, kTolerance);
            EXPECT_NEAR(RectCentreY(right), kNarrowViewHeight / 2.0f, kTolerance);
        }
    }

    TEST(TableLayoutTest, TheNarrowTableFitsEveryZoneAndEveryPlateForEveryViewerAndEveryTable) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const TableInput& table : kAllTables) {
                const TableLayout layout = InNarrow(WithViewer(table, viewer));
                ASSERT_NO_FATAL_FAILURE(ExpectZonesAreSound(layout)) << "viewer " << viewer;
                ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(layout, 1.0f)) << "viewer " << viewer;
                ASSERT_NO_FATAL_FAILURE(ExpectSlotsInsideTheView(layout));
                ASSERT_NO_FATAL_FAILURE(ExpectNoZoneOverlaps(layout));
            }
        }
    }

    TEST(TableLayoutTest, EachSeatIsInItsZoneOfThePortraitView) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = InPortrait(QuietInput(viewer));
            const Rect bottom = BoundsOf(layout, SlotRole::HandTile, viewer);
            const Rect right = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 1));
            const Rect top = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 2));
            const Rect left = BoundsOf(layout, SlotRole::HandTile, SeatAtPosition(viewer, 3));
            EXPECT_GT(bottom.y, kPortraitViewHeight * 0.9f) << "viewer " << viewer;
            EXPECT_LT(top.y + top.h, kPortraitViewHeight * 0.07f);
            EXPECT_GE(top.y, kPortraitViewHeight * 0.02f) << "the top seat keeps a margin for a phone's status bar";
            EXPECT_GT(right.x, kPortraitViewWidth * 0.9f);
            EXPECT_LT(left.x + left.w, kPortraitViewWidth * 0.1f);
            EXPECT_GT(right.h, right.w);
            EXPECT_GT(left.h, left.w);
            EXPECT_GT(top.w, top.h);
            EXPECT_GT(bottom.w, bottom.h);
        }
    }

    TEST(TableLayoutTest, ThePortraitHandIsOneRowOfFourteenTilesOfTheDesktopSizeRestingOnTheRimInsideTheRack) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = InPortrait(WorstInput(viewer, kWorstPile, kNoSeat));
            const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, viewer);
            ASSERT_EQ(static_cast<int>(hand.size()), kMaxConcealedTiles);
            for (int i = 0; i < kMaxConcealedTiles; ++i) {
                EXPECT_EQ(hand[i].index, i);
                EXPECT_EQ(hand[i].rect.w, kViewerHandTileW);
                EXPECT_EQ(hand[i].rect.h, kViewerHandTileH);
                EXPECT_EQ(hand[i].rect.y, hand[0].rect.y);
                EXPECT_EQ(hand[i].rotation, 0.0f);
                if (i > 0) {
                    EXPECT_GE(hand[i].rect.x - (hand[i - 1].rect.x + hand[i - 1].rect.w), -kTolerance) << "tiles overlap";
                    EXPECT_LE(hand[i].rect.x - (hand[i - 1].rect.x + hand[i - 1].rect.w), 4.0f) << "a spread row, not a loose one";
                }
                EXPECT_EQ(HitHandTile(layout, RectCentreX(hand[i].rect), RectCentreY(hand[i].rect)), i);
            }
            const Rect bounds = BoundsOf(layout, SlotRole::HandTile, viewer);
            EXPECT_NEAR(bounds.x, kRimWidth + kRackPadding, kTolerance) << "across the view";
            EXPECT_NEAR(bounds.x + bounds.w, kPortraitViewWidth - kRimWidth - kRackPadding, kTolerance);
            EXPECT_NEAR(bounds.y + bounds.h, kPortraitViewHeight - kRimWidth, kTolerance) << "on the rim";
        }
        // a short hand keeps its tiles where the full one has them; more tiles are squeezed into the row
        TableInput input = QuietInput(0);
        const std::vector<TileSlot> full = SlotsOf(InPortrait(input), SlotRole::HandTile, 0);
        input.concealedCount[0] = 13;
        const std::vector<TileSlot> thirteen = SlotsOf(InPortrait(input), SlotRole::HandTile, 0);
        ASSERT_EQ(thirteen.size(), 13u);
        for (size_t i = 0; i < thirteen.size(); ++i) {
            EXPECT_EQ(thirteen[i].rect.x, full[i].rect.x);
        }
        input.concealedCount[0] = 40;
        const Rect squeezed = BoundsOf(InPortrait(input), SlotRole::HandTile, 0);
        EXPECT_LE(squeezed.x + squeezed.w, kPortraitViewWidth - kRimWidth - kRackPadding + kTolerance);
    }

    TEST(TableLayoutTest, ThePortraitMeldsAndBonusTilesLieInRowsAboveTheHandClearOfASelectedTileAndOfTheBottomPlate) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = InPortrait(WorstInput(viewer, kWorstPile, kNoSeat));
            const Rect hand = BoundsOf(layout, SlotRole::HandTile, viewer);
            const Rect melds = BoundsOf(layout, SlotRole::MeldTile, viewer);
            const Rect bonus = BoundsOf(layout, SlotRole::BonusTile, viewer);
            const Rect& plate = layout.zones.seatPlates[viewer];
            const Rect raised = RaisedRect(hand);
            EXPECT_LE(melds.y + melds.h, raised.y - kTolerance) << "the melds end above a raised tile";
            EXPECT_LE(bonus.y + bonus.h, melds.y + kTolerance) << "the bonus row lies above the melds";
            EXPECT_LE(plate.y + plate.h, bonus.y + kTolerance) << "the plate of the viewer ends above its rows";
            EXPECT_GE(melds.x, kRimWidth - kTolerance);
            EXPECT_LE(melds.x + melds.w, kPortraitViewWidth - kRimWidth + kTolerance);
            EXPECT_LE(bonus.x + bonus.w, kPortraitViewWidth - kRimWidth + kTolerance);
            const Rect& discards = layout.zones.discards[viewer];
            EXPECT_LE(discards.y + discards.h, plate.y - kTolerance) << "the viewer's discards end above its plate";
            EXPECT_LT(layout.zones.wall.y + layout.zones.wall.h, discards.y) << "and start under the wall ring";
        }
    }

    TEST(TableLayoutTest, ThePortraitLayoutIgnoresTheProfileOfThePhone) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableInput desktop = WorstInput(viewer, kWorstPile, kNoSeat);
            TableInput phone = desktop;
            phone.profile = TableProfile{true, kPhoneHandTileMaxW};
            const TableLayout expected = InPortrait(desktop);
            const TableLayout layout = InPortrait(phone);
            EXPECT_EQ(layout.profile, TableProfile{});
            ExpectSameSlots(expected, layout);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_TRUE(SameRect(expected.zones.seatPlates[seat], layout.zones.seatPlates[seat]));
                EXPECT_TRUE(SameRect(expected.zones.discards[seat], layout.zones.discards[seat]));
            }
            // in a landscape view the same profile is what it always was
            EXPECT_TRUE(InLandscape(phone).profile.phone);
        }
    }

    TEST(TableLayoutTest, EveryZoneAndPlateOfThePortraitTableIsSoundForEveryViewerAndEveryTable) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const TableInput& table : kAllTables) {
                const TableLayout layout = InPortrait(WithViewer(table, viewer));
                ASSERT_NO_FATAL_FAILURE(ExpectZonesAreSound(layout)) << "viewer " << viewer;
                ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(layout, 1.0f)) << "viewer " << viewer;
                ASSERT_NO_FATAL_FAILURE(ExpectSlotsInsideTheView(layout));
                ASSERT_NO_FATAL_FAILURE(ExpectNoZoneOverlaps(layout));
            }
        }
    }

    TEST(TableLayoutTest, ThePortraitSeatPlatesAreOnTheGridAndLieAtTheScreenPositionOfTheirSeat) {
        const TableLayout seat0 = InPortrait(QuietInput(0));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const Rect& plate = seat0.zones.seatPlates[seat];
            EXPECT_EQ(std::fmod(plate.x, static_cast<float>(kGridUnit)), 0.0f) << "seat " << seat;
            EXPECT_EQ(std::fmod(plate.y, static_cast<float>(kGridUnit)), 0.0f) << "seat " << seat;
            EXPECT_EQ(plate.w, kSeatPlateW);
            EXPECT_EQ(plate.h, kSeatPlateH);
        }
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableLayout layout = InPortrait(QuietInput(viewer));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_TRUE(SameRect(layout.zones.seatPlates[seat], seat0.zones.seatPlates[SeatPosition(viewer, seat)])) << "viewer " << viewer << " seat " << seat;
                EXPECT_TRUE(SameRect(layout.zones.discards[seat], seat0.zones.discards[SeatPosition(viewer, seat)])) << "viewer " << viewer << " seat " << seat;
            }
        }
        const auto& plates = seat0.zones.seatPlates;
        EXPECT_GT(plates[0].y, plates[1].y);
        EXPECT_LT(plates[2].y, plates[0].y);
        EXPECT_LT(plates[3].x, plates[1].x);
    }

    TEST(TableLayoutTest, ThePortraitCenterPanelIsTheInsideOfTheWallRingAndTheSameForEveryViewer) {
        TableInput input = QuietInput(0);
        input.wallRemaining = kWorstWall;
        const TableLayout design = InPortrait(input);
        const Rect& wall = design.zones.wall;
        const Rect& panel = design.zones.centerPanel;
        const float stack = ExpectOneTileSize(design, SlotRole::WallStack, 0).w;
        EXPECT_NEAR(panel.x, wall.x + stack, kTolerance);
        EXPECT_NEAR(panel.y, wall.y + stack, kTolerance);
        EXPECT_NEAR(panel.w, wall.w - 2.0f * stack, kTolerance);
        EXPECT_NEAR(panel.h, wall.h - 2.0f * stack, kTolerance);
        EXPECT_NEAR(wall.w, stack * kWallRingColumns, kTolerance);
        EXPECT_NEAR(wall.h, stack * kWallRingRows, kTolerance);
        EXPECT_NEAR(RectCentreX(wall), kPortraitViewWidth / 2.0f, kTolerance);
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            EXPECT_TRUE(SameRect(InPortrait(QuietInput(viewer)).zones.centerPanel, panel));
        }
    }

    TEST(TableLayoutTest, ThePortraitWallRingHasTheCellsAndTheOrderOfTheLandscapeOne) {
        TableInput input = QuietInput(1);
        input.wallRemaining = kWorstWall;
        const std::vector<TileSlot> landscape = SlotsOf(InLandscape(input), SlotRole::WallStack, 0);
        const std::vector<TileSlot> portrait = SlotsOf(InPortrait(input), SlotRole::WallStack, 0);
        ASSERT_EQ(landscape.size(), portrait.size());
        ASSERT_EQ(static_cast<int>(portrait.size()), kWallRingCells);
        const float shiftX = portrait[0].rect.x - landscape[0].rect.x;
        const float shiftY = portrait[0].rect.y - landscape[0].rect.y;
        for (size_t i = 0; i < portrait.size(); ++i) {
            EXPECT_EQ(portrait[i].index, landscape[i].index);
            EXPECT_NEAR(portrait[i].rect.x - landscape[i].rect.x, shiftX, kTolerance) << "cell " << i;
            EXPECT_NEAR(portrait[i].rect.y - landscape[i].rect.y, shiftY, kTolerance) << "cell " << i;
            EXPECT_EQ(portrait[i].rect.w, landscape[i].rect.w) << "a stack is as large as in the landscape ring";
        }
    }

    TEST(TableLayoutTest, TheOpponentsHandsAreNoSmallerInPortraitTheirOtherTilesKeepTheirSizesAndTheViewersDiscardsAreLarger) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableInput input = WorstInput(viewer, 60, kNoSeat);
            const TableLayout landscape = InLandscape(input);
            const TableLayout portrait = InPortrait(input);
            ASSERT_EQ(landscape.slots.size(), portrait.slots.size());
            for (size_t i = 0; i < portrait.slots.size(); ++i) {
                const TileSlot& a = landscape.slots[i];
                const TileSlot& b = portrait.slots[i];
                ASSERT_EQ(a.role, b.role);
                ASSERT_EQ(a.seat, b.seat);
                EXPECT_EQ(a.rotation, b.rotation) << "slot " << i;
                EXPECT_EQ(a.claimEdge, b.claimEdge);
                const bool viewers = a.role != SlotRole::WallStack && a.seat == viewer;
                if (viewers && (a.role == SlotRole::HandTile || a.role == SlotRole::MeldTile || a.role == SlotRole::BonusTile)) {
                    EXPECT_EQ(a.rect.w, b.rect.w) << "the viewer's rows keep the size of the landscape table";
                } else if (viewers) {
                    EXPECT_GT(b.rect.w, a.rect.w) << "the viewer's discards are larger";
                    EXPECT_GT(b.rect.h, a.rect.h);
                    ExpectAtlasProportions(TileSize{b.rect.w, b.rect.h});
                } else if (a.role == SlotRole::HandTile) {
                    EXPECT_GE(b.rect.w, a.rect.w) << "an opponent's hand is no smaller in portrait, slot " << i;
                    EXPECT_GE(b.rect.h, a.rect.h);
                } else {
                    EXPECT_EQ(a.rect.w, b.rect.w) << "slot " << i << " role " << static_cast<int>(a.role) << " seat " << a.seat;
                    EXPECT_EQ(a.rect.h, b.rect.h);
                }
            }
        }
    }

    TEST(TableLayoutTest, ThePortraitDiscardsOfTheViewerLieInRowsAndSqueezeBeforeLeavingTheirBlock) {
        for (const int pile : {1, 6, 7, 30, 36, 37, kWorstPile}) {
            const TableLayout layout = InPortrait(WorstInput(0, pile, 0));
            const Rect& zone = layout.zones.discards[0];
            const std::vector<TileSlot> discards = SlotsOf(layout, SlotRole::DiscardTile, 0);
            ASSERT_EQ(static_cast<int>(discards.size()), pile);
            const float tileW = discards[0].rect.w;
            const float tileH = discards[0].rect.h;
            EXPECT_EQ(discards[0].rect.x, zone.x);
            EXPECT_EQ(discards[0].rect.y, zone.y);
            EXPECT_EQ(zone.w, tileW * kDiscardsPerRow);
            for (const TileSlot& slot : discards) {
                EXPECT_TRUE(Holds(zone, slot.rect)) << "pile " << pile << " tile " << slot.index;
                EXPECT_EQ(slot.rect.x, zone.x + static_cast<float>(slot.index % kDiscardsPerRow) * tileW);
            }
            if (pile > kDiscardsPerRow) {
                EXPECT_EQ(discards[kDiscardsPerRow].rect.y, discards[0].rect.y + std::min(tileH, (zone.h - tileH) / static_cast<float>((pile + kDiscardsPerRow - 1) / kDiscardsPerRow - 1)));
            }
        }
    }

    TEST(TableLayoutTest, TheClaimedTilesOfAnOpenMeldAndTheirEdgesAreTheSameInBothShapes) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (Seat owner = 0; owner < kSeatCount; ++owner) {
                for (const MeldKind kind : {MeldKind::Pung, MeldKind::Kong}) {
                    for (int offset = 1; offset < kSeatCount; ++offset) {
                        TableInput input = QuietInput(viewer);
                        input.melds[owner] = {MakeMeld(MeldKind::Pung), ClaimedMeld(kind, (owner + offset) % kSeatCount, "5p", "5p")};
                        const std::vector<std::pair<int, Edge>> claimed = ClaimedTiles(InPortrait(input), owner, 1);
                        ASSERT_EQ(claimed.size(), 1u) << "viewer " << viewer << " owner " << owner << " offset " << offset;
                        EXPECT_EQ(claimed[0].first, 3 - offset);
                        EXPECT_EQ(claimed[0].second, ExpectedEdge(SeatPosition(viewer, owner), offset)) << "viewer " << viewer << " owner " << owner << " offset " << offset;
                        EXPECT_TRUE(ClaimedTiles(InPortrait(input), owner, 0).empty());
                    }
                }
            }
        }
        for (const ViewSize& size : kViewSizes) {
            TableInput input = QuietInput(2);
            input.melds[0] = {ClaimedMeld(MeldKind::Chow, 3, "4p", "5p")};
            const std::vector<std::pair<int, Edge>> claimed = ClaimedTiles(MakeTableLayout(input, size.width, size.height), 0, 0);
            ASSERT_EQ(claimed.size(), 1u) << size.width << "x" << size.height;
            EXPECT_EQ(claimed[0].first, 1);
            EXPECT_EQ(claimed[0].second, ExpectedEdge(SeatPosition(2, 0), 3));
        }
    }

    TEST(TableLayoutTest, TheLandscapeTableKeepsTheNumbersOfItsDesign) {
        // The numbers of the landscape table as they were before the portrait one: a change to the landscape layout fails here.
        TableInput input = QuietInput(0);
        input.concealedCount = {14, 14, 14, 14};
        input.bonusCount = {1, 1, 1, 1};
        input.wallRemaining = kWorstWall;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            input.melds[seat] = {MakeMeld(MeldKind::Pung)};
            input.discardSeats.push_back(seat);
        }
        for (const TableLayout& layout : {InLandscape(input), MakeTableLayout(input)}) {
            const eastl::array<Rect, 4> discards = {Rect(568, 452, 144, 160), Rect(1006, 288, 160, 144), Rect(568, 92, 144, 160), Rect(114, 288, 160, 144)};
            const eastl::array<Rect, 4> plates = {Rect(84, 532, 152, 80), Rect(1012, 184, 152, 80), Rect(408, 96, 152, 80), Rect(112, 184, 152, 80)};
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_TRUE(SameRect(layout.zones.discards[seat], discards[seat])) << "seat " << seat;
                EXPECT_TRUE(SameRect(layout.zones.seatPlates[seat], plates[seat])) << "seat " << seat;
            }
            EXPECT_TRUE(SameRect(layout.zones.wall, Rect(458, 268, 364, 168)));
            EXPECT_TRUE(SameRect(layout.zones.centerPanel, Rect(472, 282, 336, 140)));
            const eastl::array<Rect, 4> firstHand = {Rect(72, 648, 48, 64), Rect(1235, 164, 37, 28), Rect(444, 8, 32, 42), Rect(8, 164, 37, 28)};
            const eastl::array<Rect, 4> firstMeld = {Rect(764, 672, 30, 40), Rect(1199, 156, 32, 24), Rect(436, 54, 24, 32), Rect(49, 156, 32, 24)};
            const eastl::array<Rect, 4> firstBonus = {Rect(72, 616, 24, 32), Rect(1174, 192, 21, 16), Rect(860, 54, 16, 21), Rect(85, 192, 21, 16)};
            const eastl::array<Rect, 4> firstDiscard = {Rect(568, 452, 24, 32), Rect(1134, 408, 32, 24), Rect(568, 92, 24, 32), Rect(114, 288, 32, 24)};
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_TRUE(SameRect(SlotsOf(layout, SlotRole::HandTile, seat)[0].rect, firstHand[seat])) << "hand of seat " << seat;
                EXPECT_TRUE(SameRect(SlotsOf(layout, SlotRole::MeldTile, seat)[0].rect, firstMeld[seat])) << "melds of seat " << seat;
                EXPECT_TRUE(SameRect(SlotsOf(layout, SlotRole::BonusTile, seat)[0].rect, firstBonus[seat])) << "bonus of seat " << seat;
                EXPECT_TRUE(SameRect(SlotsOf(layout, SlotRole::DiscardTile, seat)[0].rect, firstDiscard[seat])) << "discard of seat " << seat;
            }
            const std::vector<TileSlot> wall = SlotsOf(layout, SlotRole::WallStack, 0);
            EXPECT_TRUE(SameRect(wall.front().rect, Rect(458, 268, 14, 14)));
            EXPECT_TRUE(SameRect(wall.back().rect, Rect(458, 282, 14, 14)));
        }
        // the 14th tile of the viewer's hand
        EXPECT_TRUE(SameRect(SlotsOf(InLandscape(input), SlotRole::HandTile, 0)[13].rect, Rect(696, 648, 48, 64)));
    }

    TEST(TableLayoutTest, TheOtherShapesKeepTheViewersHandTileAndThePlatesOfTheirDesign) {
        // One named golden per shape besides the landscape one: a change to a zone of the file that moves the viewer's hand or a plate fails here.
        TableInput input = QuietInput(0);
        input.concealedCount = {14, 14, 14, 14};
        struct Golden {
            const char* name;
            float width;
            float height;
            Rect hand;
            eastl::array<Rect, 4> plates;
        };
        const Golden goldens[] = {
            {"narrow", kNarrowViewWidth, kNarrowViewHeight, Rect(204, 696, 48, 64), {Rect(84, 532, 152, 80), Rect(808, 184, 152, 80), Rect(308, 96, 152, 80), Rect(112, 184, 152, 80)}},
            {"portrait", kPortraitViewWidth, kPortraitViewHeight, Rect(12, 1208, 48, 64), {Rect(12, 1028, 152, 80), Rect(456, 308, 152, 80), Rect(284, 308, 152, 80), Rect(112, 308, 152, 80)}},
            {"wide",
             kWideViewWidth,
             kViewHeight,
             Rect(72.0f + kWideWingWidth, 648, 48, 64),
             {Rect(84.0f + kWideWingWidth, 532, 152, 80), Rect(1012.0f + kWideWingWidth, 184, 152, 80), Rect(408.0f + kWideWingWidth, 96, 152, 80), Rect(112.0f + kWideWingWidth, 184, 152, 80)}}};
        for (const Golden& golden : goldens) {
            const TableLayout layout = MakeTableLayout(input, golden.width, golden.height);
            EXPECT_TRUE(SameRect(SlotsOf(layout, SlotRole::HandTile, 0)[0].rect, golden.hand)) << golden.name << " hand";
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_TRUE(SameRect(layout.zones.seatPlates[seat], golden.plates[seat])) << golden.name << " plate of seat " << seat;
            }
        }
    }

    TEST(TableLayoutTest, TheSceneClearancesHold) {
        // What the node files of the scene rely on, in the landscape view and, a wing further right, in the wide one (a widget of the scene's wide
        // variant either moves by the wing or lies nearer the edge of the view, so the shifted clearance is the strict one). The numbers are those of the node files; a zone of config/TableLayout.xml
        // that crosses one fails here.
        constexpr float kHintLabelRight = 1000.0f;  // MahjongActions.xml: the Hint label ends here, left of the right seat's discard block
        constexpr float kWaitPanelRight = 432.0f;   // MahjongHints.xml: the WaitPanel ends here, left of the top seat's hand
        constexpr float kMoveLineBottom = 172.0f;   // MahjongHud.xml: the move line reaches over the side columns down to here
        constexpr float kRulesButtonLeft = 996.0f;  // MahjongHudMenu.xml: the Rules button starts here, right of the top seat's bonus row
        for (const float wing : {0.0f, kWideWingWidth}) {
            const float width = kViewWidth + 2.0f * wing;
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, kNoSeat), width, kViewHeight);
                ASSERT_EQ(layout.shape, wing == 0.0f ? TableShape::Landscape : TableShape::Wide);
                const Seat right = SeatAtPosition(viewer, 1);
                const Seat top = SeatAtPosition(viewer, 2);
                const Seat left = SeatAtPosition(viewer, 3);
                EXPECT_GE(layout.zones.discards[right].x, wing + kHintLabelRight) << "MahjongActions.xml Hint label, viewer " << viewer;
                EXPECT_GE(BoundsOf(layout, SlotRole::HandTile, top).x, wing + kWaitPanelRight) << "MahjongHints.xml WaitPanel, viewer " << viewer;
                EXPECT_GE(BoundsOf(layout, SlotRole::BonusTile, right).y, kMoveLineBottom) << "MahjongHud.xml move line, right seat, viewer " << viewer;
                EXPECT_GE(BoundsOf(layout, SlotRole::BonusTile, left).y, kMoveLineBottom) << "MahjongHud.xml move line, left seat, viewer " << viewer;
                const Rect topBonus = BoundsOf(layout, SlotRole::BonusTile, top);
                EXPECT_LE(topBonus.x + topBonus.w, wing + kRulesButtonLeft) << "MahjongHudMenu.xml Rules button, viewer " << viewer;
            }
        }
    }

    TEST(TableLayoutTest, ARebuildIntoAUsedLayoutEqualsANewOneAndKeepsItsStorage) {
        TableLayout reused = MakeTableLayout(WorstInput(2, kWorstPile, 1), kPortraitViewWidth, kPortraitViewHeight);  // more slots than below
        const TileSlot* storage = reused.slots.data();
        const TableInput input = WorstInput(0, 10, 3);

        RebuildTableLayout(reused, input, kViewWidth, kViewHeight);
        const TableLayout fresh = MakeTableLayout(input, kViewWidth, kViewHeight);

        EXPECT_EQ(reused.slots.data(), storage);
        EXPECT_EQ(reused.viewer, fresh.viewer);
        EXPECT_EQ(reused.shape, fresh.shape);
        EXPECT_TRUE(reused.profile == fresh.profile);
        ASSERT_EQ(reused.slots.size(), fresh.slots.size());
        for (size_t i = 0; i < fresh.slots.size(); ++i) {
            const TileSlot& a = reused.slots[i];
            const TileSlot& b = fresh.slots[i];
            EXPECT_TRUE(SameRect(a.rect, b.rect)) << "slot " << i;
            EXPECT_TRUE(a.role == b.role && a.seat == b.seat && a.index == b.index && a.part == b.part && a.claimEdge == b.claimEdge && a.rotation == b.rotation) << "slot " << i;
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_TRUE(SameRect(reused.zones.discards[seat], fresh.zones.discards[seat]));
            EXPECT_TRUE(SameRect(reused.zones.seatPlates[seat], fresh.zones.seatPlates[seat]));
        }
        EXPECT_TRUE(SameRect(reused.zones.wall, fresh.zones.wall));
        EXPECT_TRUE(SameRect(reused.zones.centerPanel, fresh.zones.centerPanel));
    }

}  // namespace BECore::Mahjong::Tests
