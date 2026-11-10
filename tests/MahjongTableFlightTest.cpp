#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Events/SceneEvents.h>
#include <Mahjong/Table/TileFlights.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The tiles that fly to their place (`Components/MahjongTableComponent.h`, "Flights"; the planner is `Table/TileFlights.h`). The shipped
 * scene is drawn into a `RecordingRenderer` as big as the view, so a rect of a draw call is a rect of the layout. The fixed hands come
 * from `FixedWall.h`; a batch is made by changing the model between two draws (as a computer's step, a click or the control API does), and
 * the clock of the table runs on `SceneUpdateEvent`. `MahjongTableTestBase` switches the flights off, these tests switch them on.
 */
namespace BECore::Mahjong::Tests {

    // The flights are over long before the next computer step, so the pace does not change
    static_assert(kTileFlightSeconds < kComputerStepSeconds);

    namespace {

        /// Seat 0 holds a hand that is far from waiting; as East it draws the first tile of the live wall.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// The same with a pair of 4p (no seat can claim a 4p: the quiet hands hold no neighbours of it).
        constexpr const char* kPairHand = "4p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        constexpr const char* kSeat1HoldsPairOf5p = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
        constexpr const char* kSeat1HoldsAChowGap = "4p 6p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
        constexpr const char* kSeat1HoldsThree5p = "5p 5p 5p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
        constexpr const char* kThree1pHand = "1p 1p 1p 4s 7s 1m 4m 7m 4p 7p Sw Ew Gd";
        constexpr const char* kSeat1HoldsRedPair = "3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw Rd Rd";
        constexpr SeatSetup kPersonAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kOnePersonAtSeat0AndOneAtSeat3{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Person};
        constexpr SeatSetup kPersonAtSeat0{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        /// A quarter of a second: exact in floats, so sums of it are exact; half a blink period (0.5 s) is a quarter.
        constexpr float kQuarter = 0.25f;
        constexpr float kHalfFlight = kTileFlightSeconds * 0.5f;
        /// The fill behind a set and the frame of a pending discard reach this far past the tiles (`kActingMargin`, `kPendingMargin` of the component).
        constexpr float kActingMargin = 4.0f;
        constexpr float kPendingMargin = 3.0f;
        /// Seat 0 waits on the 5p with a pair of winds (the others wait as `kSeat2Waits`, `kSeat3Waits`) and declares with its first discard.
        constexpr const char* kWaitingHand0 = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Sw Sw";

        Rect Padded(const Rect& rect, float margin) {
            return Rect(rect.x - margin, rect.y - margin, rect.w + 2.0f * margin, rect.h + 2.0f * margin);
        }

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

        /// Where the tile of a slot arrives, and what it shows there.
        struct Arrival {
            FlightOrigin from;
            SlotRole role = SlotRole::HandTile;
            Seat seat = 0;
            int index = 0;
            int part = 0;
            std::optional<Tile> face;
        };

        bool SameSource(const DrawCall& call, const Rect& source) {
            return call.source.has_value() && SameRect(*call.source, source);
        }

    }  // namespace

    class MahjongTableFlightTest : public MahjongTableTestBase {
    protected:
        void SetUp() override {
            MahjongTableTestBase::SetUp();
            UseFlights();
        }

        HandModel& Model() const {
            return _game->GetMatch().Hand();
        }

        TableSession& Session() const {
            return _game->GetSession();
        }

        /// A match with `seats` and a fixed model laid; the first frame is drawn (nothing flies: nothing was drawn before).
        void StartWith(const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            Draw();
            ASSERT_EQ(_table->FlightCount(), 0u) << "the first frame of a table has nothing to fly from";
        }

        static const TileSlot* Find(const TableLayout& layout, SlotRole role, Seat seat, int index, int part = 0) {
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == role && slot.seat == seat && slot.index == index && slot.part == part) {
                    return &slot;
                }
            }
            return nullptr;
        }

        static FlightOrigin RectOf(const TableLayout& layout, SlotRole role, Seat seat, int index, int part = 0) {
            const TileSlot* slot = Find(layout, role, seat, index, part);
            EXPECT_NE(slot, nullptr) << "role " << static_cast<int>(role) << " seat " << seat << " index " << index << " part " << part;
            return slot != nullptr ? FlightOrigin{slot->rect, slot->rotation} : FlightOrigin{};
        }

        /// The middle hand slot of `seat`, where a tile of a hand that shows no identity leaves from.
        static FlightOrigin MiddleHand(const TableLayout& layout, Seat seat) {
            int count = 0;
            for (const TileSlot& slot : layout.slots) {
                count += slot.role == SlotRole::HandTile && slot.seat == seat ? 1 : 0;
            }
            return RectOf(layout, SlotRole::HandTile, seat, count / 2);
        }

        static FlightOrigin LastWallStack(const TableLayout& layout) {
            const TileSlot* last = nullptr;
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::WallStack && (last == nullptr || slot.index > last->index)) {
                    last = &slot;
                }
            }
            EXPECT_NE(last, nullptr) << "the wall has stacks";
            return last != nullptr ? FlightOrigin{last->rect, last->rotation} : FlightOrigin{};
        }

        /// Whether one of the last `last` tile draws is at `rect` and shows `face` (nullopt: a back).
        bool TileAmongLast(size_t last, const Rect& rect, const std::optional<Tile>& face) const {
            const std::vector<DrawCall> calls = TileCalls();
            const Rect source = face ? SourceRect(*face) : BackRect();
            for (size_t i = calls.size() > last ? calls.size() - last : 0; i < calls.size(); ++i) {
                if (SameRect(calls[i].rect, rect) && SameSource(calls[i], source)) {
                    return true;
                }
            }
            return false;
        }

        /// The rectangle of an arrival `fraction` of the way: the flight of `arrival.from` to the slot, eased.
        Rect Where(const Arrival& arrival, float fraction) const {
            const TileSlot* slot = Find(_table->GetLayout(), arrival.role, arrival.seat, arrival.index, arrival.part);
            if (slot == nullptr) {
                ADD_FAILURE() << "the layout has no slot role " << static_cast<int>(arrival.role) << " seat " << arrival.seat << " index " << arrival.index << " part " << arrival.part;
                return Rect();
            }
            if (fraction >= 1.0f) {
                return slot->rect;
            }
            return FlightRect(TileFlight{SlotKey::Of(*slot), arrival.from.rect, arrival.from.rotation}, slot->rect, Ease(kTileFlightEase, fraction));
        }

        /// What the draw of an arrival `fraction` of the way covers: the box of the tile as turned on the way (the screen rectangle of the
        /// slot at either end; between a turned and an unturned slot a tile in the air is turned by a part of the way).
        Rect BoxWhere(const Arrival& arrival, float fraction) const {
            const TileSlot* slot = Find(_table->GetLayout(), arrival.role, arrival.seat, arrival.index, arrival.part);
            if (slot == nullptr) {
                ADD_FAILURE() << "the layout has no slot role " << static_cast<int>(arrival.role) << " seat " << arrival.seat << " index " << arrival.index << " part " << arrival.part;
                return Rect();
            }
            if (fraction >= 1.0f) {
                return slot->rect;
            }
            return BoxOf(FlightPlacement(TileFlight{SlotKey::Of(*slot), arrival.from.rect, arrival.from.rotation}, *slot, Ease(kTileFlightEase, fraction)));
        }

        /// Every slot of the layout is drawn once whatever flies: as many tile draws as slots.
        void ExpectEverySlotDrawnOnce() const {
            EXPECT_EQ(TileCalls().size(), _table->GetLayout().slots.size());
        }

        /// The state of the flights `fraction` of their time into the batch, as the last draw shows it.
        void ExpectAt(const std::vector<Arrival>& arrivals, float fraction) const {
            ExpectEverySlotDrawnOnce();
            for (const Arrival& arrival : arrivals) {
                const Rect expected = BoxWhere(arrival, fraction);
                EXPECT_TRUE(TileAmongLast(fraction >= 1.0f ? TileCalls().size() : arrivals.size(), expected, arrival.face))
                    << "fraction " << fraction << " role " << static_cast<int>(arrival.role) << " seat " << arrival.seat << " index " << arrival.index << " part " << arrival.part << " at "
                    << expected.x << "," << expected.y;
                if (fraction > 0.0f && fraction < 1.0f) {
                    const Rect to = BoxWhere(arrival, 1.0f);
                    EXPECT_FALSE(SameRect(expected, arrival.from)) << "moved";
                    EXPECT_FALSE(SameRect(expected, to)) << "not there yet";
                }
            }
        }

        /// The turned draw of the tile draw over `box` that shows `face` (nullopt: a back); null when that draw is not turned or is not there.
        const TurnedDraw* TurnedAt(const Rect& box, const std::optional<Tile>& face) const {
            const Rect source = face ? SourceRect(*face) : BackRect();
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                const DrawCall& call = _renderer->calls[i];
                if (call.kind == DrawKind::Texture && call.texture == _atlasTexture && SameRect(call.rect, box) && SameSource(call, source)) {
                    return _renderer->TurnOf(i);
                }
            }
            ADD_FAILURE() << "no tile draw over " << box.x << "," << box.y << " " << box.w << "x" << box.h;
            return nullptr;
        }

        /// The draw of an arrival `fraction` of the way is turned by `rotation` (0: not turned) about the centre of what it covers; landed, it is
        /// the unturned rectangle of its slot; on the way, the unturned size is the mean of the two ends' by the eased progress.
        void ExpectTurnAt(const Arrival& arrival, float fraction, float rotation) const {
            const TileSlot* slot = Find(_table->GetLayout(), arrival.role, arrival.seat, arrival.index, arrival.part);
            ASSERT_NE(slot, nullptr);
            const Rect box = BoxWhere(arrival, fraction);
            const TurnedDraw* turn = TurnedAt(box, arrival.face);
            if (rotation == 0.0f) {
                EXPECT_EQ(turn, nullptr) << "fraction " << fraction << " seat " << arrival.seat << " index " << arrival.index;
                return;
            }
            ASSERT_NE(turn, nullptr) << "fraction " << fraction << " seat " << arrival.seat << " index " << arrival.index;
            EXPECT_NEAR(turn->transform.rotation, rotation, 0.01f) << "fraction " << fraction;
            EXPECT_NEAR(turn->transform.pivotX, RectCentreX(box), 0.01f) << "the box of a quad turned about its centre is centred on the pivot";
            EXPECT_NEAR(turn->transform.pivotY, RectCentreY(box), 0.01f);
            const Rect fromUpright = UprightRect(arrival.from.rect, arrival.from.rotation);
            const Rect toUpright = UprightRect(slot->rect, slot->rotation);
            const float eased = fraction >= 1.0f ? 1.0f : Ease(kTileFlightEase, fraction);
            EXPECT_NEAR(turn->upright.w, fromUpright.w + (toUpright.w - fromUpright.w) * eased, 0.01f) << "fraction " << fraction;
            EXPECT_NEAR(turn->upright.h, fromUpright.h + (toUpright.h - fromUpright.h) * eased, 0.01f) << "fraction " << fraction;
            if (fraction >= 1.0f) {
                EXPECT_TRUE(SameRect(turn->upright, toUpright)) << "it lands as its slot";
                EXPECT_EQ(turn->transform.rotation, slot->rotation);
            }
        }

        /// The batch just drawn flies exactly `arrivals`: at its start, half way and landed (the clock of the table moves between draws).
        void ExpectFlightsRun(const std::vector<Arrival>& arrivals) {
            ASSERT_EQ(_table->FlightCount(), arrivals.size());
            ExpectAt(arrivals, 0.0f);
            AdvanceTime(kHalfFlight);
            Draw();
            ASSERT_EQ(_table->FlightCount(), arrivals.size());
            ExpectAt(arrivals, 0.5f);
            AdvanceTime(kHalfFlight);
            Draw();
            EXPECT_EQ(_table->FlightCount(), 0u) << "the flights are over after kTileFlightSeconds";
            ExpectAt(arrivals, 1.0f);
        }

        /// East discards a 5p that seat 1 (holding `seat1`) may claim.
        static HandModel EastDiscardsFive(const char* seat1) {
            FixedWallSpec spec = SpecOf(kPlainHand, "5p");
            spec.hands[kSeat1] = seat1;
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// The meld tile slots of `seat`'s meld `meld` in the last layout, by part.
        std::vector<const TileSlot*> MeldSlots(Seat seat, int meld) const {
            std::vector<const TileSlot*> found;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == SlotRole::MeldTile && slot.seat == seat && slot.index == meld) {
                    found.push_back(&slot);
                }
            }
            return found;
        }

        /// The arrivals of a claimed set of `seat` (meld 0): the claimed tile comes from `claimedFrom`, the other tiles from `fromHand` in order.
        std::vector<Arrival> ClaimArrivals(Seat seat, const FlightOrigin& claimedFrom, const std::vector<FlightOrigin>& fromHand, const Tile claimedTile) const {
            std::vector<Arrival> arrivals;
            size_t next = 0;
            for (const TileSlot* slot : MeldSlots(seat, 0)) {
                Arrival arrival;
                arrival.role = SlotRole::MeldTile;
                arrival.seat = seat;
                arrival.index = 0;
                arrival.part = slot->part;
                arrival.face = LaidTile(Model().GetHand(seat).melds[0], slot->part);
                if (slot->claimEdge != Edge::None) {
                    EXPECT_EQ(arrival.face, claimedTile);
                    arrival.from = claimedFrom;
                } else if (next < fromHand.size()) {
                    arrival.from = fromHand[next++];
                } else {
                    ADD_FAILURE() << "more tiles in the set than sources";
                }
                arrivals.push_back(arrival);
            }
            EXPECT_EQ(next, fromHand.size());
            return arrivals;
        }

        /// The arrival of a pile entry.
        static Arrival Pile(const FlightOrigin& from, Seat seat, int entry, Tile tile) {
            Arrival arrival;
            arrival.from = from;
            arrival.role = SlotRole::DiscardTile;
            arrival.seat = seat;
            arrival.index = entry;
            arrival.face = tile;
            return arrival;
        }

        /// The arrival of the tile `seat` has just drawn: from the last stack of the wall, a face only for the viewer.
        Arrival DrawnBy(const TableLayout& before, Seat seat) const {
            const HandModel& hand = Hand();
            Arrival arrival;
            arrival.from = LastWallStack(before);
            arrival.role = SlotRole::HandTile;
            arrival.seat = seat;
            if (seat == _table->GetLayout().viewer && ViewerSeesHand()) {
                arrival.index = IndexOf(seat, hand.DrawnTile(seat));
                arrival.face = hand.DrawnTile(seat);
            } else {
                arrival.index = static_cast<int>(hand.GetHand(seat).concealed.size()) - 1;
            }
            return arrival;
        }

        /// The viewer's concealed tiles are face up: the hand is over, or the viewer is the person at the screen and no cover is up.
        bool ViewerSeesHand() const {
            const TableSession& session = _game->GetSession();
            return session.GetPhase() != SessionPhase::Playing || (!session.HandoffPending() && session.Viewer() == _table->GetLayout().viewer);
        }

        /// The tile draws that show a face, counted from the draw calls (the order of a frame with flights is not the order of the slots).
        int FaceUpCalls() const {
            int faces = 0;
            for (const DrawCall& call : TileCalls()) {
                faces += SameSource(call, BackRect()) ? 0 : 1;
            }
            return faces;
        }

        /// The tiles that are public in the last layout: the pile, the bonus tiles and the open sets.
        int PublicFaces() const {
            int count = 0;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == SlotRole::DiscardTile || slot.role == SlotRole::BonusTile) {
                    ++count;
                } else if (slot.role == SlotRole::MeldTile && !Hand().GetHand(slot.seat).melds[static_cast<size_t>(slot.index)].concealed) {
                    ++count;
                }
            }
            return count;
        }

        /// No concealed tile is face up: only public tiles show a face.
        void ExpectNoConcealedFace() const {
            EXPECT_EQ(FaceUpCalls(), PublicFaces());
        }

        /// Seat 0 has declared with its first discard; after the four discards of the default it is to act again with the Nw drawn, and seat 1
        /// draws a Sw next, which seat 0 may pung.
        static HandModel ViewerDeclaredModel(const char* discards = "Gd Rd Wd Gd") {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kWaitingHand0;
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kSeat2Waits;
            spec.hands[kSeat3] = kSeat3Waits;
            spec.live = "Gd Rd Wd Gd Nw Sw";
            HandModel model = MakeModel(spec);
            Lap(model, discards, {kSeat0});
            EXPECT_TRUE(model.GetHand(kSeat0).callingDeclared);
            return model;
        }

        /// The rectangles drawn in `color` in the last frame, in the order drawn.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        /// `color` is drawn exactly once in the last frame, at `rect`.
        void ExpectOneRectAt(const Color& color, const Rect& rect, const char* what) const {
            const std::vector<Rect> found = RectsOf(color);
            ASSERT_EQ(found.size(), 1u) << what;
            EXPECT_TRUE(SameRect(found[0], rect)) << what << ": at " << found[0].x << "," << found[0].y << " " << found[0].w << "x" << found[0].h << ", expected " << rect.x << "," << rect.y << " "
                                                  << rect.w << "x" << rect.h;
        }

        /// Runs the batch just drawn through its start, half way and landing, and calls `check(arrival fraction)` after each draw.
        template <class Check>
        void AtStartHalfAndLanding(Check check) {
            check(0.0f);
            AdvanceTime(kHalfFlight);
            Draw();
            check(0.5f);
            AdvanceTime(kHalfFlight);
            Draw();
            check(1.0f);
        }

        /// The fill that lies behind the meld `meld` of `seat` in the last layout.
        Rect HighlightOf(Seat seat, int meld) const {
            return Padded(BoundsOfMeld(_table->GetLayout(), seat, meld), kActingMargin);
        }

        /// Seat 1 (the person) pungs the 5p East has discarded; the batch has just been drawn, three tiles are in the air. `before` is the
        /// layout of the frame before.
        void PungByThePerson(TableLayout& before) {
            ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsPairOf5p)));
            AdvanceTime(kTileFlightSeconds);
            Draw();
            before = _table->GetLayout();
            EmitAction("MahjongClaimPung");
            ASSERT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);
            Draw();
            ASSERT_EQ(_table->FlightCount(), 3u);
        }
    };

    // ----- discards -----

    TEST_F(MahjongTableFlightTest, AComputersDiscardFliesFromTheMiddleOfItsHandAndTheNextSeatsDrawFromTheWallAsABack) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd Rd Wd 8s");
        HandModel model = MakeModel(spec);
        Lap(model, "9p");  // seat 1 has drawn a Gd and is to discard
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        const TableLayout before = _table->GetLayout();

        ASSERT_TRUE(Model().Discard(kSeat1, T("Gd")).has_value());
        ASSERT_EQ(Model().GetPhase(), Phase::Discard);
        Draw();

        // A tile that leaves a hand that shows nothing leaves from the middle of it (14 tiles: the eighth), and shows its face from the start
        const std::vector<Arrival> arrivals = {Pile(MiddleHand(before, kSeat1), kSeat1, 1, T("Gd")), DrawnBy(before, kSeat2)};
        ASSERT_EQ(Model().DrawnTile(kSeat2), T("Rd"));
        ExpectFlightsRun(arrivals);
    }

    TEST_F(MahjongTableFlightTest, ADrawIntoASideHandTurnsOnTheWayAndLandsAsItsSlot) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        const TableLayout before = _table->GetLayout();
        const int index = IndexOf(kSeat0, T("4s"));
        ASSERT_GE(index, 0);
        EXPECT_FALSE(ClickHandTile(index));
        Draw();

        // Seat 1 sits at the right (turned 270): its drawn tile leaves the unturned wall and turns the short way, 0 -> -90, by the eased progress
        const Arrival drawn = DrawnBy(before, kSeat1);
        ASSERT_FLOAT_EQ(drawn.from.rotation, 0.0f);
        ASSERT_EQ(_table->FlightCount(), 2u);
        AdvanceTime(kHalfFlight);
        Draw();
        ExpectTurnAt(drawn, 0.5f, 360.0f - 90.0f * Ease(kTileFlightEase, 0.5f));
        AdvanceTime(kHalfFlight);
        Draw();
        ASSERT_EQ(_table->FlightCount(), 0u);
        ExpectTurnAt(drawn, 1.0f, kRightSeatRotation);
    }

    TEST_F(MahjongTableFlightTest, ATileThatLeavesASideHandForItsPileKeepsItsTurnAndAnUnturnedFlightStaysUnturned) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd Rd Wd 8s");
        HandModel model = MakeModel(spec);
        Lap(model, "9p");  // seat 1 has drawn a Gd and is to discard
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        const TableLayout before = _table->GetLayout();
        ASSERT_TRUE(Model().Discard(kSeat1, T("Gd")).has_value());
        Draw();

        // Seat 1's hand and pile are both turned 270; seat 2's draw (wall to the top hand) is not turned at all
        const Arrival discard = Pile(MiddleHand(before, kSeat1), kSeat1, 1, T("Gd"));
        const Arrival drawn = DrawnBy(before, kSeat2);
        ASSERT_FLOAT_EQ(discard.from.rotation, kRightSeatRotation);
        for (const float fraction : {0.0f, 0.5f}) {
            if (fraction > 0.0f) {
                AdvanceTime(kHalfFlight);
                Draw();
            }
            ExpectTurnAt(discard, fraction, kRightSeatRotation);
            ExpectTurnAt(drawn, fraction, 0.0f);
        }
        AdvanceTime(kHalfFlight);
        Draw();
        ExpectTurnAt(discard, 1.0f, kRightSeatRotation);
        ExpectTurnAt(drawn, 1.0f, 0.0f);
    }

    TEST_F(MahjongTableFlightTest, AClaimedTileTurnsFromTheSideSeatsPileIntoTheUnturnedSetAndTheTilesFromTheHandStayUnturned) {
        // Seat 0 (the person, at the bottom) holds a pair of Gd; seat 3 (at the left, turned 90) discards the third
        FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Gd", "9m 9p 9s Rd");
        HandModel model = MakeModel(spec);
        Lap(model, "9m 9p 9s");
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat0, model));
        ASSERT_TRUE(Model().Discard(kSeat3, T("Gd")).has_value());
        ASSERT_EQ(Model().GetPhase(), Phase::Claim);
        Draw();
        AdvanceTime(kTileFlightSeconds);
        Draw();
        const TableLayout before = _table->GetLayout();
        const int gd = IndexOf(kSeat0, T("Gd"));
        ASSERT_GE(gd, 0);
        EmitAction("MahjongClaimPung");
        ASSERT_EQ(Hand().GetHand(kSeat0).melds.size(), 1u);
        Draw();

        const std::vector<Arrival> arrivals =
            ClaimArrivals(kSeat0, RectOf(before, SlotRole::DiscardTile, kSeat3, 3), {RectOf(before, SlotRole::HandTile, kSeat0, gd), RectOf(before, SlotRole::HandTile, kSeat0, gd + 1)}, T("Gd"));
        ASSERT_EQ(arrivals.size(), 3u);
        for (const float fraction : {0.0f, 0.5f, 1.0f}) {
            if (fraction > 0.0f) {
                AdvanceTime(kHalfFlight);
                Draw();
            }
            for (const Arrival& arrival : arrivals) {
                // The claimed tile starts as it lay in the pile (90) and ends unturned, the others are not turned at any time
                const bool claimed = arrival.from.rotation != 0.0f;
                const float eased = fraction >= 1.0f ? 1.0f : Ease(kTileFlightEase, fraction);
                ExpectTurnAt(arrival, fraction, claimed && fraction < 1.0f ? kLeftSeatRotation * (1.0f - eased) : 0.0f);
            }
        }
    }

    TEST_F(MahjongTableFlightTest, ThePersonsClickOnATileMakesThatTileFlyOutOfItsOwnSlot) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        const TableLayout before = _table->GetLayout();
        const int index = IndexOf(kSeat0, T("4s"));
        ASSERT_GE(index, 0);

        EXPECT_FALSE(ClickHandTile(index));
        ASSERT_EQ(Hand().Discards().size(), 1u);
        Draw();

        const std::vector<Arrival> arrivals = {Pile(RectOf(before, SlotRole::HandTile, kSeat0, index), kSeat0, 0, T("4s")), DrawnBy(before, kSeat1)};
        ExpectFlightsRun(arrivals);
    }

    TEST_F(MahjongTableFlightTest, ATileHeldTwiceFliesFromOneOfItsTwoSlots) {
        FixedWallSpec spec = SpecOf(kPairHand, "9p Gd");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        const TableLayout before = _table->GetLayout();
        const int first = IndexOf(kSeat0, T("4p"));
        ASSERT_GE(first, 0);
        ASSERT_EQ(Hand().GetHand(kSeat0).concealed[static_cast<size_t>(first) + 1], T("4p"));

        EXPECT_FALSE(ClickHandTile(first + 1));
        Draw();

        ASSERT_EQ(Hand().Discards().size(), 1u);
        ASSERT_EQ(Hand().Discards()[0].tile, T("4p"));
        const std::vector<Arrival> either = {Pile(RectOf(before, SlotRole::HandTile, kSeat0, first), kSeat0, 0, T("4p"))};
        const std::vector<Arrival> other = {Pile(RectOf(before, SlotRole::HandTile, kSeat0, first + 1), kSeat0, 0, T("4p"))};
        ASSERT_EQ(_table->FlightCount(), 2u);
        const std::optional<Tile> face = T("4p");
        const bool fromFirst = TileAmongLast(2, Where(either[0], 0.0f), face);
        const bool fromSecond = TileAmongLast(2, Where(other[0], 0.0f), face);
        EXPECT_TRUE(fromFirst || fromSecond) << "the pair's tile left from one of its two places";
        ExpectEverySlotDrawnOnce();
    }

    // ----- draws -----

    TEST_F(MahjongTableFlightTest, TheViewersDrawFliesFromTheLastStackOfTheWallToItsPlaceInTheHandAsAFace) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd Rd Wd 8s");
        HandModel model = MakeModel(spec);
        Lap(model, "9p Gd Rd");  // seat 3 has drawn a Wd and is to discard
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        const TableLayout before = _table->GetLayout();

        ASSERT_TRUE(Model().Discard(kSeat3, T("Wd")).has_value());
        ASSERT_EQ(Model().DrawnTile(kSeat0), T("8s"));
        Draw();

        const Arrival drawn = DrawnBy(before, kSeat0);
        EXPECT_EQ(drawn.index, IndexOf(kSeat0, T("8s")));
        ASSERT_TRUE(drawn.face.has_value());
        ExpectFlightsRun({Pile(MiddleHand(before, kSeat3), kSeat3, 3, T("Wd")), drawn});
    }

    TEST_F(MahjongTableFlightTest, APassThatMakesTheNextSeatDrawIsSeenByTheWallAlone) {
        // East discards a 5p, the person at seat 1 passes and draws: the log gets no entry for the pass or the draw
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsPairOf5p)));
        const TableLayout before = _table->GetLayout();
        ASSERT_EQ(Model().GetPhase(), Phase::Claim);
        // The discard itself has just flown in: let that flight end
        AdvanceTime(kTileFlightSeconds);
        const size_t moves = Model().Moves().size();

        EmitAction("MahjongPass");
        ASSERT_EQ(Model().GetPhase(), Phase::Discard);
        ASSERT_EQ(Model().CurrentSeat(), kSeat1);
        ASSERT_EQ(Model().Moves().size(), moves) << "a pass and a draw are no move";
        Draw();

        ExpectFlightsRun({DrawnBy(before, kSeat1)});
    }

    // ----- claims -----

    TEST_F(MahjongTableFlightTest, APungClaimedByThePersonTakesTheDiscardFromThePileAndTheTwoOthersFromTheHand) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsPairOf5p)));
        AdvanceTime(kTileFlightSeconds);
        Draw();
        const TableLayout before = _table->GetLayout();
        const int five = IndexOf(kSeat1, T("5p"));
        ASSERT_GE(five, 0);

        EmitAction("MahjongClaimPung");
        ASSERT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);
        Draw();

        const std::vector<Arrival> arrivals =
            ClaimArrivals(kSeat1, RectOf(before, SlotRole::DiscardTile, kSeat0, 0), {RectOf(before, SlotRole::HandTile, kSeat1, five), RectOf(before, SlotRole::HandTile, kSeat1, five + 1)}, T("5p"));
        ExpectFlightsRun(arrivals);
    }

    TEST_F(MahjongTableFlightTest, AChowClaimedByThePersonTakesTheDiscardFromThePileAndItsNeighboursFromTheHand) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsAChowGap)));
        AdvanceTime(kTileFlightSeconds);
        Draw();
        const TableLayout before = _table->GetLayout();

        EmitAction("MahjongClaimChow", "0");
        ASSERT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);
        Draw();

        // The chow 4p 5p 6p: the 4p (index 0) and the 6p (index 1) of the hand, in the order of the parts
        ExpectFlightsRun(
            ClaimArrivals(kSeat1, RectOf(before, SlotRole::DiscardTile, kSeat0, 0), {RectOf(before, SlotRole::HandTile, kSeat1, 0), RectOf(before, SlotRole::HandTile, kSeat1, 1)}, T("5p")));
    }

    TEST_F(MahjongTableFlightTest, AKongClaimedByThePersonFliesFourTilesAndThenTheLooseTileIsDrawn) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsThree5p)));
        AdvanceTime(kTileFlightSeconds);
        Draw();
        const TableLayout before = _table->GetLayout();

        EmitAction("MahjongClaimKong");
        ASSERT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);
        ASSERT_EQ(Hand().GetHand(kSeat1).melds[0].kind, MeldKind::Kong);
        Draw();

        std::vector<Arrival> arrivals =
            ClaimArrivals(kSeat1, RectOf(before, SlotRole::DiscardTile, kSeat0, 0),
                          {RectOf(before, SlotRole::HandTile, kSeat1, 0), RectOf(before, SlotRole::HandTile, kSeat1, 1), RectOf(before, SlotRole::HandTile, kSeat1, 2)}, T("5p"));
        // The loose tile drawn after the kong is the wall's, like any draw
        ASSERT_TRUE(Hand().DrawnTile(kSeat1).IsValid());
        arrivals.push_back(DrawnBy(before, kSeat1));
        ExpectFlightsRun(arrivals);
    }

    TEST_F(MahjongTableFlightTest, AComputersClaimAndTheDiscardAfterItFlyTogetherAndTheClaimedTileComesFromTheDiscardersHand) {
        // The person at seat 0 discards a red dragon, seat 1 (a pair of them) pungs it and discards, all before the next draw of the table
        FixedWallSpec spec = SpecOf(kPlainHand, "Rd");
        spec.hands[kSeat1] = kSeat1HoldsRedPair;
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        const TableLayout before = _table->GetLayout();
        const int red = IndexOf(kSeat0, T("Rd"));
        ASSERT_GE(red, 0);
        EXPECT_FALSE(ClickHandTile(red));
        AdvanceTime(kComputerStepSeconds);  // the pause of the computers: the claim, and the claimer's discard
        ASSERT_EQ(Hand().Moves().size(), 3u);
        ASSERT_EQ(Hand().Moves()[1].kind, MoveKind::ClaimPung);
        ASSERT_EQ(Hand().Moves()[2].kind, MoveKind::Discard);
        Draw();

        // The discard never was in a frame: the claimed tile leaves the hand of the discarder (the viewer's own slot of that tile)
        const FlightOrigin seat1Middle = MiddleHand(before, kSeat1);
        std::vector<Arrival> arrivals = ClaimArrivals(kSeat1, RectOf(before, SlotRole::HandTile, kSeat0, red), {seat1Middle, seat1Middle}, T("Rd"));
        ASSERT_EQ(Hand().Discards().size(), 1u) << "the claimed tile left the pile";
        arrivals.push_back(Pile(seat1Middle, kSeat1, 0, Hand().Discards()[0].tile));
        if (Model().DrawnTile(kSeat2).IsValid()) {
            arrivals.push_back(DrawnBy(before, kSeat2));
        }
        ExpectFlightsRun(arrivals);
    }

    // ----- kongs -----

    TEST_F(MahjongTableFlightTest, AConcealedKongOfTheViewerFliesFourFacesAndThenTheLooseTileIsDrawn) {
        FixedWallSpec own = SpecOf(kThree1pHand, "1p");
        own.loose = "9m";
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(own)));
        const TableLayout before = _table->GetLayout();

        ASSERT_TRUE(Model().DeclareConcealedKong(kSeat0, T("1p")).has_value());
        Draw();

        std::vector<Arrival> arrivals;
        for (const TileSlot* slot : MeldSlots(kSeat0, 0)) {
            Arrival arrival;
            arrival.from = RectOf(before, SlotRole::HandTile, kSeat0, slot->part);  // the four 1p are the first four tiles of the sorted hand
            arrival.role = SlotRole::MeldTile;
            arrival.seat = kSeat0;
            arrival.index = 0;
            arrival.part = slot->part;
            arrival.face = T("1p");
            arrivals.push_back(arrival);
        }
        ASSERT_EQ(arrivals.size(), 4u);
        arrivals.push_back(DrawnBy(before, kSeat0));
        ExpectFlightsRun(arrivals);
    }

    TEST_F(MahjongTableFlightTest, AnOpponentsConcealedKongFliesAsFourBacksAndShowsNoFace) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p 5p");
        spec.hands[kSeat1] = kSeat1HoldsThree5p;
        spec.loose = "9m";
        HandModel model = MakeModel(spec);
        Lap(model, "9p");  // seat 1 has drawn the fourth 5p
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        const TableLayout before = _table->GetLayout();

        ASSERT_TRUE(Model().DeclareConcealedKong(kSeat1, T("5p")).has_value());
        Draw();

        const FlightOrigin middle = MiddleHand(before, kSeat1);
        std::vector<Arrival> arrivals;
        for (const TileSlot* slot : MeldSlots(kSeat1, 0)) {
            Arrival arrival;
            arrival.from = middle;
            arrival.role = SlotRole::MeldTile;
            arrival.seat = kSeat1;
            arrival.index = 0;
            arrival.part = slot->part;
            arrivals.push_back(arrival);  // no face: a back
        }
        ASSERT_EQ(arrivals.size(), 4u);
        arrivals.push_back(DrawnBy(before, kSeat1));
        // All five are backs while they fly: no tile in the air shows a face
        ExpectEverySlotDrawnOnce();
        const std::vector<DrawCall> calls = TileCalls();
        for (size_t i = calls.size() - arrivals.size(); i < calls.size(); ++i) {
            EXPECT_TRUE(SameSource(calls[i], BackRect())) << "a tile in the air at call " << i << " shows a face";
        }
        ExpectFlightsRun(arrivals);
    }

    TEST_F(MahjongTableFlightTest, AnUpgradedKongFliesItsOneNewTileAndThenTheLooseTileIsDrawn) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, UpgradeModel()));
        const TableLayout before = _table->GetLayout();
        ASSERT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);

        ASSERT_TRUE(Model().UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(Hand().GetHand(kSeat1).melds[0].kind, MeldKind::Kong);
        Draw();

        Arrival fourth;
        fourth.from = MiddleHand(before, kSeat1);
        fourth.role = SlotRole::MeldTile;
        fourth.seat = kSeat1;
        fourth.index = 0;
        fourth.part = 3;
        fourth.face = T("5p");
        ExpectFlightsRun({fourth, DrawnBy(before, kSeat1)});
    }

    // ----- no peeking -----

    TEST_F(MahjongTableFlightTest, ForASpectatorNoTileInTheAirShowsAFaceBeforeItIsPublic) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        // Three steps: the clock of the pace has run 0.3 s after each, and a fourth would make the component step on its own
        for (int step = 0; step < 3; ++step) {
            ASSERT_TRUE(Session().StepComputer().has_value());
            Draw();
            EXPECT_GT(_table->FlightCount(), 0u) << "step " << step;
            ExpectNoConcealedFace();
            ExpectEverySlotDrawnOnce();
            AdvanceTime(kHalfFlight);
            Draw();
            ExpectNoConcealedFace();
            AdvanceTime(kHalfFlight);
            Draw();
            ASSERT_EQ(_table->FlightCount(), 0u) << "step " << step << " moves " << Hand().Moves().size();
            ExpectNoConcealedFace();
        }
    }

    TEST_F(MahjongTableFlightTest, UnderTheHotSeatCoverNothingFliesAndNoConcealedTileIsFaceUpThenRevealOpensTheViewersFlights) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd Rd");
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Lay(MakeModel(spec));
        Draw();
        ASSERT_TRUE(Session().HandoffPending()) << "the cover for seat 0 is up";
        ExpectNoConcealedFace();

        // Seat 0 shows itself, discards; seat 1 (a person) is next, so the cover goes up for it and the viewer changes: nothing flies across
        Session().Reveal();
        Draw();
        ASSERT_TRUE(Session().PerformPerson(HandAction::MakeDiscard(T("4s"), false)).has_value());
        ASSERT_TRUE(Session().HandoffPending());
        Draw();
        EXPECT_EQ(_table->FlightCount(), 0u) << "the viewer changed: no flight crosses it";
        ExpectNoConcealedFace();
        ExpectEverySlotDrawnOnce();

        // After Reveal seat 1 is the viewer: its discard flies out of its own slot and the draw of the next (a computer) as a back
        Session().Reveal();
        Draw();
        const TableLayout seat1Before = _table->GetLayout();
        ASSERT_EQ(seat1Before.viewer, kSeat1);
        const int index = IndexOf(kSeat1, T("2m"));
        ASSERT_GE(index, 0);
        ASSERT_TRUE(Session().PerformPerson(HandAction::MakeDiscard(T("2m"), false)).has_value());
        Draw();
        ASSERT_EQ(_table->GetLayout().viewer, kSeat1);
        ExpectFlightsRun({Pile(RectOf(seat1Before, SlotRole::HandTile, kSeat1, index), kSeat1, 1, T("2m")), DrawnBy(seat1Before, kSeat2)});
    }

    // ----- nothing lost, nothing drawn twice, nothing moved in the model -----

    TEST_F(MahjongTableFlightTest, ASecondBatchWhileOneFliesEndsTheFirstAtItsTargetAndStartsItsOwnAtZero) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd Rd Wd 8s");
        HandModel model = MakeModel(spec);
        Lap(model, "9p");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        ASSERT_TRUE(Model().Discard(kSeat1, T("Gd")).has_value());
        Draw();
        ASSERT_EQ(_table->FlightCount(), 2u);
        AdvanceTime(kHalfFlight);
        Draw();
        const TableLayout second = _table->GetLayout();

        ASSERT_TRUE(Model().Discard(kSeat2, T("Rd")).has_value());
        Draw();

        // The first batch ended where it was going: seat 1's discard lies at its slot, and only the new batch flies, from zero
        ASSERT_EQ(_table->FlightCount(), 2u);
        ExpectEverySlotDrawnOnce();
        const Rect landed = RectOf(_table->GetLayout(), SlotRole::DiscardTile, kSeat1, 1);
        const std::vector<DrawCall> calls = TileCalls();
        bool seen = false;
        for (size_t i = 0; i + 2 < calls.size(); ++i) {
            seen = seen || (SameRect(calls[i].rect, landed) && SameSource(calls[i], SourceRect(T("Gd"))));
        }
        EXPECT_TRUE(seen) << "the first discard is at its place, not in the air";
        ExpectFlightsRun({Pile(MiddleHand(second, kSeat2), kSeat2, 2, T("Rd")), DrawnBy(second, kSeat3)});
    }

    TEST_F(MahjongTableFlightTest, ABatchOfManyMovesFromTheControlApiFliesTheFinalSlotsAndBreaksNothing) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p");
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat0, MakeModel(spec)));
        EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("9p"))));
        Draw();
        AdvanceTime(kTileFlightSeconds);
        Draw();
        ASSERT_EQ(_table->FlightCount(), 0u);
        const size_t moves = Hand().Moves().size();

        // The control API does not wait: the three computers play until the person acts again, all in one batch
        ASSERT_GT(Session().RunComputers(), 2);
        ASSERT_GE(Hand().Moves().size(), moves + 3);
        Draw();

        EXPECT_GE(_table->FlightCount(), 3u);
        ExpectEverySlotDrawnOnce();
        AdvanceTime(kTileFlightSeconds);
        Draw();
        EXPECT_EQ(_table->FlightCount(), 0u);
        ExpectEverySlotDrawnOnce();
        ExpectTableDrawn(false, Session().Viewer());
    }

    TEST_F(MahjongTableFlightTest, FlightsOffPlanNothingAndTilesStayAtTheirSlots) {
        _table->SetTileFlightSeconds(0.0f);
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("4s"))));
        Draw();
        EXPECT_EQ(_table->FlightCount(), 0u);
        ExpectTableDrawn(false, kSeat0);
    }

    TEST_F(MahjongTableFlightTest, TheModelAndTheSessionAreTheSameWithFlightsOnAndOffOverAWholeHandOfComputers) {
        const auto play = [&](bool flights, std::vector<std::string>& log) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers, kSeed));
            _table->SetTileFlightSeconds(flights ? kTileFlightSeconds : 0.0f);
            Draw();
            const auto firstVersion = Session().Version();  // the counter runs on across matches: what is compared is the distance from the start
            for (int period = 0; period < kActionLimit && Session().GetPhase() == SessionPhase::Playing; ++period) {
                for (int quarter = 0; quarter < 4; ++quarter) {  // the fourth ends the period of the pace: the computer plays
                    AdvanceTime(kQuarter);
                    Draw();
                    ExpectEverySlotDrawnOnce();
                }
                log.push_back(std::to_string(Session().Version() - firstVersion) + " " + Snapshot(Hand()));
            }
            ASSERT_NE(Session().GetPhase(), SessionPhase::Playing) << "the hand ended";
        };
        std::vector<std::string> withFlights;
        std::vector<std::string> without;
        ASSERT_NO_FATAL_FAILURE(play(true, withFlights));
        ASSERT_NO_FATAL_FAILURE(play(false, without));
        EXPECT_EQ(withFlights, without);
        EXPECT_GT(withFlights.size(), 20u);
    }

    // ----- the marks follow the flying tile -----

    TEST_F(MahjongTableFlightTest, TheTintOfTheLastDiscardIsOnTheFlyingTileAndThenOnItsSlot) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        const TableLayout before = _table->GetLayout();
        const int index = IndexOf(kSeat0, T("4s"));
        ASSERT_GE(index, 0);
        EXPECT_FALSE(ClickHandTile(index));
        Draw();

        const Arrival discard = Pile(RectOf(before, SlotRole::HandTile, kSeat0, index), kSeat0, 0, T("4s"));
        AtStartHalfAndLanding([&](float fraction) {
            ExpectOneRectAt(kLastDiscardMark, Where(discard, fraction), "the tint of the last discard");
            ExpectEverySlotDrawnOnce();
        });
        EXPECT_EQ(_table->FlightCount(), 0u);
    }

    TEST_F(MahjongTableFlightTest, TheTintOfTheDrawnTileIsOnTheFlyingTileAndThenOnItsPlaceInTheRow) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd Rd Wd 8s");
        HandModel model = MakeModel(spec);
        Lap(model, "9p Gd Rd");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));
        const TableLayout before = _table->GetLayout();
        ASSERT_TRUE(Model().Discard(kSeat3, T("Wd")).has_value());
        ASSERT_EQ(Model().DrawnTile(kSeat0), T("8s"));
        Draw();

        const Arrival drawn = DrawnBy(before, kSeat0);
        AtStartHalfAndLanding([&](float fraction) {
            ExpectOneRectAt(kDrawnTileMark, Where(drawn, fraction), "the tint of the drawn tile");
            ExpectOneRectAt(kLastDiscardMark, Where(Pile(MiddleHand(before, kSeat3), kSeat3, 3, T("Wd")), fraction), "the tint of the discard that came with it");
        });
    }

    TEST_F(MahjongTableFlightTest, TheBarOfTheClaimedEdgeTravelsWithTheClaimedTile) {
        TableLayout before;
        ASSERT_NO_FATAL_FAILURE(PungByThePerson(before));
        const std::vector<Arrival> arrivals = ClaimArrivals(kSeat1, RectOf(before, SlotRole::DiscardTile, kSeat0, 0),
                                                            {RectOf(before, SlotRole::HandTile, kSeat1, 1), RectOf(before, SlotRole::HandTile, kSeat1, 2)}, T("5p"));  // 4p 5p 5p 6p ...
        Edge edge = Edge::None;
        Arrival flying;
        for (const Arrival& arrival : arrivals) {
            const TileSlot* slot = Find(_table->GetLayout(), arrival.role, arrival.seat, arrival.index, arrival.part);
            if (slot != nullptr && slot->claimEdge != Edge::None) {
                edge = slot->claimEdge;
                flying = arrival;
            }
        }
        ASSERT_NE(edge, Edge::None);
        ASSERT_TRUE(SameRect(flying.from, RectOf(before, SlotRole::DiscardTile, kSeat0, 0))) << "the claimed tile leaves the pile";
        AtStartHalfAndLanding([&](float fraction) { ExpectOneRectAt(kClaimedEdge, BarOf(Where(flying, fraction), edge), "the bar of the claimed edge"); });
    }

    TEST_F(MahjongTableFlightTest, TheFrameOfThePendingDiscardFollowsTheDiscardInTheAir) {
        FixedWallSpec spec = SpecOf(kPlainHand, "5p");
        spec.hands[kSeat1] = kSeat1HoldsPairOf5p;
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, MakeModel(spec)));
        const TableLayout before = _table->GetLayout();
        ASSERT_TRUE(Model().Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(Model().GetPhase(), Phase::Claim);
        Draw();

        // The person's claim buttons are live while the discard flies, and the frame says so
        const Arrival pending = Pile(MiddleHand(before, kSeat0), kSeat0, 0, T("5p"));
        AtStartHalfAndLanding([&](float fraction) {
            ExpectOneRectAt(kPendingFrame, Padded(Where(pending, fraction), kPendingMargin), "the frame of the pending discard");
            ExpectOneRectAt(kLastDiscardMark, Where(pending, fraction), "the tint of the pending discard");
        });
        EmitAction("MahjongClaimPung");  // the buttons work during a flight too: nothing refuses this one
        EXPECT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);
    }

    TEST_F(MahjongTableFlightTest, TheBarOfASafeDiscardIsOnTheFlyingDrawnTileOfADeclaredHand) {
        // Seat 0 has declared; seat 3 is to discard, and the draw that follows is seat 0's: the Nw, which is no part of its shape
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, ViewerDeclaredModel("Gd Rd Wd")));
        const TableLayout before = _table->GetLayout();
        ASSERT_TRUE(Model().Discard(kSeat3, T("Gd")).has_value());
        ASSERT_EQ(Model().DrawnTile(kSeat0), T("Nw"));
        Draw();

        const Arrival drawn = DrawnBy(before, kSeat0);
        ASSERT_TRUE(drawn.face.has_value());
        AtStartHalfAndLanding([&](float fraction) {
            const Rect tile = Where(drawn, fraction);
            const Rect bar(tile.x, tile.y + tile.h - kKeepsWaitingBarHeight, tile.w, kKeepsWaitingBarHeight);
            const std::vector<Rect> bars = RectsOf(kKeepsWaitingMark);
            const bool onTheFlyingTile = std::any_of(bars.begin(), bars.end(), [&](const Rect& found) { return SameRect(found, bar); });
            EXPECT_TRUE(onTheFlyingTile) << "fraction " << fraction << ": the bar of the drawn Nw is inside the bottom edge of the tile in the air";
            ExpectOneRectAt(kDrawnTileMark, tile, "the tint of the drawn tile");
            ExpectEverySlotDrawnOnce();
        });
    }

    TEST_F(MahjongTableFlightTest, TheTurnBackdropTheCallingFrameAndTheSeatPlatesAreTheSameWithAFlightAsWithout) {
        const std::vector<Color> colors = {kTurnMark, kSeatPlate, kSeatTurnFrame, kDealerMark, kCallingFrame};
        const auto play = [&](bool flights, std::vector<std::vector<Rect>>& seen) {
            _table->SetTileFlightSeconds(flights ? kTileFlightSeconds : 0.0f);
            ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, ViewerDeclaredModel()));
            const int drawn = IndexOf(kSeat0, T("Nw"));
            ASSERT_GE(drawn, 0);
            EXPECT_FALSE(ClickHandTile(drawn));
            Draw();
            EXPECT_EQ(_table->FlightCount() > 0u, flights);
            AdvanceTime(kHalfFlight);
            Draw();
            for (const Color& color : colors) {
                seen.push_back(RectsOf(color));
            }
        };
        std::vector<std::vector<Rect>> with;
        std::vector<std::vector<Rect>> without;
        ASSERT_NO_FATAL_FAILURE(play(true, with));
        ASSERT_NO_FATAL_FAILURE(play(false, without));
        ASSERT_EQ(with.size(), without.size());
        for (size_t i = 0; i < with.size(); ++i) {
            ASSERT_EQ(with[i].size(), without[i].size()) << "colour " << i;
            for (size_t k = 0; k < with[i].size(); ++k) {
                EXPECT_TRUE(SameRect(with[i][k], without[i][k])) << "colour " << i << " rect " << k;
            }
        }
        EXPECT_FALSE(with[4].empty()) << "the frame of the declared hand is drawn";
        EXPECT_FALSE(with[0].empty()) << "and so is the turn backdrop";
    }

    // ----- the blink of a set waits for its tiles -----

    TEST_F(MahjongTableFlightTest, TheSetBlinksOnlyAfterItsTilesHaveLandedAndThenForItsWholeTime) {
        TableLayout before;
        ASSERT_NO_FATAL_FAILURE(PungByThePerson(before));
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "the tiles are in the air";
        AdvanceTime(kHalfFlight);
        Draw();
        ASSERT_EQ(_table->FlightCount(), 3u);
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "half way";
        AdvanceTime(kHalfFlight);
        Draw();

        ASSERT_EQ(_table->FlightCount(), 0u);
        ExpectOneRectAt(kClaimHighlight, HighlightOf(kSeat1, 0), "on at the moment of landing");
        Draw();
        EXPECT_EQ(RectsOf(kClaimHighlight).size(), 1u) << "drawing again changes nothing";
        AdvanceTime(kQuarter);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "the second half of the period";
        AdvanceTime(kQuarter);
        Draw();
        EXPECT_EQ(RectsOf(kClaimHighlight).size(), 1u) << "the next period";
        AdvanceTime(kClaimHighlightSeconds);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "gone after kClaimHighlightSeconds more";
        AdvanceTime(kQuarter);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
    }

    TEST_F(MahjongTableFlightTest, WithoutAFlightTheSetBlinksAtOnce) {
        _table->SetTileFlightSeconds(0.0f);
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, EastDiscardsFive(kSeat1HoldsPairOf5p)));
        EmitAction("MahjongClaimPung");
        Draw();
        ASSERT_EQ(_table->FlightCount(), 0u);
        ExpectOneRectAt(kClaimHighlight, HighlightOf(kSeat1, 0), "on at the moment of the move");
        AdvanceTime(kQuarter);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
    }

    TEST_F(MahjongTableFlightTest, FlightsSwitchedOffInTheAirEndAtOnceAndTheSetBlinks) {
        TableLayout before;
        ASSERT_NO_FATAL_FAILURE(PungByThePerson(before));
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
        _table->SetTileFlightSeconds(0.0f);
        AdvanceTime(0.01f);
        Draw();
        EXPECT_EQ(_table->FlightCount(), 0u);
        ExpectOneRectAt(kClaimHighlight, HighlightOf(kSeat1, 0), "nothing is left in the air to wait for");
    }

    TEST_F(MahjongTableFlightTest, TheDelayOfTheBlinkRunsOnlyWhileTheTableIsLive) {
        for (const bool rules : {false, true}) {
            TableLayout before;
            ASSERT_NO_FATAL_FAILURE(PungByThePerson(before));
            AdvanceTime(kHalfFlight);
            Draw();
            if (rules) {
                _game->OpenRules();
            } else {
                _game->OpenSetup();
            }
            AdvanceTime(5.0f);
            Draw();
            EXPECT_EQ(_table->FlightCount(), 3u) << (rules ? "rules" : "setup");
            EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
            if (rules) {
                _game->CloseRules();
            } else {
                _game->CloseSetup();
            }
            AdvanceTime(kHalfFlight);
            Draw();
            ASSERT_EQ(_table->FlightCount(), 0u) << (rules ? "rules" : "setup");
            ExpectOneRectAt(kClaimHighlight, HighlightOf(kSeat1, 0), "on when the second half of the flight has run");
        }
    }

    TEST_F(MahjongTableFlightTest, ASecondSetLaidDuringTheFlightRestartsTheDelayOnItsOwnTiles) {
        FixedWallSpec spec = SpecOf(kPlainHand, "9p 5p");
        spec.hands[kSeat1] = "5p 5p 5p 5s 5s 5s 2p 8p 2s 8s 2m 5m 8m";
        spec.loose = "5s 9m";
        HandModel model = MakeModel(spec);
        Lap(model, "9p");  // seat 1 has drawn the fourth 5p
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        ASSERT_TRUE(Model().DeclareConcealedKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(Model().DrawnTile(kSeat1), T("5s"));
        Draw();
        ASSERT_GT(_table->FlightCount(), 0u);
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());
        AdvanceTime(kHalfFlight);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty());

        ASSERT_TRUE(Model().DeclareConcealedKong(kSeat1, T("5s")).has_value());
        ASSERT_EQ(Model().GetHand(kSeat1).melds.size(), 2u);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "the new set's tiles are in the air";
        AdvanceTime(kHalfFlight);
        Draw();
        EXPECT_TRUE(RectsOf(kClaimHighlight).empty()) << "the first batch would have landed by now; the second has not";
        ASSERT_GT(_table->FlightCount(), 0u);
        AdvanceTime(kHalfFlight);
        Draw();
        ASSERT_EQ(_table->FlightCount(), 0u);
        ExpectOneRectAt(kClaimHighlight, HighlightOf(kSeat1, 1), "the blink is on the second set");
    }

    // ----- a face-up flight after the cover, and a new match -----

    TEST_F(MahjongTableFlightTest, AfterRevealTheViewersConcealedKongFliesAsFourFaces) {
        FixedWallSpec own = SpecOf(kThree1pHand, "1p");
        own.loose = "9m";
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Lay(MakeModel(own));
        Draw();
        ASSERT_TRUE(Session().HandoffPending());
        Session().Reveal();
        Draw();
        ASSERT_EQ(Session().Viewer(), kSeat0);
        const TableLayout before = _table->GetLayout();

        ASSERT_TRUE(Session().PerformPerson(HandAction::MakeConcealedKong(T("1p"))).has_value());
        ASSERT_FALSE(Session().HandoffPending());
        Draw();

        std::vector<Arrival> arrivals;
        for (const TileSlot* slot : MeldSlots(kSeat0, 0)) {
            Arrival arrival;
            arrival.from = RectOf(before, SlotRole::HandTile, kSeat0, slot->part);
            arrival.role = SlotRole::MeldTile;
            arrival.seat = kSeat0;
            arrival.index = 0;
            arrival.part = slot->part;
            arrival.face = T("1p");
            arrivals.push_back(arrival);
        }
        ASSERT_EQ(arrivals.size(), 4u);
        arrivals.push_back(DrawnBy(before, kSeat0));
        ExpectFlightsRun(arrivals);
    }

    TEST_F(MahjongTableFlightTest, AMatchStartedWhileTheOldOneHadNoHistoryFliesNothingEvenWhenItHasMadeMoreMovesThanWereSeen) {
        // The old match is in its first hand with no move: the table has seen none. A control `reset` starts a new one and the computers
        // play it on in the same call, so by the next draw it has more moves than were ever seen; that is not news.
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers, 11));
        Draw();
        ASSERT_TRUE(Hand().Moves().empty());
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers, 12));
        ASSERT_GT(Session().RunComputers(), 5);
        ASSERT_GT(Hand().Moves().size(), 5u);
        Draw();
        EXPECT_EQ(_table->FlightCount(), 0u) << "a new match starts without flights";
        ExpectEverySlotDrawnOnce();
        // and the batches of the new match fly as usual
        ASSERT_NO_FATAL_FAILURE(Start(kPersonAtSeat0, 13));
        Draw();
        ASSERT_TRUE(Session().PerformPerson(HandAction::MakeDiscard(Hand().GetHand(kSeat0).concealed.front(), false)).has_value());
        Draw();
        EXPECT_GT(_table->FlightCount(), 0u);
    }

    // ----- the clock -----

    TEST_F(MahjongTableFlightTest, TheClockOfTheFlightsStandsStillWhileTheSeatChoiceOrTheRulesAreOpenAndGoesOnAfter) {
        for (const bool rules : {false, true}) {
            FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd");
            ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
            EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("4s"))));
            Draw();
            const size_t flying = _table->FlightCount();
            ASSERT_GT(flying, 0u);
            AdvanceTime(kQuarter * 0.4f);
            Draw();
            const std::vector<DrawCall> calls = TileCalls();

            if (rules) {
                _game->OpenRules();
            } else {
                _game->OpenSetup();
            }
            AdvanceTime(5.0f);
            Draw();
            EXPECT_EQ(_table->FlightCount(), flying) << (rules ? "rules" : "setup");
            const std::vector<DrawCall> still = TileCalls();
            ASSERT_EQ(still.size(), calls.size());
            for (size_t i = 0; i < calls.size(); ++i) {
                EXPECT_TRUE(SameRect(calls[i].rect, still[i].rect)) << "call " << i << " moved while the table was paused";
            }

            if (rules) {
                _game->CloseRules();
            } else {
                _game->CloseSetup();
            }
            AdvanceTime(kTileFlightSeconds);
            Draw();
            EXPECT_EQ(_table->FlightCount(), 0u) << (rules ? "rules" : "setup");
        }
    }

    TEST_F(MahjongTableFlightTest, ANewHandAViewerChangeAndAResetLeaveNoFlight) {
        // A reset
        FixedWallSpec spec = SpecOf(kPlainHand, "9p Gd");
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        EXPECT_FALSE(ClickHandTile(IndexOf(kSeat0, T("4s"))));
        Draw();
        ASSERT_GT(_table->FlightCount(), 0u);
        _game->Reset();
        Draw();
        EXPECT_EQ(_table->FlightCount(), 0u);
        ExpectEverySlotDrawnOnce();

        // A new hand: the hand is played to its end, the next one is dealt
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        Session().RunComputers();
        Draw();
        ASSERT_EQ(Session().GetPhase(), SessionPhase::HandOver);
        ASSERT_TRUE(Session().NextHand().has_value());
        Draw();
        EXPECT_EQ(_table->FlightCount(), 0u) << "the deal of a new hand is not news";

        // A viewer change: the first person's move hands the screen to the second
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Lay(MakeModel(SpecOf(kPlainHand, "9p")));
        Draw();
        Session().Reveal();
        Draw();
        ASSERT_EQ(_table->GetLayout().viewer, kSeat0);
        const Tile tile = Hand().GetHand(kSeat0).concealed.front();
        ASSERT_TRUE(Session().PerformPerson(HandAction::MakeDiscard(tile, false)).has_value());
        Draw();
        EXPECT_EQ(_table->GetLayout().viewer, kSeat1);
        EXPECT_EQ(_table->FlightCount(), 0u);
    }

    TEST_F(MahjongTableFlightTest, ThreeStepsAtThePaceFlyAndLandBeforeTheNextStep) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        for (int step = 0; step < 3; ++step) {
            const size_t moves = Hand().Moves().size();
            AdvanceTime(kComputerStepSeconds);  // one step of the pace
            ASSERT_GT(Hand().Moves().size(), moves) << "step " << step;
            Draw();
            EXPECT_GT(_table->FlightCount(), 0u) << "step " << step;
            ExpectEverySlotDrawnOnce();
            AdvanceTime(kTileFlightSeconds);  // the clock of the pace has run only that far since the step
            Draw();
            EXPECT_EQ(_table->FlightCount(), 0u) << "step " << step << ": landed before the next step is due";
            ExpectEverySlotDrawnOnce();
        }
    }

}  // namespace BECore::Mahjong::Tests
