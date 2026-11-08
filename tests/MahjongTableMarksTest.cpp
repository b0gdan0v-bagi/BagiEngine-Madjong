#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Table/UiPalette.h>

/**
 * The marks of the last move on the table (`Components/MahjongTableComponent.h`): the tint on the last discard, the tile the viewer has
 * just drawn lying apart (and clickable like any other tile) and the bar on the claimed tile of an open set. The shipped scene is drawn
 * into a `RecordingRenderer` as big as the view, so a rect of a draw call is a rect of the layout. The fixed hands come from
 * `FixedWall.h` and are laid into the hand in progress.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seat 0 holds a hand that is far from waiting; as East it draws 9p.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// Seat 1 holds a pair of 5p, 4p 6p 7p near them (a pung and two chows on a discarded 5p).
        constexpr const char* kSeat1HoldsPairOf5p = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
        /// Seat 1 holds 4p 6p (a chow with a discarded 5p) and no 5p.
        constexpr const char* kSeat1HoldsAChowGap = "4p 6p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
        /// Seat 1 holds three 5p (a kong with a discarded 5p).
        constexpr const char* kSeat1HoldsThree5p = "5p 5p 5p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
        /// Seat 0 holds three 1p (no other seat holds one) and draws a fourth.
        constexpr const char* kThree1pHand = "1p 1p 1p 4s 7s 1m 4m 7m 4p 7p Sw Ew Gd";
        constexpr SeatSetup kPersonAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        /// How far a tile of the hand is apart from the next one in a plain row at the size of the test (the view is not scaled).
        constexpr float kEpsilon = 0.01f;

        /// The bar along `edge` of `rect`, worked out here and not taken from the component.
        Rect BarOf(const Rect& rect, Edge edge) {
            switch (edge) {
                case Edge::Left:
                    return Rect(rect.x, rect.y, kClaimedEdgeWidth, rect.h);
                case Edge::Right:
                    return Rect(rect.x + rect.w - kClaimedEdgeWidth, rect.y, kClaimedEdgeWidth, rect.h);
                case Edge::Top:
                    return Rect(rect.x, rect.y, rect.w, kClaimedEdgeWidth);
                case Edge::Bottom:
                    return Rect(rect.x, rect.y + rect.h - kClaimedEdgeWidth, rect.w, kClaimedEdgeWidth);
                case Edge::None:
                    break;
            }
            return Rect();
        }

    }  // namespace

    class MahjongTableMarksTest : public MahjongTableTestBase {
    protected:
        /// The rectangles drawn in `color`, in the order drawn.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        /// The index in `_renderer->calls` of each rectangle drawn in `color`.
        std::vector<size_t> CallsOf(const Color& color) const {
            std::vector<size_t> found;
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                if (_renderer->calls[i].kind == DrawKind::Rect && SameColor(_renderer->calls[i].color, color)) {
                    found.push_back(i);
                }
            }
            return found;
        }

        const TileSlot* SlotOf(SlotRole role, Seat seat, int index, int part = 0) const {
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == role && slot.seat == seat && slot.index == index && slot.part == part) {
                    return &slot;
                }
            }
            return nullptr;
        }

        /// The slots of the hand of `seat` in the order of the concealed tiles.
        std::vector<const TileSlot*> HandSlots(Seat seat) const {
            std::vector<const TileSlot*> found;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == SlotRole::HandTile && slot.seat == seat) {
                    found.push_back(&slot);
                }
            }
            return found;
        }

        /// The hand of `seat` is one plain row: from tile to tile the step is always the same.
        void ExpectPlainRow(Seat seat) const {
            const std::vector<const TileSlot*> slots = HandSlots(seat);
            ASSERT_GE(slots.size(), 3u) << "seat " << seat;
            const float dx = slots[1]->rect.x - slots[0]->rect.x;
            const float dy = slots[1]->rect.y - slots[0]->rect.y;
            for (size_t i = 2; i < slots.size(); ++i) {
                EXPECT_NEAR(slots[i]->rect.x - slots[i - 1]->rect.x, dx, kEpsilon) << "seat " << seat << " tile " << i;
                EXPECT_NEAR(slots[i]->rect.y - slots[i - 1]->rect.y, dy, kEpsilon) << "seat " << seat << " tile " << i;
            }
        }

        /// The viewer's tile `drawn` lies apart: the tiles before it keep their step, it is `kDrawnTileGap` farther from the last of them.
        void ExpectLiesApart(int drawn) const {
            const std::vector<const TileSlot*> slots = HandSlots(_table->GetLayout().viewer);
            ASSERT_GE(slots.size(), 4u);
            ASSERT_LT(drawn, static_cast<int>(slots.size()));
            std::vector<const TileSlot*> others;
            for (size_t i = 0; i < slots.size(); ++i) {
                if (static_cast<int>(i) != drawn) {
                    others.push_back(slots[i]);
                }
            }
            const float step = others[1]->rect.x - others[0]->rect.x;
            for (size_t i = 1; i < others.size(); ++i) {
                EXPECT_NEAR(others[i]->rect.x - others[i - 1]->rect.x, step, kEpsilon) << "the others close up";
            }
            EXPECT_NEAR(slots[drawn]->rect.x - others.back()->rect.x, step + kDrawnTileGap, kEpsilon);
            EXPECT_EQ(slots[drawn]->rect.y, others.back()->rect.y);
        }

        /// A match with `seats` and a fixed model laid; the first frame is drawn.
        void StartWith(const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            Draw();
        }

        /// The only slot of a meld tile that carries an edge.
        const TileSlot* ClaimedSlot() const {
            const TileSlot* found = nullptr;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.claimEdge != Edge::None) {
                    EXPECT_EQ(found, nullptr) << "one claimed tile";
                    found = &slot;
                }
            }
            return found;
        }

        /// East (seat 0) discards a 5p that seat 1 (holding `seat1`) may claim; the viewer is the person at seat 0.
        HandModel EastDiscardsFive(const char* seat1) const {
            FixedWallSpec spec = SpecOf(kPlainHand, "5p");
            spec.hands[kSeat1] = seat1;
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }
    };

    // ----- the last discard -----

    TEST_F(MahjongTableMarksTest, NothingIsMarkedAtTheStartOfAHand) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        EXPECT_TRUE(Hand().Moves().empty());
        EXPECT_TRUE(RectsOf(kLastDiscardMark).empty());
        EXPECT_TRUE(RectsOf(kClaimedEdge).empty());
    }

    TEST_F(MahjongTableMarksTest, TheLastDiscardIsTintedAndTheMarkMovesWithTheNextDiscard) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("9p"))));
        Draw();

        // The viewer's own discard carries the mark, on the face of the tile (after the shadow, on the slot's rect)
        ASSERT_EQ(Hand().Discards().size(), 1u);
        const TileSlot* first = SlotOf(SlotRole::DiscardTile, kSeat0, 0);
        ASSERT_NE(first, nullptr);
        std::vector<Rect> tints = RectsOf(kLastDiscardMark);
        ASSERT_EQ(tints.size(), 1u);
        EXPECT_EQ(tints[0].x, first->rect.x);
        EXPECT_EQ(tints[0].y, first->rect.y);
        EXPECT_EQ(tints[0].w, first->rect.w);
        EXPECT_EQ(tints[0].h, first->rect.h);
        const std::vector<size_t> shadows = CallsOf(kTileShadow);
        const auto shadowOfFirst =
            std::find_if(shadows.begin(), shadows.end(), [&](size_t i) { return _renderer->calls[i].rect.x == first->rect.x + 2.0f && _renderer->calls[i].rect.y == first->rect.y + 2.0f; });
        ASSERT_NE(shadowOfFirst, shadows.end());
        EXPECT_LT(*shadowOfFirst, CallsOf(kLastDiscardMark).front()) << "shadow, face, then the tint";

        // A computer discards next: the mark leaves the first and lies on the new tile only
        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        ASSERT_EQ(Hand().Moves().back().kind, MoveKind::Discard);
        ASSERT_EQ(Hand().Discards().back().seat, kSeat1);
        Draw();

        const TileSlot* second = SlotOf(SlotRole::DiscardTile, kSeat1, static_cast<int>(Hand().Discards().size()) - 1);
        ASSERT_NE(second, nullptr);
        tints = RectsOf(kLastDiscardMark);
        ASSERT_EQ(tints.size(), 1u) << "the mark moved on, it did not multiply";
        EXPECT_EQ(tints[0].x, second->rect.x);
        EXPECT_EQ(tints[0].y, second->rect.y);
    }

    TEST_F(MahjongTableMarksTest, AClaimTakesTheMarkOffTheDiscardAndTheEarlierDiscardIsNotMarked) {
        // Seat 0 discards a 9p that stays in the pile, seat 1 draws a 5p and discards it, seat 2 (a pair of 5p) pungs it
        FixedWallSpec spec = SpecOf(kPlainHand, "9p 5p");
        spec.hands[kSeat2] = "5p 5p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("9p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Discard) << "nobody holds a claim on the 9p";
        ASSERT_TRUE(model.Discard(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        EXPECT_EQ(RectsOf(kLastDiscardMark).size(), 1u) << "the 5p, not the 9p";

        ASSERT_TRUE(_game->GetMatch().Hand().Claim(kSeat2, ClaimKind::Pung).has_value());
        ASSERT_EQ(Hand().Moves().back().kind, MoveKind::ClaimPung);
        ASSERT_EQ(Hand().Discards().size(), 1u) << "the 9p stays in the pile";
        Draw();

        EXPECT_TRUE(RectsOf(kLastDiscardMark).empty());
    }

    TEST_F(MahjongTableMarksTest, ThePendingFrameAndTheTintAreSeparateDrawsOnAPendingDiscard) {
        ASSERT_NO_FATAL_FAILURE(Start(kPersonAtSeat1));
        Lay(EastDiscardsFive(kSeat1HoldsPairOf5p));
        Draw();
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);

        const std::vector<size_t> frames = CallsOf(kPendingFrame);
        const std::vector<size_t> tints = CallsOf(kLastDiscardMark);
        ASSERT_EQ(frames.size(), 1u);
        ASSERT_EQ(tints.size(), 1u);
        EXPECT_LT(frames[0], tints[0]) << "the tint lies over the face, inside the frame";
        const Rect& frame = _renderer->calls[frames[0]].rect;
        const Rect& tint = _renderer->calls[tints[0]].rect;
        EXPECT_LT(frame.x, tint.x);
        EXPECT_GT(frame.x + frame.w, tint.x + tint.w);
    }

    // ----- the tile just drawn -----

    TEST_F(MahjongTableMarksTest, TheTileTheViewerHasJustDrawnLiesApartAndTheHandClosesUpAfterTheDiscard) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "5p"))));
        ASSERT_EQ(Hand().DrawnTile(kSeat0), T("5p"));

        const int drawn = IndexOf(kSeat0, T("5p"));
        ASSERT_GE(drawn, 0);
        EXPECT_NE(drawn, static_cast<int>(Hand().GetHand(kSeat0).concealed.size()) - 1) << "5p sorts into the middle of the hand";
        ASSERT_NO_FATAL_FAILURE(ExpectLiesApart(drawn));
        const TileSlot* slot = HandSlot(drawn);
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->index, drawn) << "the slot keeps the index of the tile";
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0));

        // After the discard seat 0 holds no drawn tile: the plain row of 13
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeDiscard(T("1p"), false)).has_value());
        Draw();

        EXPECT_EQ(Hand().DrawnTile(kSeat0), Tile{});
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
    }

    TEST_F(MahjongTableMarksTest, AClickOnTheDrawnTileDiscardsThatTile) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "5p"))));
        const Tile drawn = Hand().DrawnTile(kSeat0);
        ASSERT_TRUE(drawn.IsValid());
        const int index = IndexOf(kSeat0, drawn);
        ASSERT_GE(index, 0);

        EXPECT_FALSE(ClickHandTile(index)) << "no canvas takes the click: the table does";

        ASSERT_EQ(Hand().Discards().size(), 1u);
        EXPECT_EQ(Hand().Discards().front().tile, drawn);
        EXPECT_EQ(Hand().Discards().front().seat, kSeat0);
        EXPECT_EQ(Hand().GetHand(kSeat0).concealed.size(), static_cast<size_t>(kDealtTiles)) << "the other 13 stay";
    }

    TEST_F(MahjongTableMarksTest, TheDrawnTileIsTheLastCopyWhenTheHandHoldsMore) {
        // 5p 5p in the hand and a 5p drawn: the apart tile is a 5p, the one that sorted last among the three
        FixedWallSpec spec = SpecOf("5p 5p 4s 7s 1m 4m 7m 1p 4p 7p Sw Ew Gd", "5p");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        ASSERT_EQ(Hand().DrawnTile(kSeat0), T("5p"));

        int last = -1;
        const PlayerHand& held = Hand().GetHand(kSeat0);
        for (size_t i = 0; i < held.concealed.size(); ++i) {
            last = held.concealed[i] == T("5p") ? static_cast<int>(i) : last;
        }
        ASSERT_GE(last, 2);
        ASSERT_NO_FATAL_FAILURE(ExpectLiesApart(last));
    }

    TEST_F(MahjongTableMarksTest, NoTileLiesApartAfterAClaimOfAPung) {
        // Seat 1 (East) discards a 5p and the person at seat 0 pungs it: it holds one tile too many and drew nothing
        FixedWallSpec spec = SpecOf("5p 5p 4s 7s 1m 4m 7m 1p 4p 7p Sw Ew Gd", "5p");
        spec.hands[kSeat1] = kQuiet1;
        HandModel model = MakeModel(spec, kSeat1);
        ASSERT_TRUE(model.Discard(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Pung).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        ASSERT_EQ(Hand().CurrentSeat(), kSeat0);
        ASSERT_EQ(Hand().DrawnTile(kSeat0), Tile{});
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
    }

    TEST_F(MahjongTableMarksTest, TheLooseTileAfterAKongLiesApart) {
        // A concealed kong, then the loose tile
        FixedWallSpec own = SpecOf(kThree1pHand, "1p");
        own.loose = "9m";
        HandModel model = MakeModel(own);
        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T("1p")).has_value());
        ASSERT_EQ(model.DrawnTile(kSeat0), T("9m"));
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        ASSERT_NO_FATAL_FAILURE(ExpectLiesApart(IndexOf(kSeat0, T("9m"))));
        EXPECT_TRUE(RectsOf(kClaimedEdge).empty()) << "a concealed kong was claimed from nobody";

        // A claimed kong, then the loose tile: seat 1 (East) discards the 1p, the person at seat 0 holds three
        FixedWallSpec claim = SpecOf(kThree1pHand, "1p");
        claim.hands[kSeat1] = kQuiet1;
        claim.loose = "9m";
        HandModel claimed = MakeModel(claim, kSeat1);
        ASSERT_TRUE(claimed.Discard(kSeat1, T("1p")).has_value());
        ASSERT_EQ(claimed.GetPhase(), Phase::Claim);
        ASSERT_TRUE(claimed.Claim(kSeat0, ClaimKind::Kong).has_value());
        ASSERT_EQ(claimed.DrawnTile(kSeat0), T("9m"));
        Lay(claimed);
        Draw();

        ASSERT_NO_FATAL_FAILURE(ExpectLiesApart(IndexOf(kSeat0, T("9m"))));
    }

    TEST_F(MahjongTableMarksTest, NoPeekingTheCoverTheSpectatorAndAnOpponentSeeAPlainRow) {
        // Two people: the screen is covered at the start, the hand of seat 0 (14 tiles, one drawn) is backs in a plain row
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Lay(MakeModel(SpecOf(kPlainHand, "5p")));
        Draw();
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        ASSERT_TRUE(Hand().DrawnTile(kSeat0).IsValid());
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kNoSeat));

        session.Reveal();
        Draw();

        ASSERT_FALSE(session.HandoffPending());
        ASSERT_NO_FATAL_FAILURE(ExpectLiesApart(IndexOf(kSeat0, T("5p"))));

        // A spectator: no person at the table
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Lay(MakeModel(SpecOf(kPlainHand, "5p")));
        Draw();
        ASSERT_TRUE(Hand().DrawnTile(kSeat0).IsValid());
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));

        // An opponent that has just drawn: seat 1 is East, the person sits at seat 0
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Lay(MakeModel(SpecOf(kPlainHand, "5p"), kSeat1));
        Draw();
        ASSERT_TRUE(Hand().DrawnTile(kSeat1).IsValid());
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat1));
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0));
    }

    // ----- the claimed tile -----

    TEST_F(MahjongTableMarksTest, TheClaimedTileOfAPungHasABarOnTheSideOfTheSeatItCameFrom) {
        // Seat 1 pungs the 5p of seat 0, the person at seat 0 is at the bottom: seat 1 sits at the right, seat 0 is the seat before it
        HandModel model = EastDiscardsFive(kSeat1HoldsPairOf5p);
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        const TileSlot* claimed = ClaimedSlot();
        ASSERT_NE(claimed, nullptr);
        EXPECT_EQ(claimed->role, SlotRole::MeldTile);
        EXPECT_EQ(claimed->seat, kSeat1);
        EXPECT_EQ(claimed->part, 0) << "from the seat before it: the first tile";
        EXPECT_EQ(claimed->claimEdge, Edge::Bottom) << "a seat on the right faces the bottom, where seat 0 sits";
        const std::vector<Rect> bars = RectsOf(kClaimedEdge);
        ASSERT_EQ(bars.size(), 1u);
        const Rect expected = BarOf(claimed->rect, Edge::Bottom);
        EXPECT_EQ(bars[0].x, expected.x);
        EXPECT_EQ(bars[0].y, expected.y);
        EXPECT_EQ(bars[0].w, expected.w);
        EXPECT_EQ(bars[0].h, expected.h);
        // The bar is on the face: after the tile, inside it
        const std::vector<size_t> calls = CallsOf(kClaimedEdge);
        const std::vector<size_t> shadows = CallsOf(kTileShadow);
        const auto shadow =
            std::find_if(shadows.begin(), shadows.end(), [&](size_t i) { return _renderer->calls[i].rect.x == claimed->rect.x + 2.0f && _renderer->calls[i].rect.y == claimed->rect.y + 2.0f; });
        ASSERT_NE(shadow, shadows.end());
        EXPECT_LT(*shadow, calls[0]);
        EXPECT_GE(bars[0].x, claimed->rect.x);
        EXPECT_LE(bars[0].x + bars[0].w, claimed->rect.x + claimed->rect.w + kEpsilon);
    }

    TEST_F(MahjongTableMarksTest, TheBarFollowsTheOwnersPlaceOnTheScreen) {
        // The same pung seen by the person at seat 1: the owner sits at the bottom, the source (the seat before it) on its left
        ASSERT_NO_FATAL_FAILURE(Start(kPersonAtSeat1));
        HandModel model = EastDiscardsFive(kSeat1HoldsPairOf5p);
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        Lay(model);
        Draw();

        const TileSlot* claimed = ClaimedSlot();
        ASSERT_NE(claimed, nullptr);
        ASSERT_EQ(_table->GetLayout().viewer, kSeat1);
        EXPECT_EQ(claimed->claimEdge, Edge::Left);
        const std::vector<Rect> bars = RectsOf(kClaimedEdge);
        ASSERT_EQ(bars.size(), 1u);
        const Rect expected = BarOf(claimed->rect, Edge::Left);
        EXPECT_EQ(bars[0].x, expected.x);
        EXPECT_EQ(bars[0].w, expected.w);
        EXPECT_EQ(bars[0].h, expected.h);
    }

    TEST_F(MahjongTableMarksTest, TheClaimedTileOfAChowIsTheMiddleOneAndOfAKongTheFirst) {
        HandModel chow = EastDiscardsFive(kSeat1HoldsAChowGap);
        ASSERT_TRUE(chow.Claim(kSeat1, ClaimKind::Chow, T("4p")).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, chow));

        const TileSlot* claimed = ClaimedSlot();
        ASSERT_NE(claimed, nullptr);
        EXPECT_EQ(claimed->part, 1) << "4p 5p 6p: the 5p";
        EXPECT_EQ(claimed->claimEdge, Edge::Bottom);
        EXPECT_EQ(RectsOf(kClaimedEdge).size(), 1u);

        HandModel kong = EastDiscardsFive(kSeat1HoldsThree5p);
        ASSERT_TRUE(kong.Claim(kSeat1, ClaimKind::Kong).has_value());
        Lay(kong);
        Draw();

        claimed = ClaimedSlot();
        ASSERT_NE(claimed, nullptr);
        EXPECT_EQ(claimed->part, 0);
        EXPECT_EQ(claimed->claimEdge, Edge::Bottom);
        EXPECT_EQ(RectsOf(kClaimedEdge).size(), 1u);
        EXPECT_EQ(Hand().GetHand(kSeat1).melds.back().kind, MeldKind::Kong);
    }

    TEST_F(MahjongTableMarksTest, AnUpgradedKongKeepsTheMarkOfItsPung) {
        HandModel model = UpgradeModel();
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        const TileSlot* before = ClaimedSlot();
        ASSERT_NE(before, nullptr);
        ASSERT_EQ(RectsOf(kClaimedEdge).size(), 1u);
        const Edge edge = before->claimEdge;
        const int part = before->part;

        ASSERT_TRUE(_game->GetMatch().Hand().UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(_game->GetMatch().Hand().GetHand(kSeat1).melds.front().kind, MeldKind::Kong);
        Draw();

        const TileSlot* after = ClaimedSlot();
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(after->claimEdge, edge);
        EXPECT_EQ(after->part, part);
        EXPECT_EQ(RectsOf(kClaimedEdge).size(), 1u);
    }

    TEST_F(MahjongTableMarksTest, AConcealedKongHasNoBarAndNoDiscardIsMarked) {
        FixedWallSpec own = SpecOf(kThree1pHand, "1p");
        HandModel model = MakeModel(own);
        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T("1p")).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        EXPECT_TRUE(RectsOf(kClaimedEdge).empty());
        EXPECT_TRUE(RectsOf(kLastDiscardMark).empty());
    }

    // ----- the cover -----

    TEST_F(MahjongTableMarksTest, UnderTheCoverTheMarksOfTheOpenTableShowAndNothingElseDoes) {
        // Seat 1 has pungged the 5p of seat 0 and is to act; two people, the screen is covered until "Show"
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, OpenPungModel(kSeat1Hand)));
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        ASSERT_EQ(Hand().CurrentSeat(), kSeat1);
        ASSERT_TRUE(Hand().DrawnTile(kSeat1).IsValid());

        EXPECT_EQ(RectsOf(kClaimedEdge).size(), 1u) << "an open set is public";
        EXPECT_EQ(RectsOf(kLastDiscardMark).size(), 1u) << "so is the pile";
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat1));
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kNoSeat));

        session.Reveal();
        Draw();

        ASSERT_FALSE(session.HandoffPending());
        EXPECT_EQ(RectsOf(kClaimedEdge).size(), 1u);
        EXPECT_EQ(RectsOf(kLastDiscardMark).size(), 1u);
        ASSERT_NO_FATAL_FAILURE(ExpectLiesApart(IndexOf(kSeat1, Hand().DrawnTile(kSeat1))));
    }

}  // namespace BECore::Mahjong::Tests
