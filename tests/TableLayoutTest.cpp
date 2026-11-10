#include <Mahjong/Table/TableLayout.h>
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
        const ViewSize kViewSizes[] = {{kViewWidth, kViewHeight}, {1920.0f, 1080.0f}, {800.0f, 800.0f}, {640.0f, 360.0f}, {1000.0f, 400.0f}};
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
                const std::vector<TileSlot> hand = SlotsOf(MakeTableLayout(input), SlotRole::HandTile, viewer);
                ASSERT_EQ(static_cast<int>(hand.size()), count);
                for (int i = 0; i < count; ++i) {
                    EXPECT_EQ(hand[i].index, i);
                    EXPECT_EQ(hand[i].rect.y, hand[0].rect.y);
                    EXPECT_EQ(hand[i].rect.w, kViewerHandTileW);
                    EXPECT_EQ(hand[i].rect.h, kViewerHandTileH);
                    if (i > 0) {
                        const float gap = hand[i].rect.x - (hand[i - 1].rect.x + hand[i - 1].rect.w);
                        EXPECT_GE(gap, -kTolerance) << "tiles overlap";
                        EXPECT_LE(gap, kViewerHandTileW) << "a gap wider than a tile";
                    }
                }
            }
        }
    }

    TEST(TableLayoutTest, TheOpponentsHandsAreSmallAndAsManyAsTheirConcealedTiles) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            const TableInput input = QuietInput(viewer);
            const TableLayout layout = MakeTableLayout(input);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const std::vector<TileSlot> hand = SlotsOf(layout, SlotRole::HandTile, seat);
                EXPECT_EQ(static_cast<int>(hand.size()), input.concealedCount[seat]);
                const bool turned = IsSideSeat(viewer, seat);
                for (const TileSlot& slot : hand) {
                    EXPECT_EQ(slot.rect.w, seat == viewer ? kViewerHandTileW : turned ? kSideHandTileH : kOpponentHandTileW);
                    EXPECT_EQ(slot.rect.h, seat == viewer ? kViewerHandTileH : turned ? kSideHandTileW : kOpponentHandTileH);
                }
                for (size_t i = 1; i < hand.size(); ++i) {
                    EXPECT_FALSE(Overlaps(hand[i - 1].rect, hand[i].rect));
                }
            }
        }
    }

    TEST(TableLayoutTest, TheWorstTableFitsTheViewAndNoZoneOverlapsAnother) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const Seat discardSeat : {kNoSeat, 0, 1, 2, 3}) {
                const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, discardSeat));
                ExpectSlotsInsideTheView(layout);
                ExpectNoZoneOverlaps(layout);
            }
        }
    }

    TEST(TableLayoutTest, TheWorstTableHasTheSlotsItAsksFor) {
        const TableLayout layout = MakeTableLayout(WorstInput(1, kWorstPile, kNoSeat));
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
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            input.viewer = viewer;
            const TableLayout layout = MakeTableLayout(input);
            ExpectSlotsInsideTheView(layout);
            ExpectNoZoneOverlaps(layout);
            EXPECT_EQ(static_cast<int>(SlotsOf(layout, SlotRole::WallStack, 0).size()), kWallRingCells);
        }
    }

    TEST(TableLayoutTest, EverySlotIsInsideOtherViewSizesToo) {
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, kNoSeat), size.width, size.height);
                EXPECT_EQ(layout.viewWidth, size.width);
                EXPECT_EQ(layout.viewHeight, size.height);
                ExpectSlotsInsideTheView(layout);
                ExpectNoZoneOverlaps(layout);
            }
        }
    }

    TEST(TableLayoutTest, TheZonesHoldTheirSlotsAndOverlapNothingElse) {
        for (const ViewSize& size : kViewSizes) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                for (const Seat discardSeat : {kNoSeat, 0, 1, 2, 3}) {
                    ASSERT_NO_FATAL_FAILURE(ExpectZonesAreSound(MakeTableLayout(WorstInput(viewer, kWorstPile, discardSeat), size.width, size.height)))
                        << "view " << size.width << "x" << size.height << " viewer " << viewer;
                }
                // The zones are there whatever the table holds
                ASSERT_NO_FATAL_FAILURE(ExpectZonesAreSound(MakeTableLayout(QuietInput(viewer), size.width, size.height)));
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

    namespace {

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

    }  // namespace

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
                ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(MakeTableLayout(WorstInput(viewer, kWorstPile, discardSeat)), 1.0f)) << "viewer " << viewer;
            }
            ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(MakeTableLayout(QuietInput(viewer)), 1.0f)) << "viewer " << viewer;
            ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(MakeTableLayout(BusyInput(viewer)), 1.0f)) << "viewer " << viewer;
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
        const TableLayout design = MakeTableLayout(QuietInput(0));
        const Rect& wall = design.zones.wall;
        const Rect& panel = design.zones.centerPanel;
        EXPECT_NEAR(panel.x, wall.x + kWallStackSize, kTolerance);
        EXPECT_NEAR(panel.y, wall.y + kWallStackSize, kTolerance);
        EXPECT_NEAR(panel.w, wall.w - 2.0f * kWallStackSize, kTolerance);
        EXPECT_NEAR(panel.h, wall.h - 2.0f * kWallStackSize, kTolerance);
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
        const TableLayout design = MakeTableLayout(input);
        for (const ViewSize& size : kViewSizes) {
            const TableLayout layout = MakeTableLayout(input, size.width, size.height);
            const float scale = std::min(size.width / kViewWidth, size.height / kViewHeight);
            ASSERT_NO_FATAL_FAILURE(ExpectSeatPlatesAreSound(layout, scale)) << "view " << size.width << "x" << size.height;
            const float offsetX = (size.width - kViewWidth * scale) / 2.0f;
            const float offsetY = (size.height - kViewHeight * scale) / 2.0f;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_NEAR(layout.zones.seatPlates[seat].x, offsetX + design.zones.seatPlates[seat].x * scale, kTolerance) << "seat " << seat;
                EXPECT_NEAR(layout.zones.seatPlates[seat].y, offsetY + design.zones.seatPlates[seat].y * scale, kTolerance) << "seat " << seat;
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
        // A view twice as high keeps the scale of the width and puts the table in the middle.
        const TableLayout tall = MakeTableLayout(input, kViewWidth, 2.0f * kViewHeight);
        ASSERT_EQ(design.slots.size(), tall.slots.size());
        for (size_t i = 0; i < design.slots.size(); ++i) {
            EXPECT_NEAR(tall.slots[i].rect.x, design.slots[i].rect.x, kTolerance);
            EXPECT_NEAR(tall.slots[i].rect.y, design.slots[i].rect.y + kViewHeight / 2.0f, kTolerance);
            EXPECT_NEAR(tall.slots[i].rect.w, design.slots[i].rect.w, kTolerance);
        }
    }

    TEST(TableLayoutTest, DiscardsLieInRowsOfSixInTheOrderDiscarded) {
        TableInput input = QuietInput(0);
        for (int entry = 0; entry < 7; ++entry) {
            input.discardSeats.push_back(2);
        }
        const std::vector<TileSlot> pile = SlotsOf(MakeTableLayout(input), SlotRole::DiscardTile, 2);
        ASSERT_EQ(pile.size(), 7u);
        for (int i = 0; i < kDiscardsPerRow; ++i) {
            EXPECT_EQ(pile[i].rect.y, pile[0].rect.y);
            EXPECT_EQ(pile[i].rect.x, pile[0].rect.x + static_cast<float>(i) * kDiscardTileW);
        }
        // the 7th tile starts a new row, under the first
        EXPECT_EQ(pile[kDiscardsPerRow].rect.x, pile[0].rect.x);
        EXPECT_EQ(pile[kDiscardsPerRow].rect.y, pile[0].rect.y + kDiscardTileH);
        EXPECT_EQ(pile[kDiscardsPerRow].index, kDiscardsPerRow);
    }

    namespace {

        /// Where cell `cell` of a pile of at most `kDiscardRowCapacity` rows lies, written out from how the seat reads it: the bottom and top
        /// seats row by row from the left, the next row below; the left seat down a column, the next column to its right; the right seat up
        /// a column starting at the right-hand edge of the block, the next column to its left.
        Rect ExpectedDiscardRect(const Rect& zone, int position, int cell) {
            const float column = static_cast<float>(cell % kDiscardsPerRow);
            const float row = static_cast<float>(cell / kDiscardsPerRow);
            switch (position) {
                case 3:
                    return Rect(zone.x + row * kDiscardTileH, zone.y + column * kDiscardTileW, kDiscardTileH, kDiscardTileW);
                case 1:
                    return Rect(zone.x + zone.w - (row + 1.0f) * kDiscardTileH, zone.y + zone.h - (column + 1.0f) * kDiscardTileW, kDiscardTileH, kDiscardTileW);
                default:
                    return Rect(zone.x + column * kDiscardTileW, zone.y + row * kDiscardTileH, kDiscardTileW, kDiscardTileH);
            }
        }

        void ExpectDiscardCell(const TableLayout& layout, const TileSlot& slot, int cell) {
            const Rect expected = ExpectedDiscardRect(layout.zones.discards[slot.seat], SeatPosition(layout.viewer, slot.seat), cell);
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
            // the left seat reads top to bottom, the right seat bottom to top
            for (int i = 1; i < kDiscardsPerRow; ++i) {
                EXPECT_EQ(leftPile[i].rect.x, leftPile[0].rect.x) << "viewer " << viewer;
                EXPECT_NEAR(leftPile[i].rect.y - leftPile[i - 1].rect.y, kDiscardTileW, kTolerance);
                EXPECT_EQ(rightPile[i].rect.x, rightPile[0].rect.x);
                EXPECT_NEAR(rightPile[i - 1].rect.y - rightPile[i].rect.y, kDiscardTileW, kTolerance);
            }
            // row 0 is at the outer side, the next row toward the middle of the table
            EXPECT_LT(leftPile[0].rect.x, leftPile[kDiscardsPerRow].rect.x);
            EXPECT_NEAR(leftPile[kDiscardsPerRow].rect.x - leftPile[0].rect.x, kDiscardTileH, kTolerance);
            EXPECT_EQ(leftPile[kDiscardsPerRow].rect.y, leftPile[0].rect.y);
            EXPECT_GT(rightPile[0].rect.x, rightPile[kDiscardsPerRow].rect.x);
            EXPECT_NEAR(rightPile[0].rect.x - rightPile[kDiscardsPerRow].rect.x, kDiscardTileH, kTolerance);
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
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (const int position : {1, 3}) {
                const Seat seat = SeatAtPosition(viewer, position);
                const TableLayout layout = MakeTableLayout(WorstInput(viewer, kWorstPile, seat));
                const std::vector<TileSlot> pile = SlotsOf(layout, SlotRole::DiscardTile, seat);
                const Rect& zone = layout.zones.discards[seat];
                const int rows = (static_cast<int>(pile.size()) + kDiscardsPerRow - 1) / kDiscardsPerRow;
                ASSERT_GT(rows, kDiscardRowCapacity) << "the worst pile is longer than the block holds";
                const float pitch = kDiscardTileH * (kDiscardRowCapacity - 1) / static_cast<float>(rows - 1);
                for (size_t i = 0; i < pile.size(); ++i) {
                    const float row = static_cast<float>(i / kDiscardsPerRow);
                    EXPECT_TRUE(Holds(zone, pile[i].rect)) << "viewer " << viewer << " position " << position << " tile " << i;
                    const float expectedX = position == 3 ? zone.x + row * pitch : zone.x + zone.w - kDiscardTileH - row * pitch;
                    EXPECT_NEAR(pile[i].rect.x, expectedX, kTolerance) << "tile " << i;
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
        const std::vector<TileSlot> melds = SlotsOf(MakeTableLayout(input), SlotRole::MeldTile, 0);
        ASSERT_EQ(melds.size(), 10u);
        const int expectedMeld[] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2};
        const int expectedPart[] = {0, 1, 2, 0, 1, 2, 3, 0, 1, 2};
        for (size_t i = 0; i < melds.size(); ++i) {
            EXPECT_EQ(melds[i].index, expectedMeld[i]);
            EXPECT_EQ(melds[i].part, expectedPart[i]);
            EXPECT_EQ(melds[i].rect.w, kViewerMeldTileW);
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
            for (int i = 0; i < input.bonusCount[seat]; ++i) {
                EXPECT_EQ(bonus[i].index, i);
                const float opponentWidth = IsSideSeat(0, seat) ? kOpponentBonusTileH : kOpponentBonusTileW;
                EXPECT_EQ(bonus[i].rect.w, seat == 0 ? kViewerBonusTileW : opponentWidth);
                EXPECT_LT(bonus[i].rect.w, kViewerHandTileW);
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
        const std::vector<TileSlot> wall = SlotsOf(MakeTableLayout(input), SlotRole::WallStack, 0);
        ASSERT_EQ(static_cast<int>(wall.size()), kWallRingCells);
        for (size_t i = 0; i < wall.size(); ++i) {
            const Rect& a = wall[i].rect;
            const Rect& b = wall[(i + 1) % wall.size()].rect;
            EXPECT_NEAR(std::abs(a.x - b.x) + std::abs(a.y - b.y), kWallStackSize, kTolerance) << "cell " << i;
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
            EXPECT_FALSE(HitHandTile(layout, bounds.x + bounds.w / 2.0f, bounds.y + bounds.h + 1.0f).has_value());
            EXPECT_FALSE(HitHandTile(layout, -10.0f, -10.0f).has_value());
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
            EXPECT_NEAR(hand.x, 72.0f, kTolerance);
            EXPECT_NEAR(hand.x + hand.w, 72.0f + static_cast<float>(kMaxConcealedTiles) * kViewerHandTileW, kTolerance);
            EXPECT_LE(hand.x + hand.w, melds.x + kTolerance);
            const std::vector<TileSlot> tiles = SlotsOf(layout, SlotRole::HandTile, viewer);
            ASSERT_EQ(static_cast<int>(tiles.size()), kMaxConcealedTiles);
            for (size_t i = 1; i < tiles.size(); ++i) {
                EXPECT_NEAR(tiles[i].rect.x - tiles[i - 1].rect.x, kViewerHandTileW, kTolerance) << "tile " << i;
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
        const Rect chow = BoundsOfMeld(layout, 0, 0);
        const Rect kong = BoundsOfMeld(layout, 0, 1);
        EXPECT_NEAR(chow.x, melds[0].rect.x, kTolerance);
        EXPECT_NEAR(chow.w, 3 * kViewerMeldTileW, kTolerance);
        EXPECT_NEAR(chow.h, kViewerMeldTileH, kTolerance);
        EXPECT_NEAR(kong.x, melds[3].rect.x, kTolerance);
        EXPECT_NEAR(kong.w, 4 * kViewerMeldTileW, kTolerance);
        EXPECT_FALSE(Overlaps(chow, kong));
        // the top seat's meld is laid in a row of its own size
        EXPECT_NEAR(BoundsOfMeld(layout, 2, 0).w, 3 * kOpponentMeldTileW, kTolerance);
        // a side seat's meld is a column of turned tiles
        input.melds[1] = {MakeMeld(MeldKind::Pung)};
        const Rect column = BoundsOfMeld(MakeTableLayout(input), 1, 0);
        EXPECT_NEAR(column.h, 3 * kOpponentMeldTileW, kTolerance);
        EXPECT_NEAR(column.w, kOpponentMeldTileH, kTolerance);
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
            for (size_t i = 0; i < hand.size(); ++i) {
                EXPECT_EQ(hand[i].rect.w, kSideHandTileH) << "seat " << seat;
                EXPECT_EQ(hand[i].rect.h, kSideHandTileW);
                EXPECT_EQ(hand[i].rect.x, hand[0].rect.x);
                if (i > 0) {
                    EXPECT_NEAR(hand[i].rect.y - hand[i - 1].rect.y, kSideHandTileW, kTolerance) << "the pitch is the height of the turned tile";
                }
            }
            const std::vector<TileSlot> bonus = SlotsOf(layout, SlotRole::BonusTile, seat);
            ASSERT_EQ(bonus.size(), 3u);
            for (size_t i = 1; i < bonus.size(); ++i) {
                EXPECT_EQ(bonus[i].rect.x, bonus[0].rect.x);
                EXPECT_NEAR(bonus[i].rect.y - bonus[i - 1].rect.y, kOpponentBonusTileW, kTolerance);
            }
            const std::vector<TileSlot> melds = SlotsOf(layout, SlotRole::MeldTile, seat);
            ASSERT_GE(melds.size(), 6u);
            for (size_t i = 1; i < melds.size(); ++i) {
                EXPECT_EQ(melds[i].rect.x, melds[0].rect.x);
                EXPECT_EQ(melds[i].rect.w, kOpponentMeldTileH);
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
        // the block is the block of six by five tiles, turned
        for (const Seat seat : {1, 3}) {
            EXPECT_EQ(layout.zones.discards[seat].w, kDiscardTileH * kDiscardRowCapacity);
            EXPECT_EQ(layout.zones.discards[seat].h, kDiscardTileW * kDiscardsPerRow);
        }
        EXPECT_EQ(layout.zones.discards[0].w, kDiscardTileW * kDiscardsPerRow);
        EXPECT_EQ(layout.zones.discards[2].h, kDiscardTileH * kDiscardRowCapacity);
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
            EXPECT_EQ(upright.w, kSideHandTileW);
            EXPECT_EQ(upright.h, kSideHandTileH);
        }
        for (const TileSlot& slot : SlotsOf(layout, SlotRole::HandTile, 2)) {
            EXPECT_EQ(UprightRect(slot.rect, slot.rotation).x, slot.rect.x);
        }
    }

}  // namespace BECore::Mahjong::Tests
