#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Control/Json.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Table/TileFlights.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * A claim that loses to a better one in a claim window (`Components/MahjongTableComponent.h`, "A claim that lost"): for
 * `kClaimLostSeconds` the seat plate of the window's winner blinks and the plate of each seat whose claim lost has a frame, and a mahjong
 * on a discard flies from the pile into the winner's hand. The model is changed between two draws as a computer's step, a click or the
 * control API does; the windows are built on fixed walls (`FixedWall.h`): seat 3 draws and discards a 4p after a lap of three discards.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// What the seats hold when seat 3 discards the 4p (the lap before: seats 0, 1, 2 discard what they draw, "Wd Gd Nw").
        /// A person at seat 0 with a pair of 4p (a pung) or with 3p 5p (a chow).
        constexpr const char* kPungHand = "4p 4p 1s 4s 7s 1m 4m 7m 2s 8s Sw Ww Gd";
        constexpr const char* kChowHand = "3p 5p 1s 4s 7s 1m 4m 7m 2s 8s Sw Ww Gd";
        /// A hand with no claim on the 4p.
        constexpr const char* kQuietHand = "1p 7p 2s 5s 8s 2m 5m 8m Ww Rd 9p Nw 9m";
        /// A hand that pungs the 4p, one that waits on the 4p (it has to declare with its discard of the lap) and one that waits on the 4p
        /// as a single wait with a 4p of its own to pair it with: the winning tile is held twice.
        constexpr const char* kSeatPungs = "4p 4p 7p 1s 4s 7s 2m 5m 8m Sw Ww Rd 9p";
        constexpr const char* kSeatWaits = "3p 5p 4s 5s 6s 7m 8m 9m Ew Ew Ew 1m 1m";
        constexpr const char* kSeatWaitsAlone = "4p 1s 2s 3s 4s 5s 6s 7m 8m 9m Ew Ew Ew";
        constexpr const char* kDiscarder = "2p 5p 8p 2s 5s 8s 2m 5m 8m Rd Wd Nw 9s";

        constexpr SeatSetup kPersonAtSeat0{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kPersonAtSeat2{SeatKind::Computer, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};
        /// Times that a blink period (`kClaimBlinkSeconds` 0.5) divides exactly in floats: 0.375 s is in the off half, 0.125 s ends it.
        constexpr float kOffHalf = 0.375f;
        constexpr float kEndOfPeriod = 0.125f;
        constexpr float kHalfFlight = kTileFlightSeconds * 0.5f;
        constexpr int kPileEntryOfTheDiscard = 3;

        FixedWallSpec WindowSpec(const char* seat0, const char* seat1, const char* seat2) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = seat0;
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = kDiscarder;
            spec.live = "Wd Gd Nw 4p";
            return spec;
        }

        /// Seats 0, 1, 2 discard what they draw (the seats in `declaring` declare), then seat 3 discards the 4p: a window is open.
        HandModel WindowModel(const FixedWallSpec& spec, std::initializer_list<Seat> declaring) {
            HandModel model = MakeModel(spec);
            Lap(model, "Wd Gd Nw", declaring);
            EXPECT_EQ(model.CurrentSeat(), kSeat3);
            EXPECT_TRUE(model.Discard(kSeat3, T("4p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// The four bars the component draws round `rect`: `kSeatTurnFrameWidth` units wide, the side bars between the top and the bottom one.
        std::vector<Rect> BarsOf(const Rect& rect) {
            const float width = kSeatTurnFrameWidth;
            return {Rect(rect.x, rect.y, rect.w, width), Rect(rect.x, rect.y + rect.h - width, rect.w, width), Rect(rect.x, rect.y + width, width, rect.h - 2.0f * width),
                    Rect(rect.x + rect.w - width, rect.y + width, width, rect.h - 2.0f * width)};
        }

        /// Where a tile that arrives somewhere flies from, and what it shows there.
        struct Arrival {
            FlightOrigin from;
            SlotRole role = SlotRole::HandTile;
            Seat seat = 0;
            int index = 0;
            std::optional<Tile> face;
        };

    }  // namespace

    class MahjongTableLostClaimTest : public MahjongTableTestBase {
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

        /// A match with `seats` and `model` laid in; the first frame is drawn with the window open.
        void StartWindow(const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            Draw();
            ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
            ASSERT_EQ(_table->FlightCount(), 0u);
        }

        /// The person claims a pung at seat 0 against a computer's mahjong at seat 2: the computer decides at the next step, the hand ends.
        void PersonPungsAndAComputerWins() {
            ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kPungHand, kQuietHand, kSeatWaits), {kSeat2})));
            ASSERT_EQ(Hand().PendingClaimSeats(), (eastl::vector<Seat>{kSeat0, kSeat2}));
            _before = _table->GetLayout();
            EmitAction("MahjongClaimPung");
            ASSERT_EQ(Hand().GetPhase(), Phase::Claim) << "seat 2 has yet to decide";
            AdvanceTime(kComputerStepSeconds);
            ASSERT_EQ(Hand().GetPhase(), Phase::Finished);
            ASSERT_TRUE(Hand().Result().has_value());
            ASSERT_EQ(Hand().Result()->winner, kSeat2);
        }

        /// The person claims a chow at seat 0 against a computer's pung at seat 1.
        void PersonChowsAndAComputerPungs() {
            ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kChowHand, kSeatPungs, kSeatWaits), {})));
            ASSERT_EQ(Hand().PendingClaimSeats(), (eastl::vector<Seat>{kSeat0, kSeat1}));
            _before = _table->GetLayout();
            EmitAction("MahjongClaimChow", "0");
            ASSERT_EQ(Hand().GetPhase(), Phase::Claim) << "seat 1 has yet to decide";
            AdvanceTime(kComputerStepSeconds);
            ASSERT_FALSE(Hand().GetHand(kSeat1).melds.empty()) << "the computer at seat 1 claimed the pung";
        }

        /// The rectangles drawn in `color`.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        /// Whether the canvas on `node` is shown (a canvas that is shown takes the input).
        bool Shown(const char* node) const {
            const auto canvas = Canvas(node);
            return canvas && canvas->TakesInput();
        }

        Rect PlateOf(Seat seat) const {
            return _table->GetLayout().zones.seatPlates[seat];
        }

        /// The four bars of `kClaimLostFrame` round the plate of `seat` are drawn, once each.
        bool Framed(Seat seat) const {
            const std::vector<Rect> drawn = RectsOf(kClaimLostFrame);
            for (const Rect& bar : BarsOf(PlateOf(seat))) {
                int found = 0;
                for (const Rect& rect : drawn) {
                    found += SameRect(rect, bar) ? 1 : 0;
                }
                if (found != 1) {
                    return false;
                }
            }
            return true;
        }

        /// The plate of `seat` is filled with `kClaimHighlight` (the blink of the winner).
        bool Blinking(Seat seat) const {
            for (const Rect& rect : RectsOf(kClaimHighlight)) {
                if (SameRect(rect, PlateOf(seat))) {
                    return true;
                }
            }
            return false;
        }

        /// Nothing of the window is drawn: no frame, no blink on a plate.
        void ExpectNoMarks() const {
            EXPECT_TRUE(RectsOf(kClaimLostFrame).empty());
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_FALSE(Blinking(seat)) << "seat " << seat;
            }
        }

        const TileSlot* Find(const TableLayout& layout, SlotRole role, Seat seat, int index, int part = 0) const {
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == role && slot.seat == seat && slot.index == index && slot.part == part) {
                    return &slot;
                }
            }
            return nullptr;
        }

        FlightOrigin RectOfSlot(const TableLayout& layout, SlotRole role, Seat seat, int index, int part = 0) const {
            const TileSlot* slot = Find(layout, role, seat, index, part);
            EXPECT_NE(slot, nullptr) << "role " << static_cast<int>(role) << " seat " << seat << " index " << index;
            return slot != nullptr ? FlightOrigin{slot->rect, slot->rotation} : FlightOrigin{};
        }

        /// The last copy of `tile` in the concealed tiles of `seat`, worked out here.
        int LastCopy(Seat seat, Tile tile) const {
            const eastl::vector<Tile>& concealed = Hand().GetHand(seat).concealed;
            int found = -1;
            for (size_t i = 0; i < concealed.size(); ++i) {
                found = concealed[i] == tile ? static_cast<int>(i) : found;
            }
            return found;
        }

        /// Whether one of the tile draws is at `rect` and shows `face` (nullopt: a back).
        bool TileAt(const Rect& rect, const std::optional<Tile>& face) const {
            const Rect source = face ? SourceRect(*face) : BackRect();
            for (const DrawCall& call : TileCalls()) {
                if (SameRect(call.rect, rect) && call.source.has_value() && SameRect(*call.source, source)) {
                    return true;
                }
            }
            return false;
        }

        /// What the draw of an arrival `fraction` of the way covers, eased: the box of the tile as turned on the way.
        Rect Where(const Arrival& arrival, float fraction) const {
            const TileSlot* slot = Find(_table->GetLayout(), arrival.role, arrival.seat, arrival.index);
            if (slot == nullptr) {
                ADD_FAILURE() << "the layout has no slot seat " << arrival.seat << " index " << arrival.index;
                return Rect();
            }
            return fraction >= 1.0f ? slot->rect : BoxOf(FlightPlacement(TileFlight{SlotKey::Of(*slot), arrival.from.rect, arrival.from.rotation}, *slot, Ease(kTileFlightEase, fraction)));
        }

        /// The tile of `arrival` flies from where it started, half way and lands at its slot (the clock of the table moves between draws).
        void ExpectOneFlight(const Arrival& arrival) {
            ASSERT_EQ(_table->FlightCount(), 1u);
            EXPECT_EQ(TileCalls().size(), _table->GetLayout().slots.size()) << "every slot is drawn once";
            EXPECT_TRUE(TileAt(arrival.from, arrival.face)) << "it starts at the pile slot the discard left";
            AdvanceTime(kHalfFlight);
            Draw();
            ASSERT_EQ(_table->FlightCount(), 1u);
            const Rect half = Where(arrival, 0.5f);
            EXPECT_TRUE(TileAt(half, arrival.face)) << "half way, strictly between source and target";
            EXPECT_FALSE(SameRect(half, arrival.from));
            EXPECT_FALSE(SameRect(half, Where(arrival, 1.0f)));
            AdvanceTime(kHalfFlight);
            Draw();
            EXPECT_EQ(_table->FlightCount(), 0u) << "landed after kTileFlightSeconds";
            EXPECT_TRUE(TileAt(Where(arrival, 1.0f), arrival.face));
        }

        TableLayout _before;
    };

    TEST_F(MahjongTableLostClaimTest, ThePersonsPungBeatenByAMahjongFramesTheirPlateAndTheWinnersPlateBlinksForItsTime) {
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        Draw();

        EXPECT_EQ(Session().GetPhase(), SessionPhase::HandOver);
        EXPECT_TRUE(Framed(kSeat0)) << "four bars on the plate of the seat whose claim lost";
        EXPECT_EQ(RectsOf(kClaimLostFrame).size(), 4u);
        EXPECT_TRUE(Blinking(kSeat2)) << "on at the moment";
        EXPECT_FALSE(Blinking(kSeat0));
        EXPECT_FALSE(Framed(kSeat2));

        AdvanceTime(kOffHalf);
        Draw();
        EXPECT_FALSE(Blinking(kSeat2)) << "off in the second half of the period";
        EXPECT_TRUE(Framed(kSeat0)) << "the frame does not blink";

        AdvanceTime(kEndOfPeriod);
        Draw();
        EXPECT_TRUE(Blinking(kSeat2)) << "on again at the start of the next period";

        AdvanceTime(kClaimLostSeconds - kOffHalf - kEndOfPeriod);
        Draw();
        ExpectNoMarks();
        Draw();
        ExpectNoMarks();  // not back by drawing again
        EXPECT_EQ(Session().GetPhase(), SessionPhase::HandOver);
    }

    TEST_F(MahjongTableLostClaimTest, TheMarksAreDrawnAfterTheBaseOfTheirPlateAndBeforeTheFirstTile) {
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        Draw();

        int baseOf0 = -1;
        int baseOf2 = -1;
        int frameAt = -1;
        int blinkAt = -1;
        int firstTile = -1;
        for (size_t i = 0; i < _renderer->calls.size(); ++i) {
            const DrawCall& call = _renderer->calls[i];
            const int at = static_cast<int>(i);
            if (call.kind == DrawKind::Texture && call.texture == _atlasTexture) {
                firstTile = firstTile < 0 ? at : firstTile;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kSeatPlate)) {
                baseOf0 = SameRect(call.rect, PlateOf(kSeat0)) ? at : baseOf0;
                baseOf2 = SameRect(call.rect, PlateOf(kSeat2)) ? at : baseOf2;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kClaimLostFrame)) {
                frameAt = frameAt < 0 ? at : frameAt;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kClaimHighlight) && SameRect(call.rect, PlateOf(kSeat2))) {
                blinkAt = at;
            }
        }
        ASSERT_GE(baseOf0, 0);
        ASSERT_GE(baseOf2, 0);
        ASSERT_GE(frameAt, 0);
        ASSERT_GE(blinkAt, 0);
        ASSERT_GE(firstTile, 0);
        EXPECT_GT(frameAt, baseOf0);
        EXPECT_GT(blinkAt, baseOf2);
        EXPECT_LT(frameAt, firstTile);
        EXPECT_LT(blinkAt, firstTile);
    }

    TEST_F(MahjongTableLostClaimTest, TheMahjongFliesFromThePileSlotOfTheDiscardIntoTheWinnersHand) {
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        Draw();

        const int index = LastCopy(kSeat2, T("4p"));
        ASSERT_GE(index, 0);
        const FlightOrigin pile = RectOfSlot(_before, SlotRole::DiscardTile, kSeat3, kPileEntryOfTheDiscard);
        ExpectOneFlight(Arrival{pile, SlotRole::HandTile, kSeat2, index, T("4p")});
    }

    TEST_F(MahjongTableLostClaimTest, AWinningTileHeldTwiceFliesItsLastCopyAndTheOtherCopyStaysInPlace) {
        // Seat 2 waits on the 4p alone and holds one: the concealed tiles after the win are 4p 4p 1s ..., the second 4p is the new one
        ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kQuietHand, kSeatPungs, kSeatWaitsAlone), {kSeat2})));
        ASSERT_EQ(Hand().PendingClaimSeats(), (eastl::vector<Seat>{kSeat1, kSeat2}));
        const TableLayout before = _table->GetLayout();
        AdvanceTime(kComputerStepSeconds);  // the computers decide one after the other within the pause
        ASSERT_EQ(Hand().GetPhase(), Phase::Finished);
        ASSERT_EQ(Hand().Result()->winner, kSeat2);
        Draw();

        const eastl::vector<Tile>& concealed = Hand().GetHand(kSeat2).concealed;
        ASSERT_GE(concealed.size(), 2u);
        ASSERT_EQ(concealed[0], T("4p"));
        ASSERT_EQ(concealed[1], T("4p"));
        EXPECT_EQ(LastCopy(kSeat2, T("4p")), 1);
        const FlightOrigin pile = RectOfSlot(before, SlotRole::DiscardTile, kSeat3, kPileEntryOfTheDiscard);
        ExpectOneFlight(Arrival{pile, SlotRole::HandTile, kSeat2, 1, T("4p")});
    }

    TEST_F(MahjongTableLostClaimTest, ThePersonsChowBeatenByAPungFramesTheirPlateAndTheClaimedTileFliesIntoThePungsSet) {
        ASSERT_NO_FATAL_FAILURE(PersonChowsAndAComputerPungs());
        Draw();

        EXPECT_TRUE(Framed(kSeat0));
        EXPECT_EQ(RectsOf(kClaimLostFrame).size(), 4u);
        EXPECT_TRUE(Blinking(kSeat1)) << "the plate of the pung's owner";
        EXPECT_FALSE(Framed(kSeat1));
        EXPECT_FALSE(Blinking(kSeat0));
        // The claimed 4p flies from its pile slot as before: a face at the old pile rect in the first frame
        EXPECT_GT(_table->FlightCount(), 0u);
        EXPECT_TRUE(TileAt(RectOfSlot(_before, SlotRole::DiscardTile, kSeat3, kPileEntryOfTheDiscard), T("4p")));
    }

    TEST_F(MahjongTableLostClaimTest, ThePersonWhoPassesOrWinsTheWindowGetsNoFrameAndAWindowOfOneClaimMarksNothing) {
        // The person passes and a computer pungs: a pass is no claim
        ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kChowHand, kSeatPungs, kSeatWaits), {})));
        EmitAction("MahjongPass");
        AdvanceTime(kComputerStepSeconds);
        ASSERT_NE(Hand().GetPhase(), Phase::Claim);
        ASSERT_TRUE(Hand().LastClaimWindow().has_value());
        EXPECT_EQ(Hand().LastClaimWindow()->winner, kSeat1);
        Draw();
        ExpectNoMarks();

        // The person claims the pung and nobody else claims: the person is the winner of a window nobody lost
        ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kPungHand, kQuietHand, kSeatWaits), {})));
        ASSERT_EQ(Hand().PendingClaimSeats(), (eastl::vector<Seat>{kSeat0}));
        EmitAction("MahjongClaimPung");
        ASSERT_EQ(Hand().GetHand(kSeat0).melds.size(), 1u);
        Draw();
        ExpectNoMarks();
    }

    TEST_F(MahjongTableLostClaimTest, AWindowThatAllPassedOrNobodyClaimedInMarksNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kChowHand, kSeatPungs, kSeatWaits), {})));
        EmitAction("MahjongPass");
        ASSERT_TRUE(Model().Pass(kSeat1).has_value());
        ASSERT_NE(Hand().GetPhase(), Phase::Claim);
        ASSERT_TRUE(Hand().LastClaimWindow().has_value());
        EXPECT_EQ(Hand().LastClaimWindow()->winner, kNoSeat);
        Draw();
        ExpectNoMarks();

        // A discard nobody can claim opens no window
        FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "9p");
        ASSERT_NO_FATAL_FAILURE(Start(kPersonAtSeat0));
        Lay(MakeModel(spec));
        Draw();
        ClickHandTile(IndexOf(kSeat0, T("9p")));
        Draw();
        EXPECT_FALSE(Hand().LastClaimWindow().has_value());
        ExpectNoMarks();
    }

    TEST_F(MahjongTableLostClaimTest, TheMarksShowUnderTheHotSeatCoverAndForASpectator) {
        // Two people: the cover is up for the first, the model is played behind it (a chow of seat 0 beaten by a pung of seat 1)
        ASSERT_NO_FATAL_FAILURE(StartWindow(kTwoPeople, WindowModel(WindowSpec(kChowHand, kSeatPungs, kSeatWaits), {})));
        ASSERT_TRUE(Session().HandoffPending());
        ASSERT_TRUE(Model().Claim(kSeat0, ClaimKind::Chow, T("3p")).has_value());
        ASSERT_TRUE(Model().Claim(kSeat1, ClaimKind::Pung).has_value());
        Draw();
        EXPECT_TRUE(Framed(kSeat0));
        EXPECT_TRUE(Blinking(kSeat1));

        // A spectator: four computers
        ASSERT_NO_FATAL_FAILURE(StartWindow(kAllComputers, WindowModel(WindowSpec(kChowHand, kSeatPungs, kSeatWaits), {})));
        ASSERT_EQ(Session().Viewer(), kNoSeat);
        ASSERT_TRUE(Model().Claim(kSeat0, ClaimKind::Chow, T("3p")).has_value());
        ASSERT_TRUE(Model().Claim(kSeat1, ClaimKind::Pung).has_value());
        Draw();
        EXPECT_TRUE(Framed(kSeat0));
        EXPECT_TRUE(Blinking(kSeat1));
    }

    TEST_F(MahjongTableLostClaimTest, TheCoverIsUpWhileTheMarksShow) {
        // Two people: seat 0 (chow) and seat 1 (pung) decide behind the cover; the pung wins and seat 1 is to discard
        ASSERT_NO_FATAL_FAILURE(StartWindow(kTwoPeople, WindowModel(WindowSpec(kChowHand, kSeatPungs, kSeatWaits), {})));
        ASSERT_TRUE(Model().Claim(kSeat0, ClaimKind::Chow, T("3p")).has_value());
        ASSERT_TRUE(Model().Claim(kSeat1, ClaimKind::Pung).has_value());
        ASSERT_EQ(Hand().GetPhase(), Phase::Discard);
        Draw();
        ASSERT_TRUE(Session().HandoffPending()) << "another person is to act, the screen is covered";
        EXPECT_TRUE(CoverShown());
        EXPECT_TRUE(Framed(kSeat0));
        EXPECT_TRUE(Blinking(kSeat1));
    }

    TEST_F(MahjongTableLostClaimTest, TheMarksShowAtTheEndOfTheHandWithAllHandsOpen) {
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        Draw();
        ASSERT_EQ(Session().GetPhase(), SessionPhase::HandOver);
        EXPECT_TRUE(Framed(kSeat0));
        EXPECT_TRUE(Blinking(kSeat2));
    }

    TEST_F(MahjongTableLostClaimTest, TheClockOfTheMarksStandsStillWhileTheSeatChoiceOrTheRulesAreOpenAndGoesOnAfter) {
        for (const bool rules : {false, true}) {
            ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
            Draw();
            ASSERT_TRUE(Blinking(kSeat2));
            AdvanceTime(kOffHalf);
            Draw();
            ASSERT_FALSE(Blinking(kSeat2));

            if (rules) {
                _game->OpenRules();
            } else {
                _game->OpenSetup();
            }
            AdvanceTime(5.0f);
            Draw();
            EXPECT_FALSE(Blinking(kSeat2)) << (rules ? "rules" : "setup") << ": the same moment of the blink";
            EXPECT_TRUE(Framed(kSeat0)) << (rules ? "rules" : "setup") << ": nothing ran out";

            if (rules) {
                _game->CloseRules();
            } else {
                _game->CloseSetup();
            }
            AdvanceTime(kEndOfPeriod);
            Draw();
            EXPECT_TRUE(Blinking(kSeat2)) << "the blink goes on from where it stood";
            AdvanceTime(kClaimLostSeconds);
            Draw();
            ExpectNoMarks();
        }
    }

    TEST_F(MahjongTableLostClaimTest, ANewHandAResetAndAControlResetLeaveNoMark) {
        // A new hand
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        Draw();
        ASSERT_TRUE(Framed(kSeat0));
        ASSERT_TRUE(Session().NextHand().has_value());
        Draw();
        ExpectNoMarks();

        // Reset
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        Draw();
        ASSERT_TRUE(Framed(kSeat0));
        _game->Reset();
        Draw();
        ExpectNoMarks();

        // A control reset
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        Draw();
        ASSERT_TRUE(Framed(kSeat0));
        Json options = Json::object();
        options["seed"] = 5;
        ASSERT_TRUE(_game->AsControllable()->Reset(options).has_value());
        Draw();
        ExpectNoMarks();
    }

    TEST_F(MahjongTableLostClaimTest, AWindowSettledBeforeTheFirstDrawOfAHandIsNotNews) {
        // The table has not seen this hand yet: a hand laid in whose window is settled already shows nothing
        ASSERT_NO_FATAL_FAILURE(Start(kPersonAtSeat0));
        HandModel model = WindowModel(WindowSpec(kChowHand, kSeatPungs, kSeatWaits), {});
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T("3p")).has_value());
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        Lay(model);
        Draw();
        ExpectNoMarks();
    }

    TEST_F(MahjongTableLostClaimTest, TheMarksFollowTheViewerAndSitOnThePlatesWhereTheSeatsAre) {
        // A person at seat 2 holds the pung; seat 1 (a computer) has declared and wins on the 4p
        ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat2, WindowModel(WindowSpec(kQuietHand, kSeatWaits, kPungHand), {kSeat1})));
        ASSERT_EQ(_table->GetLayout().viewer, kSeat2);
        ASSERT_EQ(SeatPosition(kSeat2, kSeat2), 0);
        AdvanceTime(kComputerStepSeconds);  // seat 1 claims the mahjong; seat 2 is a person and has yet to decide
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        EmitAction("MahjongClaimPung");
        ASSERT_EQ(Hand().GetPhase(), Phase::Finished);
        Draw();

        EXPECT_TRUE(Framed(kSeat2));
        EXPECT_TRUE(Blinking(kSeat1));
        // The plate of the person is the one at the bottom of the table: lower than every other plate
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat != kSeat2) {
                EXPECT_GT(PlateOf(kSeat2).y, PlateOf(seat).y) << "seat " << seat;
            }
        }
        EXPECT_EQ(RectsOf(kClaimLostFrame).size(), 4u);
    }

    TEST_F(MahjongTableLostClaimTest, TheColourOfTheFrameIsGoldWithAnAlphaOfItsOwn) {
        EXPECT_EQ(kClaimLostFrame.r, kTurnMark.r);
        EXPECT_EQ(kClaimLostFrame.g, kTurnMark.g);
        EXPECT_EQ(kClaimLostFrame.b, kTurnMark.b);
        for (const Color& other : {kTurnMark, kPendingFrame, kLastDiscardMark, kDrawnTileMark, kCallingFrame, kClaimedEdge, kClaimHighlight, kDeclareMark, kKeepsWaitingMark, kAdvisedFrame,
                                   kSeatTurnFrame, kVeil, kTileShadow, kTileShadowSoft, kFeltLip, kSeatPlate, kDealerMark}) {
            EXPECT_FALSE(SameColor(kClaimLostFrame, other));
        }
        EXPECT_FLOAT_EQ(kClaimLostSeconds, kClaimHighlightSeconds);
    }

    // ----- the score table waits when the person's claim lost to a mahjong -----

    TEST_F(MahjongTableLostClaimTest, TheScoreTableWaitsForTheMarksWhenThePersonsPungLostToAMahjongAndComesAfterwards) {
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        ASSERT_EQ(Session().GetPhase(), SessionPhase::HandOver);
        EXPECT_TRUE(_table->HandOverHeld());
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
        EXPECT_TRUE(Shown("HudMenu")) << "the score table is not up, so the menu stays";
        EXPECT_FALSE(Var("handOverHeadline").empty()) << "the words are published at once";
        // The flight and the marks are on the table meanwhile
        EXPECT_EQ(_table->FlightCount(), 1u);
        EXPECT_TRUE(Framed(kSeat0));
        EXPECT_TRUE(Blinking(kSeat2));

        AdvanceTime(kClaimLostSeconds - kEndOfPeriod);
        Draw();
        EXPECT_TRUE(_table->HandOverHeld());
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
        EXPECT_EQ(_table->FlightCount(), 0u) << "the tile has landed long ago";

        AdvanceTime(kEndOfPeriod);
        Draw();
        EXPECT_FALSE(_table->HandOverHeld());
        EXPECT_TRUE(Shown("HandOver"));
        EXPECT_TRUE(Shown("HandOverAction"));
        EXPECT_FALSE(Shown("HudMenu"));
        ExpectNoMarks();

        // The button works as it always does
        const size_t settled = _game->GetMatch().History().size();
        ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
        EXPECT_EQ(_game->GetMatch().History().size(), settled + 1);
        EXPECT_EQ(Session().GetPhase(), SessionPhase::Playing);
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
    }

    TEST_F(MahjongTableLostClaimTest, ThePersonsPungBeatenByAMahjongSettledByTheControlApiHoldsNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kPungHand, kQuietHand, kSeatWaits), {kSeat2})));
        const auto action = ParseJson(R"({"type": "claim", "claim": "pung"})").value();
        const auto outcome = _game->AsControllable()->PerformAction(action);
        ASSERT_TRUE(outcome.has_value());
        ASSERT_EQ(Session().GetPhase(), SessionPhase::HandOver);
        ASSERT_EQ(Hand().Result()->winner, kSeat2);

        EXPECT_FALSE(_table->HandOverHeld());
        Draw();
        EXPECT_TRUE(Shown("HandOver"));
        EXPECT_TRUE(Shown("HandOverAction"));
        EXPECT_TRUE(Framed(kSeat0)) << "the marks of the window show all the same";
    }

    TEST_F(MahjongTableLostClaimTest, AClaimOfAComputerThatLostToAMahjongHoldsNothing) {
        // The person has no claim; seat 1 pungs, seat 2 wins: the beaten seat is a computer
        ASSERT_NO_FATAL_FAILURE(StartWindow(kPersonAtSeat0, WindowModel(WindowSpec(kQuietHand, kSeatPungs, kSeatWaits), {kSeat2})));
        ASSERT_EQ(Hand().PendingClaimSeats(), (eastl::vector<Seat>{kSeat1, kSeat2}));
        AdvanceTime(kComputerStepSeconds);
        ASSERT_EQ(Hand().GetPhase(), Phase::Finished);
        ASSERT_EQ(Hand().Result()->winner, kSeat2);
        ASSERT_TRUE(Hand().LastClaimWindow().has_value());
        ASSERT_EQ(Hand().LastClaimWindow()->beaten.size(), 1u);
        ASSERT_EQ(Hand().LastClaimWindow()->beaten[0].seat, kSeat1);

        EXPECT_FALSE(_table->HandOverHeld());
        Draw();
        EXPECT_TRUE(Shown("HandOver"));
        EXPECT_TRUE(Framed(kSeat1)) << "the marks show, the score table does not wait";
    }

    TEST_F(MahjongTableLostClaimTest, ThePersonsChowBeatenByAPungHoldsNothingAsTheHandGoesOn) {
        ASSERT_NO_FATAL_FAILURE(PersonChowsAndAComputerPungs());
        EXPECT_FALSE(_table->HandOverHeld());
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_NE(Session().GetPhase(), SessionPhase::HandOver);
    }

    TEST_F(MahjongTableLostClaimTest, TheHoldEndsAtOnceWhenAControlClientLeavesTheHandOver) {
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        ASSERT_TRUE(_table->HandOverHeld());
        const auto action = ParseJson(R"({"type": "next_hand"})").value();
        ASSERT_TRUE(_game->AsControllable()->PerformAction(action).has_value());

        EXPECT_FALSE(_table->HandOverHeld()) << "no frame has passed";
        EXPECT_NE(Session().GetPhase(), SessionPhase::HandOver);
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
        // A later hand that ends by itself is not held by what is left
        EXPECT_FALSE(_table->HandOverHeld());
    }

    TEST_F(MahjongTableLostClaimTest, TheHoldIsVoidInANewMatchAndAfterReset) {
        ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
        ASSERT_TRUE(_table->HandOverHeld());
        _game->Reset();
        EXPECT_FALSE(_table->HandOverHeld());
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
    }

    TEST_F(MahjongTableLostClaimTest, TheHoldStandsStillWhileTheSeatChoiceOrTheRulesAreOpenAndGoesOnAfter) {
        for (const bool rules : {false, true}) {
            ASSERT_NO_FATAL_FAILURE(PersonPungsAndAComputerWins());
            ASSERT_TRUE(_table->HandOverHeld());
            AdvanceTime(kClaimLostSeconds - kEndOfPeriod);
            ASSERT_TRUE(_table->HandOverHeld());

            if (rules) {
                _game->OpenRules();
            } else {
                _game->OpenSetup();
            }
            AdvanceTime(5.0f);
            EXPECT_TRUE(_table->HandOverHeld()) << (rules ? "rules" : "setup") << ": the time did not run";

            if (rules) {
                _game->CloseRules();
            } else {
                _game->CloseSetup();
            }
            AdvanceTime(kEndOfPeriod);
            EXPECT_FALSE(_table->HandOverHeld());
            Draw();
            EXPECT_TRUE(Shown("HandOver"));
        }
    }

}  // namespace BECore::Mahjong::Tests
