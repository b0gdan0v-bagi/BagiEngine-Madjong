#include <Mahjong/Table/TableLayout.h>
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
                EXPECT_EQ(a.slots[i].part, b.slots[i].part);
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
            // the side hands are columns of upright tiles, the others rows
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
                for (const TileSlot& slot : hand) {
                    EXPECT_EQ(slot.rect.w, seat == viewer ? kViewerHandTileW : kOpponentHandTileW);
                    EXPECT_EQ(slot.rect.h, seat == viewer ? kViewerHandTileH : kOpponentHandTileH);
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
                EXPECT_EQ(pile[own].rect.x, pile[0].rect.x + static_cast<float>(own % kDiscardsPerRow) * kDiscardTileW);
                EXPECT_EQ(pile[own].rect.y, pile[0].rect.y + static_cast<float>(own / kDiscardsPerRow) * kDiscardTileH);
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
                EXPECT_EQ(bonus[i].rect.w, seat == 0 ? kViewerBonusTileW : kOpponentBonusTileW);
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

}  // namespace BECore::Mahjong::Tests
