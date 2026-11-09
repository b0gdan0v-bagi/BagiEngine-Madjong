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
        /// A quarter of a second: exact in floats, so sums of it are exact.
        constexpr float kQuarter = 0.25f;
        constexpr float kHalfFlight = kTileFlightSeconds * 0.5f;

        /// Where the tile of a slot arrives, and what it shows there.
        struct Arrival {
            Rect from;
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

        static Rect RectOf(const TableLayout& layout, SlotRole role, Seat seat, int index, int part = 0) {
            const TileSlot* slot = Find(layout, role, seat, index, part);
            EXPECT_NE(slot, nullptr) << "role " << static_cast<int>(role) << " seat " << seat << " index " << index << " part " << part;
            return slot != nullptr ? slot->rect : Rect();
        }

        /// The middle hand slot of `seat`, where a tile of a hand that shows no identity leaves from.
        static Rect MiddleHand(const TableLayout& layout, Seat seat) {
            int count = 0;
            for (const TileSlot& slot : layout.slots) {
                count += slot.role == SlotRole::HandTile && slot.seat == seat ? 1 : 0;
            }
            return RectOf(layout, SlotRole::HandTile, seat, count / 2);
        }

        static Rect LastWallStack(const TableLayout& layout) {
            const TileSlot* last = nullptr;
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::WallStack && (last == nullptr || slot.index > last->index)) {
                    last = &slot;
                }
            }
            EXPECT_NE(last, nullptr) << "the wall has stacks";
            return last != nullptr ? last->rect : Rect();
        }

        /// The tile `tile` of the sorted hand of `seat` now.
        int IndexNow(Seat seat, Tile tile) const {
            return IndexOf(seat, tile);
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
            return FlightRect(TileFlight{SlotKey::Of(*slot), arrival.from}, slot->rect, Ease(kTileFlightEase, fraction));
        }

        /// Every slot of the layout is drawn once whatever flies: as many tile draws as slots.
        void ExpectEverySlotDrawnOnce() const {
            EXPECT_EQ(TileCalls().size(), _table->GetLayout().slots.size());
        }

        /// The state of the flights `fraction` of their time into the batch, as the last draw shows it.
        void ExpectAt(const std::vector<Arrival>& arrivals, float fraction) const {
            ExpectEverySlotDrawnOnce();
            for (const Arrival& arrival : arrivals) {
                const Rect expected = Where(arrival, fraction);
                EXPECT_TRUE(TileAmongLast(fraction >= 1.0f ? TileCalls().size() : arrivals.size(), expected, arrival.face))
                    << "fraction " << fraction << " role " << static_cast<int>(arrival.role) << " seat " << arrival.seat << " index " << arrival.index << " part " << arrival.part << " at "
                    << expected.x << "," << expected.y;
                if (fraction > 0.0f && fraction < 1.0f) {
                    const Rect to = Where(arrival, 1.0f);
                    EXPECT_FALSE(SameRect(expected, arrival.from)) << "moved";
                    EXPECT_FALSE(SameRect(expected, to)) << "not there yet";
                }
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
        std::vector<Arrival> ClaimArrivals(Seat seat, const Rect& claimedFrom, const std::vector<Rect>& fromHand, const Tile claimedTile) const {
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
        static Arrival Pile(const Rect& from, Seat seat, int entry, Tile tile) {
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
                arrival.index = IndexNow(seat, hand.DrawnTile(seat));
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
        const Rect seat1Middle = MiddleHand(before, kSeat1);
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

        const Rect middle = MiddleHand(before, kSeat1);
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
