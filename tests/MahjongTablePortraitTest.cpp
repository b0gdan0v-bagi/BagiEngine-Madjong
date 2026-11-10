#include "ComputerTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The table in the portrait view (`Table/TableLayout.h`, `TableShape`): the scene picks the view of 720 x 1280 for an output at least as
 * high as it is wide, and the component lays the table out for it, fits it into the window and plays it by click and tap. The fixture's
 * renderer stands for the window (`SetWindow`). In the design window (720 x 1280 at density 1) the view is the output, so a rect of a draw
 * call is a rect of the layout; the fitted windows (a phone, a 3:4 tablet, a square) put bars on a pair of sides and the tests map the
 * layout through `TableFit`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seat 0 holds a hand that is far from waiting; as East it draws 9p, so it holds 14 tiles and is the one to discard.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        constexpr SeatSetup kPersonAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        /// The fill behind a set and the frame of a declared hand reach this far past the tiles (`kActingMargin` of the component).
        constexpr float kActingMargin = 4.0f;
        constexpr float kPendingMargin = 3.0f;
        /// A tolerance for a mark carried through the fit of the window (single floats).
        constexpr float kPixel = 0.05f;
        constexpr float kHalfFlight = kTileFlightSeconds * 0.5f;

        /// The portrait windows the table is checked in: the design size, two phones (bars on the top and the bottom), a 3:4 tablet and a
        /// square (bars on the sides).
        const std::vector<MatrixWindow>& PortraitWindows() {
            static const std::vector<MatrixWindow> windows{DesignWindowOf(TableShape::Portrait), MakePhoneWindow(390.0f, 844.0f, 3.0f), MakePhoneWindow(360.0f, 640.0f, 2.0f),
                                                           MakePhoneWindow(768.0f, 1024.0f, 1.0f), MakePhoneWindow(480.0f, 480.0f, 1.0f)};
            return windows;
        }

        /// A window too narrow for the buttons of `Actions` to be touch targets, with a bar under the portrait table for the compact row.
        MatrixWindow NarrowPortraitWindow() {
            return MakePhoneWindow(300.0f, 900.0f, 1.0f);
        }

        bool NearRect(const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) < kPixel && std::abs(a.y - b.y) < kPixel && std::abs(a.w - b.w) < kPixel && std::abs(a.h - b.h) < kPixel;
        }

        /// The four bars of width `width` on the border of `rect`, inside it, as the table draws a frame.
        std::vector<Rect> BarsOf(const Rect& rect, float width) {
            return {Rect(rect.x, rect.y, rect.w, width), Rect(rect.x, rect.y + rect.h - width, rect.w, width), Rect(rect.x, rect.y + width, width, rect.h - 2.0f * width),
                    Rect(rect.x + rect.w - width, rect.y + width, width, rect.h - 2.0f * width)};
        }

        Rect Padded(const Rect& rect, float margin) {
            return Rect(rect.x - margin, rect.y - margin, rect.w + 2.0f * margin, rect.h + 2.0f * margin);
        }

    }  // namespace

    class MahjongTablePortraitTest : public MahjongTableTestBase {
    protected:
        /// A match with seat 0 holding a fixed hand (it is to discard), drawn in `window`.
        void StartIn(const MatrixWindow& window, const SeatSetup& seats = kDefaultSeatSetup) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(MakeModel(SpecOf(kPlainHand, "9p"), kSeat0));
            SetWindow(*_renderer, window);
            Draw();
        }

        /// A match with `model` laid, drawn in `window`.
        void StartWith(const MatrixWindow& window, const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            SetWindow(*_renderer, window);
            Draw();
        }

        const Scene& MahjongScene() const {
            return *_manager->GetSceneByName("MahjongScene"_intern);
        }

        /// A rect of the view in output pixels as the window shows it.
        static Rect Out(const MatrixWindow& window, const Rect& rect) {
            return TableFit(window).ToOutput(rect.x, rect.y, rect.w, rect.h);
        }

        /// The middle of a rect of the view in output pixels.
        static std::pair<float, float> OutCentre(const MatrixWindow& window, const Rect& rect) {
            const Rect out = Out(window, rect);
            return {out.x + out.w * 0.5f, out.y + out.h * 0.5f};
        }

        /// The rectangles drawn in `color` (output pixels), in the order drawn.
        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        /// How many of `expected` (view units, carried through the fit of the window) are among the rects drawn in `color`.
        int CountDrawn(const MatrixWindow& window, const Color& color, const std::vector<Rect>& expected) const {
            const std::vector<Rect> drawn = RectsOf(color);
            int found = 0;
            for (const Rect& want : expected) {
                const Rect out = Out(window, want);
                found += std::any_of(drawn.begin(), drawn.end(), [&](const Rect& rect) { return NearRect(rect, out); }) ? 1 : 0;
            }
            return found;
        }

        /// The first slot of the layout of the last draw with this role, seat and index.
        const TileSlot* SlotOf(SlotRole role, Seat seat, int index, int part = 0) const {
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == role && slot.seat == seat && slot.index == index && slot.part == part) {
                    return &slot;
                }
            }
            return nullptr;
        }

        /// Every draw call of a tile lies inside the output.
        void ExpectEveryTileInsideTheOutput(const MatrixWindow& window, const char* what) const {
            for (const DrawCall& call : TileCalls()) {
                EXPECT_GE(call.rect.x, -kPixel) << window.label.c_str() << " " << what;
                EXPECT_GE(call.rect.y, -kPixel) << window.label.c_str() << " " << what;
                EXPECT_LE(call.rect.x + call.rect.w, window.outputWidth + kPixel) << window.label.c_str() << " " << what;
                EXPECT_LE(call.rect.y + call.rect.h, window.outputHeight + kPixel) << window.label.c_str() << " " << what;
            }
        }

        /// The canvas on `node` is the one on screen: shown and taking input.
        bool Shown(const char* node) const {
            const auto canvas = Canvas(node);
            return canvas && canvas->TakesInput();
        }

        void FlyThreeSteps(const MatrixWindow& window);

        /// East discards the 5p it drew; seat 1 holds 4p 5p 5p 6p 7p, so it may pung or take the chow 4-6p or 5-7p.
        HandModel ClaimWindowModel() const {
            FixedWallSpec spec = SpecOf(kPlainHand, "5p");
            spec.hands[kSeat1] = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }
    };

    TEST_F(MahjongTablePortraitTest, ATallOutputDrawsThePortraitViewAndAWideOneTheLandscapeOne) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        for (const MatrixWindow& window : PortraitWindows()) {
            SetWindow(*_renderer, window);
            Draw();
            EXPECT_EQ(MahjongScene().GetViewWidth(), kPortraitViewWidth) << window.label.c_str();
            EXPECT_EQ(MahjongScene().GetViewHeight(), kPortraitViewHeight) << window.label.c_str();
            EXPECT_EQ(_table->GetLayout().shape, TableShape::Portrait) << window.label.c_str();
            EXPECT_EQ(_table->GetLayout().viewWidth, kPortraitViewWidth) << window.label.c_str();
            EXPECT_EQ(_table->GetLayout().viewHeight, kPortraitViewHeight) << window.label.c_str();
        }
        for (const MatrixWindow& window : {DesignWindowOf(TableShape::Landscape), MakePhoneWindow(915.0f, 330.0f, 2.6f)}) {
            SetWindow(*_renderer, window);
            Draw();
            EXPECT_EQ(MahjongScene().GetViewWidth(), kViewWidth) << window.label.c_str();
            EXPECT_EQ(_table->GetLayout().shape, TableShape::Landscape) << window.label.c_str();
        }
    }

    TEST_F(MahjongTablePortraitTest, TheLayoutIsThePortraitLayoutOfTheWindowAndHasNoPhoneProfile) {
        for (const MatrixWindow& window : PortraitWindows()) {
            ASSERT_NO_FATAL_FAILURE(StartIn(window));
            const TableLayout& layout = _table->GetLayout();
            EXPECT_TRUE(SameSlots(layout, TableLayoutFor(window, InputOf(Hand())))) << window.label.c_str();
            EXPECT_EQ(layout.profile, TableProfile{}) << window.label.c_str() << ": the portrait layout ignores the profile";
            int tiles = 0;
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::HandTile && slot.seat == layout.viewer) {
                    ++tiles;
                    EXPECT_EQ(slot.rect.w, kViewerHandTileW) << window.label.c_str();
                    EXPECT_EQ(slot.rect.h, kViewerHandTileH) << window.label.c_str();
                }
            }
            EXPECT_EQ(tiles, kDealtTiles + 1) << window.label.c_str() << ": one row of 14";
        }
    }

    TEST_F(MahjongTablePortraitTest, EveryTileIsDrawnAtItsSlotAndTheSideSeatsAreTurned) {
        // The design window: the view is the output, so the draw call of a slot is the slot
        ASSERT_NO_FATAL_FAILURE(StartIn(DesignWindowOf(TableShape::Portrait)));
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0));
        const TableLayout& layout = _table->GetLayout();
        // The seat on the right (seat 1 for the viewer at seat 0) is turned 270, the seat on the left 90, the top and the bottom not at all
        int turnedLeft = 0;
        int turnedRight = 0;
        for (const TileSlot& slot : layout.slots) {
            const bool handOrMeld = slot.role == SlotRole::HandTile || slot.role == SlotRole::MeldTile;
            if (!handOrMeld || slot.seat == layout.viewer || SeatPosition(layout.viewer, slot.seat) == 2) {
                continue;
            }
            const int position = SeatPosition(layout.viewer, slot.seat);
            EXPECT_EQ(slot.rotation, position == 1 ? 270.0f : 90.0f) << "seat " << slot.seat;
            (position == 1 ? turnedRight : turnedLeft)++;
        }
        EXPECT_EQ(turnedRight, kDealtTiles);
        EXPECT_EQ(turnedLeft, kDealtTiles);
        size_t turns = 0;
        for (size_t i = 0; i < _renderer->calls.size(); ++i) {
            turns += _renderer->TurnOf(i) != nullptr ? 1 : 0;
        }
        EXPECT_GE(turns, static_cast<size_t>(2 * kDealtTiles)) << "the side seats' tiles are drawn turned in the record";
    }

    TEST_F(MahjongTablePortraitTest, TheFeltAndTheRimFillTheViewInEveryPortraitWindow) {
        for (const MatrixWindow& window : PortraitWindows()) {
            ASSERT_NO_FATAL_FAILURE(StartIn(window));
            const Rect view = Out(window, Rect(0.0f, 0.0f, kPortraitViewWidth, kPortraitViewHeight));
            bool felt = false;
            for (const DrawCall& call : _renderer->calls) {
                felt = felt || (call.kind == DrawKind::Texture && call.texture != _atlasTexture && NearRect(call.rect, view));
            }
            EXPECT_TRUE(felt) << window.label.c_str() << ": the felt covers the view";
            // The rim is the four bars of `kRimWidth` units on the border of the view, as in landscape
            float area = 0.0f;
            for (const Rect& rect : RectsOf(kFeltRim)) {
                area += rect.w * rect.h;
            }
            const float scale = TableFit(window).scale;
            const float inner = (kPortraitViewWidth - 2.0f * kRimWidth) * (kPortraitViewHeight - 2.0f * kRimWidth);
            EXPECT_NEAR(area, (kPortraitViewWidth * kPortraitViewHeight - inner) * scale * scale, view.w * view.h * 1e-4f) << window.label.c_str();
        }
    }

    TEST_F(MahjongTablePortraitTest, AClickOnAHandTileInAFittedWindowDiscardsThatTile) {
        // A window with a hand tile of at least the touch minimum: the regime is the mouse's; the bars are on the sides (3:4) or above and below
        for (const MatrixWindow& window : {MakePhoneWindow(768.0f, 1024.0f, 1.0f), MakePhoneWindow(720.0f, 1280.0f, 1.0f), MakePhoneWindow(1000.0f, 1300.0f, 1.0f)}) {
            for (int index = 0; index < kDealtTiles + 1; ++index) {
                ASSERT_NO_FATAL_FAILURE(StartIn(window));
                ASSERT_EQ(_game->GetSession().Actor(), kSeat0);
                const Tile tile = Hand().GetHand(kSeat0).concealed[static_cast<size_t>(index)];
                const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat0, index);
                ASSERT_NE(slot, nullptr);
                const auto [x, y] = OutCentre(window, slot->rect);
                EXPECT_FALSE(Click(x, y)) << "no canvas takes the click: the table does";
                ASSERT_FALSE(Hand().Discards().empty()) << window.label.c_str() << " tile " << index;
                EXPECT_EQ(Hand().Discards().front().tile, tile) << window.label.c_str() << " tile " << index;
            }
        }
    }

    TEST_F(MahjongTablePortraitTest, ATapOnAHandTileSelectsItAndASecondOneDiscardsItInAPhone) {
        for (const MatrixWindow& window : {MakePhoneWindow(390.0f, 844.0f, 3.0f), MakePhoneWindow(360.0f, 640.0f, 2.0f), MakePhoneWindow(768.0f, 1024.0f, 1.0f)}) {
            for (int index = 0; index < kDealtTiles + 1; ++index) {
                ASSERT_NO_FATAL_FAILURE(StartIn(window));
                const Tile tile = Hand().GetHand(kSeat0).concealed[static_cast<size_t>(index)];
                const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat0, index);
                ASSERT_NE(slot, nullptr);
                const auto [x, y] = OutCentre(window, slot->rect);
                EXPECT_FALSE(Tap(x, y));
                EXPECT_TRUE(Hand().Discards().empty()) << window.label.c_str() << " tile " << index << ": the first tap only selects";
                Draw();
                EXPECT_EQ(_table->SelectedHandIndex(), index) << window.label.c_str();
                EXPECT_FALSE(Tap(x, y));
                ASSERT_FALSE(Hand().Discards().empty()) << window.label.c_str() << " tile " << index;
                EXPECT_EQ(Hand().Discards().front().tile, tile) << window.label.c_str() << " tile " << index;
            }
        }
    }

    TEST_F(MahjongTablePortraitTest, TheOneRowHandOfAPhoneSelectsByTheRuleAndTheDesktopSizeDoesNot) {
        // 390 pt wide: the tile of 48 units is 26 pt, under the touch minimum, so a click selects; a window that holds the tile at the minimum discards at once
        const MatrixWindow phone = MakePhoneWindow(390.0f, 844.0f, 3.0f);
        ASSERT_NO_FATAL_FAILURE(StartIn(phone));
        const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat0, 3);
        ASSERT_NE(slot, nullptr);
        EXPECT_LT(slot->rect.w * TableFit(phone).scale / phone.pixelDensity, EngineTouchMinimum());
        const auto [x, y] = OutCentre(phone, slot->rect);
        ASSERT_FALSE(Click(x, y));
        Draw();
        EXPECT_EQ(_table->SelectedHandIndex(), 3) << "a mouse click selects where the tile is under the minimum";
        ASSERT_NO_FATAL_FAILURE(StartIn(DesignWindowOf(TableShape::Portrait)));
        const TileSlot* big = SlotOf(SlotRole::HandTile, kSeat0, 3);
        ASSERT_NE(big, nullptr);
        ASSERT_FALSE(Click(big->rect.x + big->rect.w * 0.5f, big->rect.y + big->rect.h * 0.5f));
        EXPECT_FALSE(Hand().Discards().empty()) << "a window with a tile of 48 pt: the click discards";
    }

    TEST_F(MahjongTablePortraitTest, TheSelectedTileRisesOverItsSlotAndKeepsClearOfTheBonusAndMeldRow) {
        const MatrixWindow window = MakePhoneWindow(390.0f, 844.0f, 3.0f);
        ASSERT_NO_FATAL_FAILURE(StartIn(window));
        const int index = 5;
        const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat0, index);
        ASSERT_NE(slot, nullptr);
        const Rect slotRect = slot->rect;
        const auto [x, y] = OutCentre(window, slotRect);
        Tap(x, y);
        Draw();
        ASSERT_EQ(_table->SelectedHandIndex(), index) << "tap-to-select is on in a 390 x 844 @3 window";
        const Rect raised = RaisedRect(slotRect);
        EXPECT_FLOAT_EQ(raised.y, slotRect.y - kSelectedTileRise);
        const std::vector<DrawCall> calls = TileCalls();
        ASSERT_FALSE(calls.empty());
        EXPECT_TRUE(NearRect(calls.back().rect, Out(window, raised))) << "the raised tile is drawn last, at its raised rect";

        // In the fullest table of a viewer the raised hand tile covers no tile of the viewer's bonus and meld rows
        const TableLayout worst = MakeWorstCaseTableLayout(0, window);
        for (const TileSlot& hand : worst.slots) {
            if (hand.role != SlotRole::HandTile || hand.seat != worst.viewer) {
                continue;
            }
            const Rect up = RaisedRect(hand.rect);
            for (const TileSlot& other : worst.slots) {
                if (other.seat == worst.viewer && (other.role == SlotRole::BonusTile || other.role == SlotRole::MeldTile)) {
                    EXPECT_FALSE(Intersects(up, other.rect)) << "hand tile " << hand.index << " raised over a slot of role " << static_cast<int>(other.role);
                }
            }
        }
    }

    TEST_F(MahjongTablePortraitTest, TheMarksLieOnTheScreenRectanglesOfTheSlotsInEveryPortraitWindow) {
        for (const MatrixWindow& window : PortraitWindows()) {
            // Seat 1 has pung East's 5p (the bar on the claimed tile), seat 2 has declared its calling hand (the frame round its hand), seat 1 is to act
            ASSERT_NO_FATAL_FAILURE(StartWith(window, kDefaultSeatSetup, OpenPungModel(kSeat1Hand, kSeat2Waits, kSeat3Waits, {kSeat2})));
            const TableLayout& layout = _table->GetLayout();

            const TileSlot* claimed = nullptr;
            for (const TileSlot& slot : layout.slots) {
                if (slot.claimEdge != Edge::None) {
                    ASSERT_EQ(claimed, nullptr) << "one claimed tile";
                    claimed = &slot;
                }
            }
            ASSERT_NE(claimed, nullptr) << window.label.c_str();
            EXPECT_EQ(claimed->seat, kSeat1);
            EXPECT_EQ(claimed->claimEdge, Edge::Bottom) << "seat 1 is on the right, the claimed tile came from seat 0 below it";
            // The bar is a strip of `kClaimedEdgeWidth` units along the edge, in the screen rectangle of the slot (the rotated slot's own)
            const Rect edge(claimed->rect.x, claimed->rect.y + claimed->rect.h - kClaimedEdgeWidth, claimed->rect.w, kClaimedEdgeWidth);
            EXPECT_EQ(CountDrawn(window, kClaimedEdge, {edge}), 1) << window.label.c_str();

            const Rect hand = BoundsOf(layout, SlotRole::HandTile, kSeat2);
            EXPECT_EQ(CountDrawn(window, kCallingFrame, BarsOf(Padded(hand, kActingMargin), kCallingFrameWidth)), 4) << window.label.c_str() << ": the frame of the hand at the top";
            EXPECT_EQ(RectsOf(kCallingFrame).size(), 4u) << window.label.c_str() << ": only that hand is framed";
        }
    }

    TEST_F(MahjongTablePortraitTest, TheChowFrameLiesOnTheTwoTilesTheHoveredChowTakesInEveryPortraitWindow) {
        for (const MatrixWindow& window : PortraitWindows()) {
            ASSERT_NO_FATAL_FAILURE(StartWith(window, kPersonAtSeat1, ClaimWindowModel()));
            ASSERT_EQ(_game->GetSession().Actor(), kSeat1);
            if (IsCompactWindow(window)) {
                continue;  // the buttons of `ActionsCompact` have no hover
            }
            const auto canvas = Canvas("Actions");
            const UIElement* chow = Element("Actions", "Chow0");
            ASSERT_TRUE(canvas && chow) << window.label.c_str();
            const auto [x, y] = OutputCenterOf(*canvas, *chow);
            SDL_MouseMotionEvent motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.x = x;
            motion.y = y;
            CoreManager::GetUIInput().DispatchMouseMotion(motion);
            Draw();

            const ChowTakes& takes = _table->GetChowTakes();
            ASSERT_EQ(takes.count, 2) << window.label.c_str();
            std::vector<Rect> expected;
            for (int k = 0; k < takes.count; ++k) {
                const int index = IndexOf(kSeat1, takes.tiles[static_cast<size_t>(k)]);
                const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat1, index);
                ASSERT_NE(slot, nullptr);
                for (const Rect& bar : BarsOf(slot->rect, kPendingMargin)) {
                    expected.push_back(bar);
                }
            }
            EXPECT_EQ(CountDrawn(window, kPendingFrame, expected), static_cast<int>(expected.size())) << window.label.c_str();
        }
    }

    TEST_F(MahjongTablePortraitTest, TheActionCanvasesFollowTheCompactRuleOfThePortraitView) {
        std::vector<MatrixWindow> windows = PortraitWindows();
        windows.push_back(NarrowPortraitWindow());
        size_t compact = 0;
        size_t plain = 0;
        for (const MatrixWindow& window : windows) {
            ASSERT_NO_FATAL_FAILURE(StartWith(window, kPersonAtSeat1, ClaimWindowModel()));
            const bool rule = IsCompactWindow(window);
            EXPECT_EQ(Shown("Actions"), !rule) << window.label.c_str();
            EXPECT_EQ(Shown("HintsButton"), !rule) << window.label.c_str() << ": the button of the hints goes with `Actions`";
            EXPECT_EQ(Shown("ActionsCompact"), rule) << window.label.c_str();
            (rule ? compact : plain)++;
        }
        EXPECT_GT(compact, 0u) << "a narrow portrait window shows the compact row";
        EXPECT_GT(plain, 0u) << "a phone's 68-unit buttons are touch targets without it";
        // A phone of 390 pt keeps `Actions` (the button is 68 x 0.5417 = 36.8 pt), a 300 pt window does not
        EXPECT_FALSE(IsCompactWindow(MakePhoneWindow(390.0f, 844.0f, 3.0f)));
        EXPECT_TRUE(IsCompactWindow(NarrowPortraitWindow()));
    }

    TEST_F(MahjongTablePortraitTest, ARotationOfTheWindowSwapsTheShapeAndStartsTheTilesWithNoFlight) {
        UseFlights();
        const MatrixWindow landscape = DesignWindowOf(TableShape::Landscape);
        const MatrixWindow portrait = DesignWindowOf(TableShape::Portrait);

        // Control: a discard between two draws in the same window flies
        ASSERT_NO_FATAL_FAILURE(StartIn(landscape));
        const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat0, 0);
        ASSERT_NE(slot, nullptr);
        EXPECT_FALSE(Click(slot->rect.x + slot->rect.w * 0.5f, slot->rect.y + slot->rect.h * 0.5f));
        Draw();
        EXPECT_GT(_table->FlightCount(), 0u) << "a discard makes a flight";

        // The same discard and the window turns upright before the next draw: the tiles are in place in the portrait table
        ASSERT_NO_FATAL_FAILURE(StartIn(landscape));
        slot = SlotOf(SlotRole::HandTile, kSeat0, 0);
        ASSERT_NE(slot, nullptr);
        EXPECT_FALSE(Click(slot->rect.x + slot->rect.w * 0.5f, slot->rect.y + slot->rect.h * 0.5f));
        SetWindow(*_renderer, portrait);
        Draw();
        EXPECT_EQ(_table->GetLayout().shape, TableShape::Portrait);
        EXPECT_EQ(_table->FlightCount(), 0u) << "another shape: no flight crosses it";
        ExpectEveryTileInsideTheOutput(portrait, "after the turn");

        // And back: the landscape table is today's again
        SetWindow(*_renderer, landscape);
        Draw();
        EXPECT_EQ(_table->GetLayout().shape, TableShape::Landscape);
        EXPECT_EQ(_table->FlightCount(), 0u);
    }

    /// Three steps of the computers in `window` (the clock of the table has run 0.3 s after each; a fourth would make the component step on
    /// its own), every tile in the air inside the output: seat 1, on the right, draws and discards in the second.
    void MahjongTablePortraitTest::FlyThreeSteps(const MatrixWindow& window) {
        UseFlights();
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        SetWindow(*_renderer, window);
        Draw();
        bool sideSeatMoved = false;
        for (int step = 0; step < 3; ++step) {
            ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
            const Seat mover = Hand().Moves().empty() ? kNoSeat : Hand().Moves().back().seat;
            sideSeatMoved = sideSeatMoved || mover == kSeat1 || mover == kSeat3;
            Draw();
            ExpectEveryTileInsideTheOutput(window, "at the start of a flight");
            AdvanceTime(kHalfFlight);
            Draw();
            ExpectEveryTileInsideTheOutput(window, "half way");
            AdvanceTime(kHalfFlight);
            Draw();
            EXPECT_EQ(_table->FlightCount(), 0u) << window.label.c_str() << " step " << step;
        }
        EXPECT_TRUE(sideSeatMoved) << window.label.c_str();
    }

    TEST_F(MahjongTablePortraitTest, TheTilesOfAPortraitTableFlyInsideTheView) {
        FlyThreeSteps(DesignWindowOf(TableShape::Portrait));
    }

    TEST_F(MahjongTablePortraitTest, TheTilesOfAPortraitTableFlyInsideAFittedWindowToo) {
        FlyThreeSteps(MakePhoneWindow(390.0f, 844.0f, 3.0f));
    }

}  // namespace BECore::Mahjong::Tests
