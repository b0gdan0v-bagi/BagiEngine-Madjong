#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Events/SceneEvents.h>
#include <Mahjong/Table/CallingMarks.h>
#include <Mahjong/Table/UiPalette.h>
#include <set>

/**
 * The marks of the last move on the table (`Components/MahjongTableComponent.h`): the tint on the last discard, the tint on the tile the
 * viewer has just drawn (it stays in its place in the row and is clickable like any other tile), the bar on the claimed tile of an open set
 * and the frame round the hand of a seat that has declared its calling hand. The shipped scene is drawn
 * into a `RecordingRenderer` as big as the view, so a rect of a draw call is a rect of the layout. The fixed hands come from
 * `FixedWall.h` and are laid into the hand in progress. The highlight of a set that was just laid is here too: it is a mark of the last move.
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
        /// The fill behind a set that was just laid and the frame of a declared hand (`kCallingFrameWidth` units wide on the border of the
        /// same padded rectangle) reach this far past the tiles (`kActingMargin` of the component).
        constexpr float kActingMargin = 4.0f;
        /// Seats 0 and 1 wait on the 5p with a pair of winds (seats 2 and 3 wait as `kSeat2Waits` and `kSeat3Waits`).
        constexpr const char* kWaitingHand0 = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Sw Sw";
        constexpr const char* kWaitingHand1 = "4p 6p 1m 2m 3m 4s 5s 6s 7m 8m 9m Nw Nw";
        /// Times that a blink period (`kClaimBlinkSeconds` 0.5) divides exactly in floats: 0.375 s is in the off half, 0.125 s ends it.
        constexpr float kOffHalf = 0.375f;
        constexpr float kEndOfPeriod = 0.125f;
        /// Seat 1 holds a pair of red dragons and a hand that a pung of them brings no further from winning: it claims (`ComputerClaimTest`).
        constexpr const char* kSeat1HoldsRedPair = "3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw Rd Rd";

        void Elapse(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

        /// The tolerance of a float comparison of two rects (the view is not scaled, so the numbers are exact up to rounding).
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

        /// The rectangles drawn in `color`, in the order drawn.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const size_t i : CallsOf(color)) {
                found.push_back(_renderer->calls[i].rect);
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

        /// The viewer's hand is one plain row and its tile `drawn` carries the tint `kDrawnTileMark`: exactly one rect of that colour, equal
        /// to the slot's rect, drawn after the tile itself.
        void ExpectDrawnMarked(int drawn) const {
            ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(_table->GetLayout().viewer));
            const std::vector<const TileSlot*> slots = HandSlots(_table->GetLayout().viewer);
            ASSERT_LT(drawn, static_cast<int>(slots.size()));
            const Rect& rect = slots[static_cast<size_t>(drawn)]->rect;
            const std::vector<size_t> tints = CallsOf(kDrawnTileMark);
            ASSERT_EQ(tints.size(), 1u);
            const DrawCall& tint = _renderer->calls[tints[0]];
            EXPECT_EQ(tint.rect.x, rect.x);
            EXPECT_EQ(tint.rect.y, rect.y);
            EXPECT_EQ(tint.rect.w, rect.w);
            EXPECT_EQ(tint.rect.h, rect.h);
            size_t tileCall = _renderer->calls.size();
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                const DrawCall& call = _renderer->calls[i];
                if (call.kind == DrawKind::Texture && call.texture == _atlasTexture && call.rect.x == rect.x && call.rect.y == rect.y) {
                    tileCall = i;
                }
            }
            ASSERT_LT(tileCall, _renderer->calls.size());
            EXPECT_LT(tileCall, tints[0]) << "the tint lies over the face";
        }

        /// A match in which the four seats hold waiting hands (on the 5p) and every seat has declared with its first discard (the dragons
        /// of the live wall: no discard opens a window); seat 0 is to act again, with `lastDraw` drawn (not a 5p, unless the test wins).
        static HandModel AllDeclaredModel(const char* lastDraw = "9s") {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kWaitingHand0;
            spec.hands[kSeat1] = kWaitingHand1;
            spec.hands[kSeat2] = kSeat2Waits;
            spec.hands[kSeat3] = kSeat3Waits;
            spec.live = std::string("Gd Rd Wd Gd ") + lastDraw;
            HandModel model = MakeModel(spec);
            Lap(model, "Gd Rd Wd Gd", {kSeat0, kSeat1, kSeat2, kSeat3});
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            return model;
        }

        /// The four bars of `kCallingFrame` that should lie round the hand of `seat` in the last layout, worked out here.
        std::vector<Rect> FrameOf(Seat seat) const {
            const Rect bounds = BoundsOf(_table->GetLayout(), SlotRole::HandTile, seat);
            const Rect padded(bounds.x - kActingMargin, bounds.y - kActingMargin, bounds.w + 2.0f * kActingMargin, bounds.h + 2.0f * kActingMargin);
            const float width = kCallingFrameWidth;
            return {Rect(padded.x, padded.y, padded.w, width), Rect(padded.x, padded.y + padded.h - width, padded.w, width), Rect(padded.x, padded.y + width, width, padded.h - 2.0f * width),
                    Rect(padded.x + padded.w - width, padded.y + width, width, padded.h - 2.0f * width)};
        }

        /// The frame of `seat` is in the draw calls (all four bars, once each).
        void ExpectFramed(Seat seat) const {
            const std::vector<Rect> drawn = RectsOf(kCallingFrame);
            for (const Rect& bar : FrameOf(seat)) {
                int found = 0;
                for (const Rect& rect : drawn) {
                    found += SameRect(rect, bar) ? 1 : 0;
                }
                EXPECT_EQ(found, 1) << "seat " << seat << " bar " << bar.x << "," << bar.y << " " << bar.w << "x" << bar.h;
            }
        }

        /// Every bar of the frames lies inside the view.
        void ExpectFramesInsideTheView() const {
            for (const Rect& rect : RectsOf(kCallingFrame)) {
                EXPECT_GE(rect.x, 0.0f);
                EXPECT_GE(rect.y, 0.0f);
                EXPECT_LE(rect.x + rect.w, kViewWidth);
                EXPECT_LE(rect.y + rect.h, kViewHeight);
            }
        }

        /// The bars of `kKeepsWaitingMark` are exactly one inside the bottom edge of each slot of the viewer's hand whose tile is in `ids`:
        /// `kKeepsWaitingBarHeight` high, as wide as the tile, drawn after the face of the tile and inside the view.
        void ExpectKeepWaitingBarsOn(const std::set<int>& ids) const {
            const Seat viewer = _table->GetLayout().viewer;
            const std::vector<Rect> bars = RectsOf(kKeepsWaitingMark);
            const std::vector<size_t> calls = CallsOf(kKeepsWaitingMark);
            int expected = 0;
            for (const TileSlot* slot : HandSlots(viewer)) {
                const Tile tile = Hand().GetHand(viewer).concealed[static_cast<size_t>(slot->index)];
                if (!ids.contains(tile.Id())) {
                    continue;
                }
                ++expected;
                const Rect want(slot->rect.x, slot->rect.y + slot->rect.h - kKeepsWaitingBarHeight, slot->rect.w, kKeepsWaitingBarHeight);
                int found = 0;
                for (size_t i = 0; i < bars.size(); ++i) {
                    if (SameRect(bars[i], want)) {
                        ++found;
                        size_t tileCall = _renderer->calls.size();
                        for (size_t k = 0; k < _renderer->calls.size(); ++k) {
                            const DrawCall& call = _renderer->calls[k];
                            if (call.kind == DrawKind::Texture && call.texture == _atlasTexture && call.rect.x == slot->rect.x && call.rect.y == slot->rect.y) {
                                tileCall = k;
                            }
                        }
                        EXPECT_LT(tileCall, calls[i]) << "the bar lies over the face";
                    }
                }
                EXPECT_EQ(found, 1) << "tile " << ToString(tile) << " at " << slot->index;
                EXPECT_GE(want.y + want.h, slot->rect.y);
                EXPECT_LE(want.y + want.h, slot->rect.y + slot->rect.h);
            }
            EXPECT_EQ(static_cast<int>(bars.size()), expected) << "no bar on a tile that is not in the set";
            for (const Rect& bar : bars) {
                EXPECT_GE(bar.x, 0.0f);
                EXPECT_GE(bar.y, 0.0f);
                EXPECT_LE(bar.x + bar.w, kViewWidth);
                EXPECT_LE(bar.y + bar.h, kViewHeight);
            }
        }

        /// The tiles of the viewer's hand that keep it waiting if discarded, from the pure function on the viewer's view.
        std::set<int> SafeDiscards(Seat viewer = kSeat0) const {
            std::set<int> ids;
            const auto keeps = KeepsWaitingDiscards(MakeTableView(Hand(), viewer));
            for (size_t id = 0; id < keeps.size(); ++id) {
                if (keeps[id]) {
                    ids.insert(static_cast<int>(id));
                }
            }
            return ids;
        }

        /// A match in which seat 0 has declared with its first discard; it is to act again with `lastDraw` drawn. The others hold hands that
        /// claim nothing; seat 1 draws `Sw` next, which seat 0 (a pair of south winds) may pung.
        static FixedWallSpec ViewerDeclaredSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kWaitingHand0;
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kSeat2Waits;
            spec.hands[kSeat3] = kSeat3Waits;
            spec.live = "Gd Rd Wd Gd 9s Sw";
            return spec;
        }

        static HandModel ViewerDeclaredModel() {
            HandModel model = MakeModel(ViewerDeclaredSpec());
            Lap(model, "Gd Rd Wd Gd", {kSeat0});
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            EXPECT_TRUE(model.GetHand(kSeat0).callingDeclared);
            return model;
        }

        /// The fill that should lie behind the meld `meld` of `seat` in the last layout.
        Rect HighlightOf(Seat seat, int meld) const {
            const Rect bounds = BoundsOfMeld(_table->GetLayout(), seat, meld);
            return Rect(bounds.x - kActingMargin, bounds.y - kActingMargin, bounds.w + 2.0f * kActingMargin, bounds.h + 2.0f * kActingMargin);
        }

        /// The highlight is drawn exactly once, behind the meld `meld` of `seat`.
        void ExpectHighlightOn(Seat seat, int meld) const {
            const std::vector<Rect> fills = RectsOf(kClaimHighlight);
            ASSERT_EQ(fills.size(), 1u);
            const Rect expected = HighlightOf(seat, meld);
            EXPECT_NEAR(fills[0].x, expected.x, kEpsilon);
            EXPECT_NEAR(fills[0].y, expected.y, kEpsilon);
            EXPECT_NEAR(fills[0].w, expected.w, kEpsilon);
            EXPECT_NEAR(fills[0].h, expected.h, kEpsilon);
        }

        /// A match with `seats`, drawn once empty, and then `model` laid and drawn: everything in `model` is news to the table.
        void StartThenLay(const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Draw();
            Lay(model);
            Draw();
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

        // ----- the seat plates -----

        /// The plate of `seat` in the last layout.
        Rect PlateOf(Seat seat) const {
            return _table->GetLayout().zones.seatPlates[static_cast<size_t>(seat)];
        }

        /// The four bars `DrawFrame` should lay on the border of `rect`, worked out here.
        static std::vector<Rect> BarsOf(const Rect& rect, float width) {
            return {Rect(rect.x, rect.y, rect.w, width), Rect(rect.x, rect.y + rect.h - width, rect.w, width), Rect(rect.x, rect.y + width, width, rect.h - 2.0f * width),
                    Rect(rect.x + rect.w - width, rect.y + width, width, rect.h - 2.0f * width)};
        }

        /// How many of the rects drawn in `color` are `rect`.
        int CountOf(const Color& color, const Rect& rect) const {
            int found = 0;
            for (const Rect& drawn : RectsOf(color)) {
                found += SameRect(drawn, rect) ? 1 : 0;
            }
            return found;
        }

        /// The plate of `seat` carries the tint and the frame of the turn, all four bars once.
        bool TurnShownOn(Seat seat) const {
            if (CountOf(kTurnMark, PlateOf(seat)) != 1) {
                return false;
            }
            for (const Rect& bar : BarsOf(PlateOf(seat), kSeatTurnFrameWidth)) {
                if (CountOf(kSeatTurnFrame, bar) != 1) {
                    return false;
                }
            }
            return true;
        }

        /// The bar of the dealer lies along the top edge of the plate of `seat`.
        bool DealerShownOn(Seat seat) const {
            const Rect plate = PlateOf(seat);
            return CountOf(kDealerMark, Rect(plate.x, plate.y, plate.w, kDealerMarkHeight)) == 1;
        }

        /// The turn is on exactly the plate of `turn` (no seat: on none), the dealer's bar on exactly the plate of `dealer`.
        void ExpectMarksOnPlates(Seat turn, Seat dealer) const {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(TurnShownOn(seat), seat == turn) << "turn, seat " << seat;
                EXPECT_EQ(DealerShownOn(seat), seat == dealer) << "dealer, seat " << seat;
            }
            EXPECT_EQ(RectsOf(kSeatTurnFrame).size(), IsSeat(turn) ? 4u : 0u);
            EXPECT_EQ(RectsOf(kDealerMark).size(), IsSeat(dealer) ? 1u : 0u);
        }

        /// The seat of the hand shown that holds East.
        Seat EastOfTheHand() const {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (Hand().SeatWind(seat) == Wind::East) {
                    return seat;
                }
            }
            return kNoSeat;
        }

        /// The base of every plate is drawn once, after the plates of the zones and before the first tile.
        void ExpectBasePlatesInOrder() const {
            std::vector<size_t> zonePlates = CallsOf(kZonePlate);
            ASSERT_FALSE(zonePlates.empty());
            size_t firstTile = _renderer->calls.size();
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                if (_renderer->calls[i].kind == DrawKind::Texture && _renderer->calls[i].texture == _atlasTexture) {
                    firstTile = i;
                    break;
                }
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                // The HUD draws its panels in the same colour, so a plate is found by its rectangle
                size_t at = _renderer->calls.size();
                int found = 0;
                for (const size_t i : CallsOf(kSeatPlate)) {
                    if (SameRect(_renderer->calls[i].rect, PlateOf(seat))) {
                        at = i;
                        ++found;
                    }
                }
                ASSERT_EQ(found, 1) << "seat " << seat;
                EXPECT_GT(at, zonePlates.back()) << "seat " << seat << ": after the plates of the zones";
                EXPECT_LT(at, firstTile) << "seat " << seat << ": before the tiles";
            }
        }

        // ----- the centre panel -----

        /// The bar of the turn light at screen position `position`, worked out here from the panel of the last layout.
        Rect LightAt(int position) const {
            const Rect panel = _table->GetLayout().zones.centerPanel;
            const float width = kPanelTurnLightWidth;
            switch (position) {
                case 1:
                    return Rect(panel.x + panel.w - width, panel.y, width, panel.h);
                case 2:
                    return Rect(panel.x, panel.y, panel.w, width);
                case 3:
                    return Rect(panel.x, panel.y, width, panel.h);
                default:
                    return Rect(panel.x, panel.y + panel.h - width, panel.w, width);
            }
        }

        /// The mark of the dealer at screen position `position` in the design view (the panel is 472, 282, 336 x 140; the top and the bottom cell
        /// 32 high, a side cell 108 wide): on the boundary of the cell toward the middle.
        static Rect DealerMarkAt(int position) {
            const float thickness = kDealerMarkHeight;
            switch (position) {
                case 1:
                    return Rect(808.0f - 108.0f - thickness, 314.0f, thickness, 76.0f);
                case 2:
                    return Rect(472.0f, 314.0f, 336.0f, thickness);
                case 3:
                    return Rect(472.0f + 108.0f, 314.0f, thickness, 76.0f);
                default:
                    return Rect(472.0f, 422.0f - 32.0f - thickness, 336.0f, thickness);
            }
        }

        /// The light is on exactly the side of `turn` (no seat: none) and the mark of the dealer in exactly the cell of `dealer`, each drawn once,
        /// inside the panel, after the ground of the panel and before the first seat plate and the first tile.
        void ExpectPanelMarks(Seat turn, Seat dealer) const {
            const Seat viewer = _table->GetLayout().viewer;
            const Rect panel = _table->GetLayout().zones.centerPanel;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const int position = SeatPosition(viewer, seat);
                EXPECT_EQ(CountOf(kPanelTurnLight, LightAt(position)), seat == turn ? 1 : 0) << "light, seat " << seat << " at " << position;
                EXPECT_EQ(CountOf(kPanelDealerMark, DealerMarkAt(position)), seat == dealer ? 1 : 0) << "dealer, seat " << seat << " at " << position;
            }
            EXPECT_EQ(RectsOf(kPanelTurnLight).size(), IsSeat(turn) ? 1u : 0u);
            EXPECT_EQ(RectsOf(kPanelDealerMark).size(), IsSeat(dealer) ? 1u : 0u);
            std::vector<size_t> marks = CallsOf(kPanelTurnLight);
            for (const size_t call : CallsOf(kPanelDealerMark)) {
                marks.push_back(call);
            }
            std::vector<size_t> grounds;
            for (const size_t i : CallsOf(kSeatPlate)) {
                if (SameRect(_renderer->calls[i].rect, panel)) {
                    grounds.push_back(i);
                }
            }
            ASSERT_EQ(grounds.size(), 1u) << "one ground";
            const std::vector<size_t> zonePlates = CallsOf(kZonePlate);
            ASSERT_FALSE(zonePlates.empty());
            EXPECT_GT(grounds[0], zonePlates.back()) << "after the plates of the zones";
            size_t firstTile = _renderer->calls.size();
            size_t firstSeatPlate = _renderer->calls.size();
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                const DrawCall& call = _renderer->calls[i];
                if (call.kind == DrawKind::Texture && call.texture == _atlasTexture) {
                    firstTile = eastl::min(firstTile, i);
                }
                if (call.kind == DrawKind::Rect && SameColor(call.color, kSeatPlate) && SameRect(call.rect, PlateOf(kSeat0))) {
                    firstSeatPlate = eastl::min(firstSeatPlate, i);
                }
            }
            EXPECT_LT(grounds[0], firstTile) << "before the tiles";
            EXPECT_LT(grounds[0], firstSeatPlate) << "before the seat plates";
            for (const size_t call : marks) {
                EXPECT_GT(call, grounds[0]) << "a mark lies over the ground";
                EXPECT_LT(call, firstTile) << "and under the tiles";
                const Rect& rect = _renderer->calls[call].rect;
                EXPECT_GE(rect.x, panel.x - kEpsilon);
                EXPECT_GE(rect.y, panel.y - kEpsilon);
                EXPECT_LE(rect.x + rect.w, panel.x + panel.w + kEpsilon);
                EXPECT_LE(rect.y + rect.h, panel.y + panel.h + kEpsilon);
            }
        }

        /// `SpecOf(kPlainHand, "5p")` with `east` as the dealer: the turn is on `east` too.
        static HandModel EastedAt(Seat east) {
            return MakeModel(SpecOf(kPlainHand, "5p"), east);
        }

        /// A match with a person at `person` only.
        static SeatSetup PersonAt(Seat person) {
            SeatSetup seats = kAllComputers;
            seats[static_cast<size_t>(person)] = SeatKind::Person;
            return seats;
        }

        /// Plays a match of four computers (8 hands) to the end through the session; `perHand` is called when a hand is over.
        template <typename Fn>
        void PlayMatchToTheEnd(Fn perHand) {
            MahjongSettings settings = _game->GetSettings();
            settings.handsPerMatch = kShortMatchHands;
            _game->SetSettings(settings);
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            TableSession& session = _game->GetSession();
            // The counted hands plus the drawn ones that are replayed: far more than a match of 8 needs, so only a stuck match ends the loop
            constexpr int kMaxHandsGuard = 100;
            for (int guard = 0; guard < kMaxHandsGuard && session.GetPhase() != SessionPhase::MatchOver; ++guard) {
                session.RunComputers();
                if (session.GetPhase() == SessionPhase::HandOver) {
                    perHand();
                    ASSERT_TRUE(session.NextHand().has_value());
                }
            }
            ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);
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

    TEST_F(MahjongTableMarksTest, AWinOnADiscardMarksNoDiscardEvenWhenTheDiscarderHadDiscardedTheSameTileBefore) {
        // Seat 0 declares with its first discard and waits on the red dragon; seat 3 discards one (seat 0 passes), later claims a pung of
        // the 9s that seat 0 discards and discards its second red dragon: seat 0 wins on it, and the entry left at the end of the pile
        // is seat 3's first red dragon, the same seat and the same tile as the move
        FixedWallSpec spec = SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd 1s 9s");
        spec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 9s 3m 6m 9m Sw Rd Rd";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("9p"), true).has_value());
        ASSERT_TRUE(model.Discard(kSeat1, T("Wd")).has_value());
        ASSERT_TRUE(model.Discard(kSeat2, T("Gd")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        ASSERT_TRUE(model.Discard(kSeat3, T("Rd")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Pass(kSeat0).has_value());
        ASSERT_TRUE(model.Discard(kSeat0, T("9s")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat3, ClaimKind::Pung).has_value());
        ASSERT_TRUE(model.Discard(kSeat3, T("Rd")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Mahjong).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_EQ(model.Result()->source, WinSource::Discard);
        ASSERT_EQ(model.Moves().back().kind, MoveKind::Discard);
        ASSERT_EQ(model.Discards().back().seat, kSeat3);
        ASSERT_EQ(model.Discards().back().tile, T("Rd"));

        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        EXPECT_TRUE(RectsOf(kLastDiscardMark).empty()) << "the discard that won left the pile; the earlier one is not the last move";
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

    TEST_F(MahjongTableMarksTest, TheTileTheViewerHasJustDrawnIsTintedInPlaceAndTheMarkGoesAfterTheDiscard) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "5p"))));
        ASSERT_EQ(Hand().DrawnTile(kSeat0), T("5p"));

        const int drawn = IndexOf(kSeat0, T("5p"));
        ASSERT_GE(drawn, 0);
        EXPECT_NE(drawn, static_cast<int>(Hand().GetHand(kSeat0).concealed.size()) - 1) << "5p sorts into the middle of the hand";
        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(drawn));
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0));

        // After the discard seat 0 holds no drawn tile: the plain row of 13 and no tint
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeDiscard(T("1p"), false)).has_value());
        Draw();

        EXPECT_EQ(Hand().DrawnTile(kSeat0), Tile{});
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
        EXPECT_TRUE(RectsOf(kDrawnTileMark).empty());
    }

    TEST_F(MahjongTableMarksTest, AClickOnTheDrawnTileDiscardsThatTile) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "5p"))));
        const Tile drawn = Hand().DrawnTile(kSeat0);
        ASSERT_TRUE(drawn.IsValid());
        const int index = IndexOf(kSeat0, drawn);
        ASSERT_GE(index, 0);

        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(index));
        EXPECT_FALSE(ClickHandTile(index)) << "no canvas takes the click: the table does";

        ASSERT_EQ(Hand().Discards().size(), 1u);
        EXPECT_EQ(Hand().Discards().front().tile, drawn);
        EXPECT_EQ(Hand().Discards().front().seat, kSeat0);
        EXPECT_EQ(Hand().GetHand(kSeat0).concealed.size(), static_cast<size_t>(kDealtTiles)) << "the other 13 stay";
    }

    TEST_F(MahjongTableMarksTest, TheDrawnTileIsTheLastCopyWhenTheHandHoldsMore) {
        // 5p 5p in the hand and a 5p drawn: the tinted tile is a 5p, the one that sorted last among the three
        FixedWallSpec spec = SpecOf("5p 5p 4s 7s 1m 4m 7m 1p 4p 7p Sw Ew Gd", "5p");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        ASSERT_EQ(Hand().DrawnTile(kSeat0), T("5p"));

        int last = -1;
        const PlayerHand& held = Hand().GetHand(kSeat0);
        for (size_t i = 0; i < held.concealed.size(); ++i) {
            last = held.concealed[i] == T("5p") ? static_cast<int>(i) : last;
        }
        ASSERT_GE(last, 2);
        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(last));
    }

    TEST_F(MahjongTableMarksTest, NoTileIsTintedAfterAClaimOfAPung) {
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
        EXPECT_TRUE(RectsOf(kDrawnTileMark).empty());
    }

    TEST_F(MahjongTableMarksTest, TheLooseTileAfterAKongIsTinted) {
        // A concealed kong, then the loose tile
        FixedWallSpec own = SpecOf(kThree1pHand, "1p");
        own.loose = "9m";
        HandModel model = MakeModel(own);
        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T("1p")).has_value());
        ASSERT_EQ(model.DrawnTile(kSeat0), T("9m"));
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(IndexOf(kSeat0, T("9m"))));
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

        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(IndexOf(kSeat0, T("9m"))));
    }

    TEST_F(MahjongTableMarksTest, NoPeekingTheCoverTheSpectatorAndAnOpponentShowNoTintOnTheHand) {
        // Two people: the screen is covered at the start, the hand of seat 0 (14 tiles, one drawn) is backs in a plain row
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Lay(MakeModel(SpecOf(kPlainHand, "5p")));
        Draw();
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        ASSERT_TRUE(Hand().DrawnTile(kSeat0).IsValid());
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
        EXPECT_TRUE(RectsOf(kDrawnTileMark).empty());
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kNoSeat));

        session.Reveal();
        Draw();

        ASSERT_FALSE(session.HandoffPending());
        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(IndexOf(kSeat0, T("5p"))));

        // A spectator: no person at the table
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Lay(MakeModel(SpecOf(kPlainHand, "5p")));
        Draw();
        ASSERT_TRUE(Hand().DrawnTile(kSeat0).IsValid());
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
        EXPECT_TRUE(RectsOf(kDrawnTileMark).empty());

        // An opponent that has just drawn: seat 1 is East, the person sits at seat 0
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Lay(MakeModel(SpecOf(kPlainHand, "5p"), kSeat1));
        Draw();
        ASSERT_TRUE(Hand().DrawnTile(kSeat1).IsValid());
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat1));
        ASSERT_NO_FATAL_FAILURE(ExpectPlainRow(kSeat0));
        EXPECT_TRUE(RectsOf(kDrawnTileMark).empty()) << "the opponent's drawn tile is its own secret";
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

    TEST_F(MahjongTableMarksTest, TheTintOfTheDrawnTileIsInTheGoldFamilyAndDiffersFromTheOtherMarks) {
        // The style: a mark on the table is gold and nothing else is (`UiPalette.h`); each mark has an alpha of its own
        EXPECT_EQ(kDrawnTileMark.r, kTurnMark.r);
        EXPECT_EQ(kDrawnTileMark.g, kTurnMark.g);
        EXPECT_EQ(kDrawnTileMark.b, kTurnMark.b);
        for (const Color& other : {kTurnMark, kPendingFrame, kLastDiscardMark, kClaimedEdge, kClaimHighlight, kClaimLostFrame, kDeclareMark, kAdvisedFrame, kVeil, kTileShadow, kZonePlate}) {
            EXPECT_FALSE(SameColor(kDrawnTileMark, other));
        }
    }

    // ----- a declared calling hand -----

    TEST_F(MahjongTableMarksTest, ASeatThatHasDeclaredItsCallingHandHasAFrameRoundItsHandAndNoOtherSeatHas) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "5p"))));
        EXPECT_TRUE(RectsOf(kCallingFrame).empty()) << "nobody has declared at the start";

        // Seat 2 (the opponent at the top for the person at seat 0) declares in the lap; seat 1 is to act
        HandModel model = OpenPungModel(kSeat1Hand, kSeat2Waits, kSeat3Waits, {kSeat2});
        ASSERT_TRUE(model.GetHand(kSeat2).callingDeclared);
        ASSERT_FALSE(model.GetHand(kSeat1).callingDeclared);
        Lay(model);
        Draw();

        const std::vector<Rect> bars = RectsOf(kCallingFrame);
        ASSERT_EQ(bars.size(), 4u);
        ASSERT_NO_FATAL_FAILURE(ExpectFramed(kSeat2));
        ASSERT_NO_FATAL_FAILURE(ExpectFramesInsideTheView());
        // The bars lie on the border of the padded hand: none inside the hand's own rect
        const Rect hand = BoundsOf(_table->GetLayout(), SlotRole::HandTile, kSeat2);
        for (const Rect& bar : bars) {
            const bool inside = bar.x >= hand.x && bar.y >= hand.y && bar.x + bar.w <= hand.x + hand.w && bar.y + bar.h <= hand.y + hand.h;
            EXPECT_FALSE(inside);
        }

        // The frame stays after another seat's discard
        ASSERT_TRUE(model.Discard(kSeat1, T("8p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        Lay(model);
        Draw();
        EXPECT_EQ(RectsOf(kCallingFrame).size(), 4u);
        ASSERT_NO_FATAL_FAILURE(ExpectFramed(kSeat2));
    }

    TEST_F(MahjongTableMarksTest, EverySeatThatHasDeclaredIsFramedAtEachOfTheFourPlacesOnTheScreen) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, AllDeclaredModel()));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            ASSERT_TRUE(Hand().GetHand(seat).callingDeclared) << "seat " << seat;
        }

        // The viewer is seat 0 at the bottom, then seats 1, 2 and 3 at the right, the top and the left
        EXPECT_EQ(RectsOf(kCallingFrame).size(), 16u);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            ASSERT_NO_FATAL_FAILURE(ExpectFramed(seat));
        }
        ASSERT_NO_FATAL_FAILURE(ExpectFramesInsideTheView());

        // Two declared seats give eight bars
        Lay(UpgradeModel({kSeat2, kSeat3}));
        Draw();
        EXPECT_EQ(RectsOf(kCallingFrame).size(), 8u);
        ASSERT_NO_FATAL_FAILURE(ExpectFramed(kSeat2));
        ASSERT_NO_FATAL_FAILURE(ExpectFramed(kSeat3));
    }

    TEST_F(MahjongTableMarksTest, TheFrameLiesBehindTheTilesOfItsHand) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, AllDeclaredModel()));
        const std::vector<size_t> frames = CallsOf(kCallingFrame);
        ASSERT_EQ(frames.size(), 16u);
        const size_t lastFrame = frames.back();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            for (const TileSlot* slot : HandSlots(seat)) {
                size_t call = _renderer->calls.size();
                for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                    const DrawCall& drawn = _renderer->calls[i];
                    if (drawn.kind == DrawKind::Texture && drawn.texture == _atlasTexture && drawn.rect.x == slot->rect.x && drawn.rect.y == slot->rect.y) {
                        call = i;
                    }
                }
                ASSERT_LT(call, _renderer->calls.size()) << "seat " << seat;
                EXPECT_LT(lastFrame, call) << "the frame of every seat is drawn before every tile of the hands";
            }
        }
    }

    TEST_F(MahjongTableMarksTest, TheFrameIsPublicSoItShowsUnderTheCoverAndAtTheEndOfTheHand) {
        // Two people: the screen is covered, and the hand of seat 0 is backs, but the declaration is public
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, AllDeclaredModel()));
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        EXPECT_EQ(RectsOf(kCallingFrame).size(), 16u);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            ASSERT_NO_FATAL_FAILURE(ExpectFramed(seat));
        }
        session.Reveal();
        Draw();
        EXPECT_EQ(RectsOf(kCallingFrame).size(), 16u);

        // Seat 0 wins with the 5p it draws: the hand is over, all four hands face up, the frames stay
        HandModel won = AllDeclaredModel("5p");
        ASSERT_TRUE(won.DeclareWin(kSeat0).has_value());
        ASSERT_EQ(won.GetPhase(), Phase::Finished);
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, won));
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        EXPECT_EQ(RectsOf(kCallingFrame).size(), 16u);

        // The next hand starts without a declaration, so without a frame
        ASSERT_TRUE(_game->GetSession().NextHand().has_value());
        Draw();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::Playing);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_FALSE(Hand().GetHand(seat).callingDeclared);
        }
        EXPECT_TRUE(RectsOf(kCallingFrame).empty());
    }

    TEST_F(MahjongTableMarksTest, TheFrameOfADeclaredHandIsInTheGoldFamilyAndDiffersFromTheOtherMarks) {
        EXPECT_EQ(kCallingFrame.r, kTurnMark.r);
        EXPECT_EQ(kCallingFrame.g, kTurnMark.g);
        EXPECT_EQ(kCallingFrame.b, kTurnMark.b);
        for (const Color& other :
             {kTurnMark, kPendingFrame, kLastDiscardMark, kDrawnTileMark, kClaimedEdge, kClaimHighlight, kClaimLostFrame, kDeclareMark, kAdvisedFrame, kVeil, kTileShadow, kZonePlate}) {
            EXPECT_FALSE(SameColor(kCallingFrame, other));
        }
    }

    // ----- the discards that keep a declared hand waiting -----

    TEST_F(MahjongTableMarksTest, EveryTileThatKeepsTheViewersDeclaredHandWaitingHasABarInsideItsBottomEdge) {
        // Seat 0 holds the winning hand with the 5p it drew: several discards leave the 13 tiles waiting
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, AllDeclaredModel("5p")));
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::Playing);
        const std::set<int> safe = SafeDiscards();
        ASSERT_GE(safe.size(), 3u);
        ASSERT_NO_FATAL_FAILURE(ExpectKeepWaitingBarsOn(safe));
        EXPECT_TRUE(RectsOf(kDeclareMark).empty()) << "the bar of the hints is for a hand that has not declared";

        // Not tied to the switch of the hints
        for (const bool hintsOn : {false, true}) {
            _game->SetHintsOn(hintsOn);
            Draw();
            ASSERT_NO_FATAL_FAILURE(ExpectKeepWaitingBarsOn(safe)) << "hints " << hintsOn;
            EXPECT_TRUE(RectsOf(kDeclareMark).empty()) << "hints " << hintsOn;
        }

        // A hand that cannot win this turn: one bar, on the tile that is no part of the shape
        Lay(AllDeclaredModel("Rd"));
        Draw();
        ASSERT_NO_FATAL_FAILURE(ExpectKeepWaitingBarsOn(SafeDiscards()));
        EXPECT_EQ(RectsOf(kKeepsWaitingMark).size(), 1u);
        EXPECT_EQ(SafeDiscards(), std::set<int>{T("Rd").Id()});
    }

    TEST_F(MahjongTableMarksTest, TheBarAndTheTintOfTheDrawnTileCanLieOnTheSameTile) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, AllDeclaredModel("Rd")));
        const int drawn = IndexOf(kSeat0, T("Rd"));
        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(drawn));
        const Rect& rect = SlotOf(SlotRole::HandTile, kSeat0, drawn)->rect;
        const std::vector<size_t> tints = CallsOf(kDrawnTileMark);
        const std::vector<size_t> bars = CallsOf(kKeepsWaitingMark);
        ASSERT_EQ(bars.size(), 1u);
        const Rect& bar = _renderer->calls[bars[0]].rect;
        EXPECT_GE(bar.x, rect.x);
        EXPECT_LE(bar.x + bar.w, rect.x + rect.w + kEpsilon);
        EXPECT_GE(bar.y, rect.y);
        EXPECT_LE(bar.y + bar.h, rect.y + rect.h + kEpsilon);
        EXPECT_LT(tints[0], bars[0]) << "the bar lies over the tint";
    }

    TEST_F(MahjongTableMarksTest, NoBarBeforeTheDeclarationWhileAComputerActsInAClaimWindowOrAfterTheDiscard) {
        // Not declared: the hand waits after a discard (it could declare), but it has not
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kWaitingHand0, "9s"))));
        ASSERT_FALSE(Hand().GetHand(kSeat0).callingDeclared);
        EXPECT_TRUE(RectsOf(kKeepsWaitingMark).empty());

        // Declared and on turn: the bar is there
        HandModel model = ViewerDeclaredModel();
        Lay(model);
        Draw();
        ASSERT_EQ(RectsOf(kKeepsWaitingMark).size(), 2u) << "both copies of the 9s";

        // The viewer discards the 9s (the bar is gone with the turn): a computer is to act
        ASSERT_TRUE(model.Discard(kSeat0, T("9s")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        ASSERT_EQ(model.CurrentSeat(), kSeat1);
        Lay(model);
        Draw();
        EXPECT_TRUE(RectsOf(kKeepsWaitingMark).empty());

        // Seat 1 discards the south wind and the declared viewer, who holds a pair of them, may pung: a window is open
        ASSERT_TRUE(model.Discard(kSeat1, T("Sw")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_FALSE(model.PendingClaimSeats().empty());
        Lay(model);
        Draw();
        EXPECT_TRUE(RectsOf(kKeepsWaitingMark).empty());
    }

    TEST_F(MahjongTableMarksTest, NoBarUnderTheCoverForASpectatorOrAtTheEndOfTheHandAndTheCoverGoesWithReveal) {
        // Two people: the screen is covered, the viewer's tiles are backs and nothing says which discards are safe
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, AllDeclaredModel("5p")));
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        EXPECT_TRUE(RectsOf(kKeepsWaitingMark).empty());
        session.Reveal();
        Draw();
        ASSERT_FALSE(session.HandoffPending());
        ASSERT_NO_FATAL_FAILURE(ExpectKeepWaitingBarsOn(SafeDiscards()));
        EXPECT_FALSE(RectsOf(kKeepsWaitingMark).empty());

        // A spectator (no person at the table) sees no concealed hand
        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, AllDeclaredModel("5p")));
        ASSERT_EQ(_game->GetSession().Viewer(), kNoSeat);
        EXPECT_TRUE(RectsOf(kKeepsWaitingMark).empty());

        // The hand is over: all hands face up, no choice of a discard left
        HandModel won = AllDeclaredModel("5p");
        ASSERT_TRUE(won.DeclareWin(kSeat0).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, won));
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        EXPECT_TRUE(RectsOf(kKeepsWaitingMark).empty());
    }

    TEST_F(MahjongTableMarksTest, TheBarOfASafeDiscardIsInTheGoldFamilyAndDiffersFromTheOtherMarks) {
        EXPECT_EQ(kKeepsWaitingMark.r, kTurnMark.r);
        EXPECT_EQ(kKeepsWaitingMark.g, kTurnMark.g);
        EXPECT_EQ(kKeepsWaitingMark.b, kTurnMark.b);
        for (const Color& other :
             {kTurnMark, kPendingFrame, kLastDiscardMark, kDrawnTileMark, kCallingFrame, kClaimedEdge, kClaimHighlight, kClaimLostFrame, kDeclareMark, kAdvisedFrame, kVeil, kTileShadow, kZonePlate}) {
            EXPECT_FALSE(SameColor(kKeepsWaitingMark, other));
        }
        EXPECT_GT(kKeepsWaitingBarHeight, 0.0f);
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
        EXPECT_TRUE(RectsOf(kDrawnTileMark).empty());
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kNoSeat));

        session.Reveal();
        Draw();

        ASSERT_FALSE(session.HandoffPending());
        EXPECT_EQ(RectsOf(kClaimedEdge).size(), 1u);
        EXPECT_EQ(RectsOf(kLastDiscardMark).size(), 1u);
        ASSERT_NO_FATAL_FAILURE(ExpectDrawnMarked(IndexOf(kSeat1, Hand().DrawnTile(kSeat1))));
    }

    // ----- the set that was just laid -----

    TEST_F(MahjongTableMarksTest, AClaimByAComputerStartsTheHighlightEvenWhenTheClaimerDiscardsInTheSameStep) {
        FixedWallSpec spec = SpecOf(kPlainHand, "Rd");
        spec.hands[kSeat1] = kSeat1HoldsRedPair;
        ASSERT_NO_FATAL_FAILURE(StartThenLay(kDefaultSeatSetup, MakeModel(spec)));
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "no move yet";
        ASSERT_FALSE(ClickHandTile(IndexOf(kSeat0, T("Rd"))));
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "a discard alone lays no set";
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);

        Elapse(kComputerStepSeconds);
        Draw();

        const eastl::vector<HandMove>& moves = Hand().Moves();
        ASSERT_EQ(moves.size(), 3u);
        EXPECT_EQ(moves[1].kind, MoveKind::ClaimPung);
        EXPECT_EQ(moves[2].kind, MoveKind::Discard) << "the claimer discarded within the same pause";
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0));
        // Behind the tiles of the set: before the shadow of its first tile
        const TileSlot* first = SlotOf(SlotRole::MeldTile, kSeat1, 0, 0);
        ASSERT_NE(first, nullptr);
        const std::vector<size_t> shadows = CallsOf(kTileShadow);
        const auto shadow =
            std::find_if(shadows.begin(), shadows.end(), [&](size_t i) { return _renderer->calls[i].rect.x == first->rect.x + 2.0f && _renderer->calls[i].rect.y == first->rect.y + 2.0f; });
        ASSERT_NE(shadow, shadows.end());
        EXPECT_LT(CallsOf(kClaimHighlight).front(), *shadow);
    }

    TEST_F(MahjongTableMarksTest, TheHighlightBlinksAndIsGoneAfterItsTimeAndNotBackByRedrawing) {
        ASSERT_NO_FATAL_FAILURE(StartThenLay(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsPairOf5p)));
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeClaim(ClaimKind::Pung)).has_value());

        Draw();
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0)) << "on at the moment of the move";
        Draw();
        EXPECT_EQ(RectsOf(kClaimHighlight).size(), 1u) << "drawing again changes nothing";

        Elapse(kOffHalf);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "the second half of the period";
        Elapse(kEndOfPeriod);
        Draw();
        EXPECT_EQ(RectsOf(kClaimHighlight).size(), 1u) << "the next period begins";
        Elapse(kOffHalf);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
        Elapse(kEndOfPeriod);
        Draw();
        EXPECT_EQ(RectsOf(kClaimHighlight).size(), 1u);

        Elapse(kClaimHighlightSeconds);
        for (int i = 0; i < 3; ++i) {
            Draw();
            EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "over for good, frame " << i;
            Elapse(kEndOfPeriod);
        }
    }

    TEST_F(MahjongTableMarksTest, AnUpgradedKongRestartsTheHighlightOnItsSet) {
        ASSERT_NO_FATAL_FAILURE(StartThenLay(kPersonAtSeat1, OpenPungModel(kSeat1Hand)));
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0)) << "the pung that seat 1 laid";
        Elapse(1.5f);
        Draw();
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0)) << "1.5 s is a whole number of blinks, a half second is left";

        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeUpgradeKong(T("5p"))).has_value());
        ASSERT_EQ(Hand().Moves().back().kind, MoveKind::UpgradeKong);
        Draw();
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0));
        Elapse(1.5f);
        Draw();

        EXPECT_EQ(Hand().GetHand(kSeat1).melds[0].kind, MeldKind::Kong);
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0)) << "the time began again with the new move, so 1.5 s later it is still there";
    }

    TEST_F(MahjongTableMarksTest, AConcealedKongHighlightsItsSetToo) {
        FixedWallSpec spec = SpecOf(kThree1pHand, "1p");
        spec.loose = "9m";
        ASSERT_NO_FATAL_FAILURE(StartThenLay(kDefaultSeatSetup, MakeModel(spec)));
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());

        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeConcealedKong(T("1p"))).has_value());
        Draw();

        ASSERT_EQ(Hand().Moves().back().kind, MoveKind::ConcealedKong);
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat0, 0));
        EXPECT_TRUE(RectsOf(kClaimedEdge).empty());
    }

    TEST_F(MahjongTableMarksTest, ANewHandOrMatchStartsWithoutAHighlight) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "nothing has happened at the start of a hand";

        ASSERT_NO_FATAL_FAILURE(StartThenLay(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsPairOf5p)));
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeClaim(ClaimKind::Pung)).has_value());
        Draw();
        ASSERT_EQ(RectsOf(kClaimHighlight).size(), 1u);

        _game->Reset();
        Draw();

        EXPECT_TRUE(Hand().Moves().empty());
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "a new match";
    }

    TEST_F(MahjongTableMarksTest, TheClockRunsUnderTheCoverAndStopsWhileTheSeatChoiceIsOpen) {
        // Seat 1 has pungged the 5p of seat 0 and is to act; two people, the screen is covered until "Show"
        ASSERT_NO_FATAL_FAILURE(StartThenLay(kTwoPeople, OpenPungModel(kSeat1Hand)));
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0)) << "an open set is public, so the cover hides nothing here";

        _game->OpenSetup();
        Elapse(10.0f);
        _game->CloseSetup();
        Draw();
        ASSERT_NO_FATAL_FAILURE(ExpectHighlightOn(kSeat1, 0)) << "no time passes while the seat choice is open";

        Elapse(1.0f + kOffHalf);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "the clock did not wait for the screen to be shown: this is the off half of a period";

        session.Reveal();
        Elapse(kClaimHighlightSeconds);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
    }

    // ----- the seat plates -----

    TEST_F(MahjongTableMarksTest, EverySeatHasABasePlateAtItsRectangleForEachViewerAfterTheZonePlatesAndBeforeTheTiles) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            SeatSetup seats{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
            seats[static_cast<size_t>(viewer)] = SeatKind::Person;
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Draw();
            ASSERT_EQ(_table->GetLayout().viewer, viewer);
            ASSERT_NO_FATAL_FAILURE(ExpectBasePlatesInOrder()) << "viewer " << viewer;
            // The viewer's plate is the one at the bottom of the table, and the four rectangles differ
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != viewer) {
                    EXPECT_LT(PlateOf(seat).y, PlateOf(viewer).y) << "viewer " << viewer << " seat " << seat;
                    EXPECT_FALSE(SameRect(PlateOf(seat), PlateOf(viewer)));
                }
            }
        }
    }

    TEST_F(MahjongTableMarksTest, TheTurnTintAndFrameLieOnThePlateOfTheSeatWhoseTurnItIsAndMoveWithTheNextDiscard) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat0, kSeat0));

        EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("9p"))));
        Draw();

        ASSERT_EQ(Hand().CurrentSeat(), kSeat1);
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat1, kSeat0));
    }

    TEST_F(MahjongTableMarksTest, InAClaimWindowTheTurnStaysOnTheDiscarderAndNeverMovesToTheSeatThatIsAskedFirst) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, EastDiscardsFive(kSeat1HoldsPairOf5p)));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);

        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat0, kSeat0));
    }

    TEST_F(MahjongTableMarksTest, NoTurnIsMarkedAtTheEndOfAHandAndOfTheMatchAndTheDealerStaysOnTheEastOfTheHandShown) {
        // A hand that seat 0 (East) has won: all hands face up, the turn gone, the dealer where the winds put it
        HandModel won = AllDeclaredModel("5p");
        ASSERT_TRUE(won.DeclareWin(kSeat0).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, won));
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kNoSeat, kSeat0));

        // The whole match: after each hand and after the last one (the winds of the last hand stay, though Match::East() is the next deal's)
        std::set<Seat> dealers;
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd([&] {
            Draw();
            dealers.insert(EastOfTheHand());
            ExpectMarksOnPlates(kNoSeat, EastOfTheHand());
        }));
        Draw();
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::MatchOver);
        EXPECT_GT(dealers.size(), 1u) << "the deal moved at least once in eight hands";
        ExpectMarksOnPlates(kNoSeat, EastOfTheHand());
    }

    TEST_F(MahjongTableMarksTest, TheDealerBarAndTheTurnFollowTheWindsOfTheHandWhenTheDealMoves) {
        // Seat 1 is East of this hand (the winds are the hand's, not Match::East()): its plate has the bar, and the turn is on it too
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "5p"), kSeat1)));
        ASSERT_EQ(Hand().CurrentSeat(), kSeat1);
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat1, kSeat1));
        Lay(MakeModel(SpecOf(kPlainHand, "5p"), kSeat3));
        Draw();
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat3, kSeat3));
    }

    TEST_F(MahjongTableMarksTest, ThePlateMarksArePublicSoTheyShowUnderTheCoverAfterRevealAndForASpectator) {
        // Two people, seat 1 is East and to act: the screen is covered, the plates are not
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, MakeModel(SpecOf(kPlainHand, "5p"), kSeat1)));
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        ASSERT_EQ(_table->GetLayout().viewer, kSeat1) << "the person that waits for the cover to go is at the bottom";
        ASSERT_NO_FATAL_FAILURE(ExpectBasePlatesInOrder());
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat1, kSeat1));
        const Rect bottomBefore = PlateOf(kSeat1);

        session.Reveal();
        Draw();

        ASSERT_FALSE(session.HandoffPending());
        EXPECT_TRUE(SameRect(PlateOf(kSeat1), bottomBefore)) << "the viewer is the same person after Show";
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat1, kSeat1));

        // A spectator: seat 0 is at the bottom (nobody is the viewer), the same marks
        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, MakeModel(SpecOf(kPlainHand, "5p"), kSeat2)));
        ASSERT_NO_FATAL_FAILURE(ExpectBasePlatesInOrder());
        ASSERT_NO_FATAL_FAILURE(ExpectMarksOnPlates(kSeat2, kSeat2));
    }

    TEST_F(MahjongTableMarksTest, ThePlateColoursAreGoldForTheTurnAndNotForTheDealerAndThePlateIsTheHudPanel) {
        EXPECT_EQ(kSeatTurnFrame.r, kTurnMark.r);
        EXPECT_EQ(kSeatTurnFrame.g, kTurnMark.g);
        EXPECT_EQ(kSeatTurnFrame.b, kTurnMark.b);
        for (const Color& other : {kTurnMark, kPendingFrame, kLastDiscardMark, kDrawnTileMark, kCallingFrame, kClaimedEdge, kClaimHighlight, kClaimLostFrame, kDeclareMark, kKeepsWaitingMark,
                                   kAdvisedFrame, kVeil, kTileShadow, kZonePlate, kSeatPlate, kDealerMark}) {
            EXPECT_FALSE(SameColor(kSeatTurnFrame, other)) << "the tests find a mark by its colour";
        }
        // The dealer's mark is red: little green and blue, and no colour of the other marks
        EXPECT_LT(kDealerMark.g, kTurnMark.g / 2);
        EXPECT_LT(kDealerMark.b, kTurnMark.b);
        for (const Color& other : {kTurnMark, kPendingFrame, kLastDiscardMark, kDrawnTileMark, kCallingFrame, kClaimedEdge, kClaimHighlight, kClaimLostFrame, kDeclareMark, kKeepsWaitingMark,
                                   kAdvisedFrame, kVeil, kTileShadow, kZonePlate, kSeatPlate, kSeatTurnFrame}) {
            EXPECT_FALSE(SameColor(kDealerMark, other));
        }
        EXPECT_TRUE(SameColor(kSeatPlate, PaletteColor("hudPanel")));
    }

    // ----- the centre panel -----

    TEST_F(MahjongTableMarksTest, TheLightAndTheDealerMarkAreOnTheSideOfTheSeatForEveryViewerAndEveryDealer) {
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            for (Seat east = 0; east < kSeatCount; ++east) {
                ASSERT_NO_FATAL_FAILURE(StartWith(PersonAt(viewer), EastedAt(east)));
                ASSERT_EQ(_table->GetLayout().viewer, viewer);
                ASSERT_EQ(Hand().CurrentSeat(), east);
                EXPECT_NO_FATAL_FAILURE(ExpectPanelMarks(east, east)) << "viewer " << viewer << ", East " << east;
            }
        }
    }

    TEST_F(MahjongTableMarksTest, TheLightMovesWithTheTurnWhileTheDealerMarkStaysOnEast) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        ASSERT_NO_FATAL_FAILURE(ExpectPanelMarks(kSeat0, kSeat0));
        EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("9p"))));
        Draw();
        ASSERT_EQ(Hand().CurrentSeat(), kSeat1);
        ASSERT_NO_FATAL_FAILURE(ExpectPanelMarks(kSeat1, kSeat0));
    }

    TEST_F(MahjongTableMarksTest, InAClaimWindowTheLightStaysOnTheDiscarder) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, EastDiscardsFive(kSeat1HoldsPairOf5p)));
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);
        ASSERT_NO_FATAL_FAILURE(ExpectPanelMarks(kSeat0, kSeat0));
    }

    TEST_F(MahjongTableMarksTest, NoLightAtTheEndOfAHandAndOfTheMatchAndTheDealerMarkStaysOnTheEastOfTheHandShown) {
        HandModel won = AllDeclaredModel("5p");
        ASSERT_TRUE(won.DeclareWin(kSeat0).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, won));
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        ASSERT_NO_FATAL_FAILURE(ExpectPanelMarks(kNoSeat, kSeat0));

        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd([&] {
            Draw();
            ExpectPanelMarks(kNoSeat, EastOfTheHand());
        }));
        Draw();
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::MatchOver);
        ExpectPanelMarks(kNoSeat, EastOfTheHand());
    }

    TEST_F(MahjongTableMarksTest, ThePanelMarksArePublicSoTheyShowUnderTheCoverAfterRevealAndForASpectator) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, EastedAt(kSeat1)));
        TableSession& session = _game->GetSession();
        ASSERT_TRUE(session.HandoffPending());
        ASSERT_NO_FATAL_FAILURE(ExpectPanelMarks(kSeat1, kSeat1));

        session.Reveal();
        Draw();
        ASSERT_FALSE(session.HandoffPending());
        ASSERT_NO_FATAL_FAILURE(ExpectPanelMarks(kSeat1, kSeat1));

        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, EastedAt(kSeat2)));
        ASSERT_NO_FATAL_FAILURE(ExpectPanelMarks(kSeat2, kSeat2));
    }

    TEST_F(MahjongTableMarksTest, ThePanelLightIsGoldAndTheDealerMarkRedEachWithAnAlphaOfItsOwn) {
        EXPECT_EQ(kPanelTurnLight.r, kTurnMark.r);
        EXPECT_EQ(kPanelTurnLight.g, kTurnMark.g);
        EXPECT_EQ(kPanelTurnLight.b, kTurnMark.b);
        EXPECT_LT(kPanelDealerMark.g, kTurnMark.g / 2);
        EXPECT_LT(kPanelDealerMark.b, kTurnMark.b);
        EXPECT_EQ(kPanelDealerMark.r, kDealerMark.r);
        EXPECT_EQ(kPanelDealerMark.g, kDealerMark.g);
        EXPECT_EQ(kPanelDealerMark.b, kDealerMark.b);
        const Color others[] = {kTurnMark,     kPendingFrame, kLastDiscardMark, kDrawnTileMark, kCallingFrame, kClaimedEdge,   kClaimHighlight, kClaimLostFrame, kDeclareMark, kKeepsWaitingMark,
                                kAdvisedFrame, kVeil,         kTileShadow,      kZonePlate,     kSeatPlate,    kSeatTurnFrame, kDealerMark};
        for (const Color& other : others) {
            EXPECT_FALSE(SameColor(kPanelTurnLight, other)) << "the tests find a mark by its colour";
            EXPECT_FALSE(SameColor(kPanelDealerMark, other)) << "the tests find a mark by its colour";
        }
        EXPECT_FALSE(SameColor(kPanelTurnLight, kPanelDealerMark));
        EXPECT_GT(kPanelTurnLightWidth, 0.0f);
    }

}  // namespace BECore::Mahjong::Tests
