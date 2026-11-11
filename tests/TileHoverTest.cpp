#include "ComputerTestSupport.h"

#include <Mahjong/Table/TileHover.h>
#include <gtest/gtest.h>

/**
 * `Table/TileHover.h` (pure logic): the slot under a point (the later of two that overlap, never a wall stack), the tile a slot shows
 * face up (the rule of the draw, so none for a back: the no-peeking rule held against hands with known tiles) and the place of the
 * name plate. The layouts are made from real models through `MakeTableLayout`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr bool kFaceUp = true;
        constexpr bool kFaceDown = false;
        constexpr float kPlateW = 200.0f;
        constexpr float kPlateH = 48.0f;
        constexpr float kTolerance = 0.001f;

        TableInput InputOfModel(const HandModel& hand) {
            TableInput input;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& held = hand.GetHand(seat);
                input.concealedCount[seat] = static_cast<int>(held.concealed.size());
                input.melds[seat] = held.melds;
                input.bonusCount[seat] = static_cast<int>(held.bonus.size());
            }
            for (const DiscardEntry& entry : hand.Discards()) {
                input.discardSeats.push_back(entry.seat);
            }
            input.wallRemaining = static_cast<int>(hand.GetWall().Remaining());
            return input;
        }

        const TileSlot* Find(const TableLayout& layout, SlotRole role, Seat seat, int index, int part = 0) {
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == role && slot.seat == seat && slot.index == index && slot.part == part) {
                    return &slot;
                }
            }
            return nullptr;
        }

        float CentreX(const Rect& rect) {
            return rect.x + rect.w / 2.0f;
        }

        float CentreY(const Rect& rect) {
            return rect.y + rect.h / 2.0f;
        }

        bool Overlaps(const Rect& a, const Rect& b) {
            return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
        }

        bool Inside(const Rect& rect, float viewW, float viewH) {
            return rect.x >= -kTolerance && rect.y >= -kTolerance && rect.x + rect.w <= viewW + kTolerance && rect.y + rect.h <= viewH + kTolerance;
        }

        /// East holds three 7p (no quiet hand holds one) and draws a fourth: a concealed kong is one declaration away.
        HandModel KongModel() {
            return MakeModel(SpecOf("7p 7p 7p 1s 4s 7s 1m 4m 7m 1p 4p Ew Rd", "7p Rd"));
        }

    }  // namespace

    TEST(TileHoverTest, SlotAtFindsTheTileUnderThePointForTheViewerAndForATurnedSideSeat) {
        const HandModel model = MakeModel(SpecOf("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s", "5s"));
        const TableLayout layout = MakeTableLayout(InputOfModel(model));
        const TileSlot* viewerTile = Find(layout, SlotRole::HandTile, kSeat0, 3);
        const TileSlot* sideTile = Find(layout, SlotRole::HandTile, kSeat1, 2);
        ASSERT_NE(viewerTile, nullptr);
        ASSERT_NE(sideTile, nullptr);
        ASSERT_NE(sideTile->rotation, 0.0f) << "the right seat's tiles are turned";
        const TileSlot* hitViewer = SlotAt(layout, CentreX(viewerTile->rect), CentreY(viewerTile->rect));
        ASSERT_NE(hitViewer, nullptr);
        EXPECT_EQ(hitViewer->role, SlotRole::HandTile);
        EXPECT_EQ(hitViewer->seat, kSeat0);
        EXPECT_EQ(hitViewer->index, 3);
        const TileSlot* hitSide = SlotAt(layout, CentreX(sideTile->rect), CentreY(sideTile->rect));
        ASSERT_NE(hitSide, nullptr);
        EXPECT_EQ(hitSide->seat, kSeat1);
        EXPECT_EQ(hitSide->index, 2) << "the screen rectangle of a turned tile is what the point is tested against";
    }

    TEST(TileHoverTest, SlotAtTakesTheLaterSlotWhereTwoOverlap) {
        const HandModel model = MakeModel(SpecOf("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s", "5s"));
        TableLayout layout = MakeTableLayout(InputOfModel(model));
        ASSERT_GE(layout.slots.size(), 2u);
        // Two hand tiles of the layout made to overlap exactly: whatever lies later in the layout is drawn over the other
        const TileSlot* first = Find(layout, SlotRole::HandTile, kSeat0, 0);
        const TileSlot* second = Find(layout, SlotRole::HandTile, kSeat0, 1);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        ASSERT_LT(first, second);
        layout.slots[static_cast<size_t>(second - layout.slots.data())].rect = first->rect;
        const TileSlot* hit = SlotAt(layout, CentreX(first->rect), CentreY(first->rect));
        ASSERT_NE(hit, nullptr);
        EXPECT_EQ(hit->index, 1);
    }

    TEST(TileHoverTest, SlotAtFindsNothingOffTheTilesAndNothingOverAWallStack) {
        const HandModel model = MakeModel(SpecOf("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s", "5s"));
        const TableLayout layout = MakeTableLayout(InputOfModel(model));
        EXPECT_EQ(SlotAt(layout, -10.0f, -10.0f), nullptr);
        EXPECT_EQ(SlotAt(layout, layout.viewWidth + 10.0f, layout.viewHeight + 10.0f), nullptr);
        const TileSlot* stack = nullptr;
        for (const TileSlot& slot : layout.slots) {
            if (slot.role == SlotRole::WallStack) {
                stack = &slot;
                break;
            }
        }
        ASSERT_NE(stack, nullptr);
        const TileSlot* hit = SlotAt(layout, CentreX(stack->rect), CentreY(stack->rect));
        EXPECT_TRUE(hit == nullptr || hit->role != SlotRole::WallStack);
    }

    TEST(TileHoverTest, TileOfSlotNamesTheViewersHandTileAndNoOpponentsWhileTheyAreFaceDown) {
        const HandModel model = MakeModel(SpecOf("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s", "5s"));
        const TableLayout layout = MakeTableLayout(InputOfModel(model));
        const TileSlot* mine = Find(layout, SlotRole::HandTile, kSeat0, 4);
        const TileSlot* theirs = Find(layout, SlotRole::HandTile, kSeat2, 4);
        ASSERT_NE(mine, nullptr);
        ASSERT_NE(theirs, nullptr);
        const std::optional<Tile> shown = TileOfSlot(model, *mine, kFaceUp);
        ASSERT_TRUE(shown.has_value());
        EXPECT_EQ(*shown, model.GetHand(kSeat0).concealed[4]);
        EXPECT_FALSE(TileOfSlot(model, *mine, kFaceDown).has_value()) << "the viewer's hand under the cover";
        EXPECT_FALSE(TileOfSlot(model, *theirs, kFaceDown).has_value());
        const std::optional<Tile> atTheEnd = TileOfSlot(model, *theirs, kFaceUp);
        ASSERT_TRUE(atTheEnd.has_value()) << "at the end of a hand every hand is face up";
        EXPECT_EQ(*atTheEnd, model.GetHand(kSeat2).concealed[4]);
    }

    TEST(TileHoverTest, TileOfSlotNamesAnOpenSetAndADiscardWhateverTheFaces) {
        const HandModel model = OpenPungModel(kSeat1Hand, kSeat2Waits, kSeat3Waits);
        const TableLayout layout = MakeTableLayout(InputOfModel(model));
        ASSERT_FALSE(model.GetHand(kSeat1).melds.empty());
        ASSERT_FALSE(model.Discards().empty());
        const TileSlot* meld = Find(layout, SlotRole::MeldTile, kSeat1, 0, 1);
        ASSERT_NE(meld, nullptr);
        EXPECT_EQ(TileOfSlot(model, *meld, kFaceDown), std::optional<Tile>(T("5p")));
        const int last = static_cast<int>(model.Discards().size()) - 1;
        const TileSlot* pile = Find(layout, SlotRole::DiscardTile, model.Discards()[last].seat, last);
        ASSERT_NE(pile, nullptr);
        EXPECT_EQ(TileOfSlot(model, *pile, kFaceDown), std::optional<Tile>(model.Discards()[last].tile));
    }

    TEST(TileHoverTest, TileOfSlotNamesABonusTile) {
        FixedWallSpec spec = SpecOf("F1 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s", "5s");
        spec.loose = "9m 9s";
        const HandModel model = MakeModel(spec, kSeat0, Wind::East, /*flowers=*/true);
        ASSERT_FALSE(model.GetHand(kSeat0).bonus.empty());
        const TableLayout layout = MakeTableLayout(InputOfModel(model));
        const TileSlot* bonus = Find(layout, SlotRole::BonusTile, kSeat0, 0);
        ASSERT_NE(bonus, nullptr);
        EXPECT_EQ(TileOfSlot(model, *bonus, kFaceDown), std::optional<Tile>(model.GetHand(kSeat0).bonus[0]));
    }

    TEST(TileHoverTest, AConcealedKongShowsOnlyWhileItsHandIsFaceUp) {
        HandModel model = KongModel();
        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T("7p")).has_value());
        ASSERT_EQ(model.GetHand(kSeat0).melds.size(), 1u);
        ASSERT_TRUE(model.GetHand(kSeat0).melds[0].concealed);
        const TableLayout layout = MakeTableLayout(InputOfModel(model));
        const TileSlot* tile = Find(layout, SlotRole::MeldTile, kSeat0, 0, 2);
        ASSERT_NE(tile, nullptr);
        EXPECT_FALSE(TileOfSlot(model, *tile, kFaceDown).has_value());
        EXPECT_EQ(TileOfSlot(model, *tile, kFaceUp), std::optional<Tile>(T("7p")));
    }

    TEST(TileHoverTest, AnOpenKongShowsWhetherOrNotTheHandIsFaceUp) {
        HandModel model = UpgradeModel();
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetHand(kSeat1).melds.size(), 1u);
        ASSERT_FALSE(model.GetHand(kSeat1).melds[0].concealed);
        const TableLayout layout = MakeTableLayout(InputOfModel(model));
        const TileSlot* tile = Find(layout, SlotRole::MeldTile, kSeat1, 0, 3);
        ASSERT_NE(tile, nullptr);
        EXPECT_EQ(TileOfSlot(model, *tile, kFaceDown), std::optional<Tile>(T("5p")));
    }

    TEST(TileHoverTest, TileOfSlotGivesNoneForAnIndexOutsideTheModelAndForAWallStack) {
        const HandModel model = MakeModel(SpecOf("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s", "5s"));
        const int hand = static_cast<int>(model.GetHand(kSeat0).concealed.size());
        for (const SlotRole role : {SlotRole::HandTile, SlotRole::MeldTile, SlotRole::BonusTile, SlotRole::DiscardTile}) {
            for (const int index : {hand + 5, -1}) {
                TileSlot slot;
                slot.role = role;
                slot.seat = kSeat0;
                slot.index = index;
                EXPECT_FALSE(TileOfSlot(model, slot, kFaceUp).has_value()) << "role " << static_cast<int>(role) << " index " << index;
            }
        }
        TileSlot stack;
        stack.role = SlotRole::WallStack;
        EXPECT_FALSE(TileOfSlot(model, stack, kFaceUp).has_value());
        TileSlot noSeat;
        noSeat.role = SlotRole::HandTile;
        noSeat.seat = kNoSeat;
        EXPECT_FALSE(TileOfSlot(model, noSeat, kFaceUp).has_value());
    }

    TEST(TileHoverTest, ThePlateLiesBelowATileWithRoomAndNeverOverlapsIt) {
        const Rect tile(600.0f, 200.0f, 48.0f, 64.0f);
        const Rect plate = HoverPlateRect(tile, kPlateW, kPlateH, kViewWidth, kViewHeight);
        EXPECT_FLOAT_EQ(plate.w, kPlateW);
        EXPECT_FLOAT_EQ(plate.h, kPlateH);
        EXPECT_GE(plate.y, tile.y + tile.h) << "below the tile";
        EXPECT_FALSE(Overlaps(plate, tile));
        EXPECT_NEAR(CentreX(plate), CentreX(tile), kTolerance);
    }

    TEST(TileHoverTest, ThePlateGoesAboveATileAtTheBottomAndStaysInsideTheViewAtAllFourCorners) {
        const Rect bottom(600.0f, kViewHeight - 70.0f, 48.0f, 64.0f);
        const Rect above = HoverPlateRect(bottom, kPlateW, kPlateH, kViewWidth, kViewHeight);
        EXPECT_LE(above.y + above.h, bottom.y) << "above the tile: it does not fit below";
        EXPECT_FALSE(Overlaps(above, bottom));
        const float right = kViewWidth - 48.0f;
        const float low = kViewHeight - 64.0f;
        for (const Rect tile : {Rect(0.0f, 0.0f, 48.0f, 64.0f), Rect(right, 0.0f, 48.0f, 64.0f), Rect(0.0f, low, 48.0f, 64.0f), Rect(right, low, 48.0f, 64.0f)}) {
            const Rect plate = HoverPlateRect(tile, kPlateW, kPlateH, kViewWidth, kViewHeight);
            EXPECT_TRUE(Inside(plate, kViewWidth, kViewHeight)) << "tile at " << tile.x << ", " << tile.y;
            EXPECT_FALSE(Overlaps(plate, tile)) << "tile at " << tile.x << ", " << tile.y;
        }
    }

}  // namespace BECore::Mahjong::Tests
