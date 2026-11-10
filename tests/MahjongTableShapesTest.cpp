#include "ComputerTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The table in the narrow and the wide view (`Table/TableLayout.h`, `TableShape`), after the pattern of `MahjongTablePortraitTest`: the scene
 * picks the view of 1080 x 768 for a landscape output at most 1.55 times as wide as high and the one of 1560 x 720 for an output twice as wide
 * as high or more, and the component lays the table out for it, fits it into the window and plays it by click and tap. The fixture's renderer
 * stands for the window (`SetWindow`). In the design window (the view at density 1) the view is the output, so a rect of a draw call is a rect of
 * the layout; the fitted windows put bars on a pair of sides and the tests map the layout through `TableFit`.
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

        /// The windows of a shape the table is checked in: its design window, and the fitted ones (bars on the sides, or above and below).
        std::vector<MatrixWindow> WindowsOf(TableShape shape) {
            if (shape == TableShape::Narrow) {
                return {DesignWindowOf(shape),
                        MakePhoneWindow(1024.0f, 768.0f, 1.0f),
                        MakePhoneWindow(1500.0f, 1000.0f, 1.0f),
                        MakePhoneWindow(640.0f, 480.0f, 1.0f),
                        MakePhoneWindow(1024.0f, 768.0f, 2.0f),
                        MakePhoneWindow(kCompactWindowWidth, kCompactWindowHeight, 1.0f)};
            }
            return {DesignWindowOf(shape), MakePhoneWindow(2560.0f, 1080.0f, 1.0f), MakePhoneWindow(3200.0f, 900.0f, 1.0f), MakePhoneWindow(915.0f, 330.0f, 2.6f),
                    MakePhoneWindow(851.0f, 393.0f, 2.6f)};
        }

        constexpr TableShape kNewShapes[] = {TableShape::Narrow, TableShape::Wide};

        const char* NameOf(TableShape shape) {
            return shape == TableShape::Narrow ? "narrow" : "wide";
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

    class MahjongTableShapesTest : public MahjongTableTestBase {
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

        /// A click point of the hand tile `index` of the viewer in `window`.
        std::pair<float, float> HandTilePoint(const MatrixWindow& window, int index) const {
            const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat0, index);
            EXPECT_NE(slot, nullptr) << index;
            return slot != nullptr ? OutCentre(window, slot->rect) : std::pair<float, float>{0.0f, 0.0f};
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

    TEST_F(MahjongTableShapesTest, TheViewOfAnOutputIsTheDesignOfItsShapeAndTheLayoutHasTheShape) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        for (const TableShape shape : kNewShapes) {
            for (const MatrixWindow& window : WindowsOf(shape)) {
                SetWindow(*_renderer, window);
                Draw();
                EXPECT_EQ(ShapeOf(window), shape) << NameOf(shape) << " " << window.label.c_str();
                EXPECT_EQ(MahjongScene().GetViewWidth(), DesignWidthOf(shape)) << window.label.c_str();
                EXPECT_EQ(MahjongScene().GetViewHeight(), DesignHeightOf(shape)) << window.label.c_str();
                EXPECT_EQ(_table->GetLayout().shape, shape) << window.label.c_str();
                EXPECT_EQ(_table->GetLayout().viewWidth, DesignWidthOf(shape)) << window.label.c_str();
                EXPECT_EQ(_table->GetLayout().viewHeight, DesignHeightOf(shape)) << window.label.c_str();
            }
        }
        // The landscape view is for what is between: 16:9 and 16:10
        for (const MatrixWindow& window : {DesignWindowOf(TableShape::Landscape), MakePhoneWindow(1920.0f, 1080.0f, 1.0f), MakePhoneWindow(1600.0f, 1000.0f, 1.0f)}) {
            SetWindow(*_renderer, window);
            Draw();
            EXPECT_EQ(_table->GetLayout().shape, TableShape::Landscape) << window.label.c_str();
            EXPECT_EQ(MahjongScene().GetViewWidth(), kViewWidth) << window.label.c_str();
        }
    }

    TEST_F(MahjongTableShapesTest, TheLayoutIsTheLayoutOfTheWindowAndTheNarrowOneIgnoresTheProfile) {
        for (const TableShape shape : kNewShapes) {
            for (const MatrixWindow& window : WindowsOf(shape)) {
                ASSERT_NO_FATAL_FAILURE(StartIn(window));
                const TableLayout& layout = _table->GetLayout();
                EXPECT_TRUE(SameSlots(layout, TableLayoutFor(window, InputOf(Hand())))) << NameOf(shape) << " " << window.label.c_str();
                if (shape == TableShape::Narrow) {
                    EXPECT_EQ(layout.profile, TableProfile{}) << window.label.c_str() << ": the narrow layout ignores the profile";
                    int tiles = 0;
                    for (const TileSlot& slot : layout.slots) {
                        if (slot.role == SlotRole::HandTile && slot.seat == layout.viewer) {
                            ++tiles;
                            EXPECT_EQ(slot.rect.w, kViewerHandTileW) << window.label.c_str();
                        }
                    }
                    EXPECT_EQ(tiles, kDealtTiles + 1) << window.label.c_str() << ": one row of 14";
                } else {
                    EXPECT_EQ(layout.profile.phone, TableProfileOf(window).phone) << window.label.c_str() << ": the wide table has the profile the rule gives";
                }
            }
        }
        // A landscape phone is a wide window with the large hand
        ASSERT_NO_FATAL_FAILURE(StartIn(MakePhoneWindow(915.0f, 330.0f, 2.6f)));
        EXPECT_TRUE(_table->GetLayout().profile.phone);
    }

    TEST_F(MahjongTableShapesTest, EveryTileIsDrawnAtItsSlotAndTheSideSeatsAreTurned) {
        for (const TableShape shape : kNewShapes) {
            // The design window: the view is the output, so the draw call of a slot is the slot
            ASSERT_NO_FATAL_FAILURE(StartIn(DesignWindowOf(shape)));
            ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0)) << NameOf(shape);
            const TableLayout& layout = _table->GetLayout();
            int turnedLeft = 0;
            int turnedRight = 0;
            for (const TileSlot& slot : layout.slots) {
                const bool handOrMeld = slot.role == SlotRole::HandTile || slot.role == SlotRole::MeldTile;
                if (!handOrMeld || slot.seat == layout.viewer || SeatPosition(layout.viewer, slot.seat) == 2) {
                    continue;
                }
                const int position = SeatPosition(layout.viewer, slot.seat);
                EXPECT_EQ(slot.rotation, position == 1 ? 270.0f : 90.0f) << NameOf(shape) << " seat " << slot.seat;
                (position == 1 ? turnedRight : turnedLeft)++;
            }
            EXPECT_EQ(turnedRight, kDealtTiles) << NameOf(shape);
            EXPECT_EQ(turnedLeft, kDealtTiles) << NameOf(shape);
            size_t turns = 0;
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                turns += _renderer->TurnOf(i) != nullptr ? 1 : 0;
            }
            EXPECT_GE(turns, static_cast<size_t>(2 * kDealtTiles)) << NameOf(shape) << ": the side seats' tiles are drawn turned in the record";
        }
    }

    TEST_F(MahjongTableShapesTest, TheFeltAndTheRimFillTheViewInEveryWindow) {
        for (const TableShape shape : kNewShapes) {
            for (const MatrixWindow& window : WindowsOf(shape)) {
                ASSERT_NO_FATAL_FAILURE(StartIn(window));
                const float viewWidth = DesignWidthOf(shape);
                const float viewHeight = DesignHeightOf(shape);
                const Rect view = Out(window, Rect(0.0f, 0.0f, viewWidth, viewHeight));
                bool felt = false;
                for (const DrawCall& call : _renderer->calls) {
                    felt = felt || (call.kind == DrawKind::Texture && call.texture != _atlasTexture && NearRect(call.rect, view));
                }
                EXPECT_TRUE(felt) << NameOf(shape) << " " << window.label.c_str() << ": the felt covers the view";
                // The rim is the four bars of `kRimWidth` units of the shape's design on the border of the view
                float area = 0.0f;
                for (const Rect& rect : RectsOf(kFeltRim)) {
                    area += rect.w * rect.h;
                }
                const float scale = TableFit(window).scale;
                const float inner = (viewWidth - 2.0f * kRimWidth) * (viewHeight - 2.0f * kRimWidth);
                EXPECT_NEAR(area, (viewWidth * viewHeight - inner) * scale * scale, view.w * view.h * 1e-4f) << NameOf(shape) << " " << window.label.c_str();
            }
        }
    }

    TEST_F(MahjongTableShapesTest, AClickOnAHandTileInAFittedWindowDiscardsThatTileWhereTheTileIsBigEnough) {
        // Windows with a hand tile of the touch minimum or more: the regime is the mouse's
        const MatrixWindow windows[] = {MakePhoneWindow(1024.0f, 768.0f, 1.0f),  MakePhoneWindow(1500.0f, 1000.0f, 1.0f), MakePhoneWindow(1024.0f, 768.0f, 2.0f),
                                        MakePhoneWindow(2560.0f, 1080.0f, 1.0f), DesignWindowOf(TableShape::Wide),        DesignWindowOf(TableShape::Narrow)};
        for (const MatrixWindow& window : windows) {
            for (int index = 0; index < kDealtTiles + 1; ++index) {
                ASSERT_NO_FATAL_FAILURE(StartIn(window));
                ASSERT_EQ(_game->GetSession().Actor(), kSeat0);
                const Tile tile = Hand().GetHand(kSeat0).concealed[static_cast<size_t>(index)];
                const auto [x, y] = HandTilePoint(window, index);
                EXPECT_FALSE(Click(x, y)) << "no canvas takes the click: the table does";
                ASSERT_FALSE(Hand().Discards().empty()) << window.label.c_str() << " tile " << index;
                EXPECT_EQ(Hand().Discards().front().tile, tile) << window.label.c_str() << " tile " << index;
            }
        }
    }

    TEST_F(MahjongTableShapesTest, ATapOnAHandTileSelectsItAndASecondOneDiscardsIt) {
        // A touch makes the regime the tap's in every window (`_touchSeen`), the bars on the sides or above and below
        const MatrixWindow windows[] = {MakePhoneWindow(640.0f, 480.0f, 1.0f), MakePhoneWindow(1024.0f, 768.0f, 2.0f), MakePhoneWindow(915.0f, 330.0f, 2.6f), MakePhoneWindow(2560.0f, 1080.0f, 1.0f)};
        for (const MatrixWindow& window : windows) {
            for (int index = 0; index < kDealtTiles + 1; ++index) {
                ASSERT_NO_FATAL_FAILURE(StartIn(window));
                const Tile tile = Hand().GetHand(kSeat0).concealed[static_cast<size_t>(index)];
                const auto [x, y] = HandTilePoint(window, index);
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

    TEST_F(MahjongTableShapesTest, AMouseClickSelectsWhereTheTileIsUnderTheTouchMinimumAndDiscardsWhereItIsNot) {
        const TileSlot* slot = nullptr;
        // Narrow, 640 x 480 at density 1: the tile of 48 units is 28 pt: a click selects. At 1024 x 768 at density 2 it is 45 pt: a click discards.
        const MatrixWindow small = MakePhoneWindow(640.0f, 480.0f, 1.0f);
        ASSERT_NO_FATAL_FAILURE(StartIn(small));
        slot = SlotOf(SlotRole::HandTile, kSeat0, 3);
        ASSERT_NE(slot, nullptr);
        EXPECT_LT(slot->rect.w * TableFit(small).scale / small.pixelDensity, EngineTouchMinimum());
        EXPECT_TRUE(
            UsesTapToSelect(false, small.outputWidth, small.outputHeight, small.pixelDensity, EngineTouchMinimum(), _table->GetLayout().profile.handTileW, kNarrowViewWidth, kNarrowViewHeight));
        {
            const auto [x, y] = OutCentre(small, slot->rect);
            ASSERT_FALSE(Click(x, y));
            Draw();
            EXPECT_EQ(_table->SelectedHandIndex(), 3) << "a mouse click selects where the tile is under the minimum";
            EXPECT_TRUE(Hand().Discards().empty());
        }
        const MatrixWindow tablet = MakePhoneWindow(1024.0f, 768.0f, 2.0f);
        ASSERT_NO_FATAL_FAILURE(StartIn(tablet));
        slot = SlotOf(SlotRole::HandTile, kSeat0, 3);
        ASSERT_NE(slot, nullptr);
        EXPECT_GE(slot->rect.w * TableFit(tablet).scale / tablet.pixelDensity, EngineTouchMinimum());
        const auto [x, y] = OutCentre(tablet, slot->rect);
        ASSERT_FALSE(Click(x, y));
        EXPECT_FALSE(Hand().Discards().empty()) << "a window with a tile of 45 pt: the click discards";

        // Wide: a landscape phone has the large hand, whose tile is the touch minimum or the most the view holds (the rule's), so a click is the rule's too
        const MatrixWindow phone = MakePhoneWindow(915.0f, 330.0f, 2.6f);
        ASSERT_NO_FATAL_FAILURE(StartIn(phone));
        slot = SlotOf(SlotRole::HandTile, kSeat0, 3);
        ASSERT_NE(slot, nullptr);
        const bool selects =
            UsesTapToSelect(false, phone.outputWidth, phone.outputHeight, phone.pixelDensity, EngineTouchMinimum(), _table->GetLayout().profile.handTileW, kWideViewWidth, kViewHeight);
        const auto [px, py] = OutCentre(phone, slot->rect);
        ASSERT_FALSE(Click(px, py));
        Draw();
        EXPECT_EQ(_table->SelectedHandIndex() == 3, selects) << "the click selects exactly where `UsesTapToSelect` says";
        EXPECT_EQ(Hand().Discards().empty(), selects);
    }

    TEST_F(MahjongTableShapesTest, TheSelectedTileRisesOverItsSlotAndKeepsClearOfTheBonusAndMeldRow) {
        // The narrow table (the wide one has the phone profile where a tile is selected by tap: `MahjongTablePhoneTest` holds its raised tile)
        for (const MatrixWindow& window : {MakePhoneWindow(640.0f, 480.0f, 1.0f), MakePhoneWindow(1024.0f, 768.0f, 1.0f)}) {
            ASSERT_NO_FATAL_FAILURE(StartIn(window));
            const int index = 5;
            const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat0, index);
            ASSERT_NE(slot, nullptr);
            const Rect slotRect = slot->rect;
            const auto [x, y] = OutCentre(window, slotRect);
            Tap(x, y);  // a touch: the tap regime is on in every window
            Draw();
            ASSERT_EQ(_table->SelectedHandIndex(), index) << window.label.c_str();
            const Rect raised = RaisedRect(slotRect);
            EXPECT_FLOAT_EQ(raised.y, slotRect.y - kSelectedTileRise);
            const std::vector<DrawCall> calls = TileCalls();
            ASSERT_FALSE(calls.empty());
            EXPECT_TRUE(NearRect(calls.back().rect, Out(window, raised))) << window.label.c_str() << ": the raised tile is drawn last, at its raised rect";

            // In the fullest table of a viewer the raised hand tile covers no tile of the viewer's bonus and meld rows (in the narrow table they lie
            // in a row right above the hand)
            const TableLayout worst = MakeWorstCaseTableLayout(0, window);
            for (const TileSlot& hand : worst.slots) {
                if (hand.role != SlotRole::HandTile || hand.seat != worst.viewer) {
                    continue;
                }
                const Rect up = RaisedRect(hand.rect);
                for (const TileSlot& other : worst.slots) {
                    if (other.seat == worst.viewer && (other.role == SlotRole::BonusTile || other.role == SlotRole::MeldTile)) {
                        EXPECT_FALSE(Intersects(up, other.rect)) << window.label.c_str() << ": hand tile " << hand.index << " raised over a slot of role " << static_cast<int>(other.role);
                    }
                }
            }
        }
    }

    TEST_F(MahjongTableShapesTest, TheMarksLieOnTheScreenRectanglesOfTheSlotsInEveryWindow) {
        for (const TableShape shape : kNewShapes) {
            for (const MatrixWindow& window : WindowsOf(shape)) {
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
                const Rect edge(claimed->rect.x, claimed->rect.y + claimed->rect.h - kClaimedEdgeWidth, claimed->rect.w, kClaimedEdgeWidth);
                EXPECT_EQ(CountDrawn(window, kClaimedEdge, {edge}), 1) << NameOf(shape) << " " << window.label.c_str();

                const Rect hand = BoundsOf(layout, SlotRole::HandTile, kSeat2);
                EXPECT_EQ(CountDrawn(window, kCallingFrame, BarsOf(Padded(hand, kActingMargin), kCallingFrameWidth)), 4) << window.label.c_str() << ": the frame of the hand at the top";
                EXPECT_EQ(RectsOf(kCallingFrame).size(), 4u) << window.label.c_str() << ": only that hand is framed";
            }
        }
    }

    TEST_F(MahjongTableShapesTest, TheChowFrameLiesOnTheTwoTilesTheHoveredChowTakesInEveryWindowWhereActionsIsShown) {
        for (const TableShape shape : kNewShapes) {
            for (const MatrixWindow& window : WindowsOf(shape)) {
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
                ASSERT_EQ(takes.count, 2) << NameOf(shape) << " " << window.label.c_str();
                std::vector<Rect> expected;
                for (int k = 0; k < takes.count; ++k) {
                    const int index = IndexOf(kSeat1, takes.tiles[static_cast<size_t>(k)]);
                    const TileSlot* slot = SlotOf(SlotRole::HandTile, kSeat1, index);
                    ASSERT_NE(slot, nullptr);
                    for (const Rect& bar : BarsOf(slot->rect, kPendingMargin)) {
                        expected.push_back(bar);
                    }
                }
                EXPECT_EQ(CountDrawn(window, kPendingFrame, expected), static_cast<int>(expected.size())) << NameOf(shape) << " " << window.label.c_str();
            }
        }
    }

    TEST_F(MahjongTableShapesTest, TheActionCanvasesFollowTheCompactRuleOfTheViewOfTheWindow) {
        size_t compact = 0;
        size_t plain = 0;
        for (const TableShape shape : kNewShapes) {
            for (const MatrixWindow& window : WindowsOf(shape)) {
                ASSERT_NO_FATAL_FAILURE(StartWith(window, kPersonAtSeat1, ClaimWindowModel()));
                const bool rule = IsCompactWindow(window);
                EXPECT_EQ(Shown("Actions"), !rule) << NameOf(shape) << " " << window.label.c_str();
                EXPECT_EQ(Shown("HintsButton"), !rule) << window.label.c_str() << ": the button of the hints goes with `Actions`";
                EXPECT_EQ(Shown("ActionsCompact"), rule) << window.label.c_str();
                (rule ? compact : plain)++;
            }
        }
        EXPECT_EQ(compact, 1u) << "only the nearly square narrow window shows the compact row";
        EXPECT_GT(plain, 0u);
        EXPECT_TRUE(IsCompactWindow(MakePhoneWindow(kCompactWindowWidth, kCompactWindowHeight, 1.0f)));
        EXPECT_FALSE(IsCompactWindow(MakePhoneWindow(640.0f, 480.0f, 1.0f))) << "the narrow table's buttons are 33 px there";
    }

    TEST_F(MahjongTableShapesTest, AResizeAcrossTheShapesStartsTheTilesWithNoFlightAndKeepsThemInsideTheView) {
        UseFlights();
        struct Change {
            MatrixWindow from;
            MatrixWindow to;
        };
        const Change changes[] = {{DesignWindowOf(TableShape::Landscape), DesignWindowOf(TableShape::Narrow)},
                                  {DesignWindowOf(TableShape::Narrow), DesignWindowOf(TableShape::Landscape)},
                                  {DesignWindowOf(TableShape::Landscape), DesignWindowOf(TableShape::Wide)},
                                  {DesignWindowOf(TableShape::Wide), DesignWindowOf(TableShape::Portrait)}};
        for (const Change& change : changes) {
            // Control: a discard between two draws in the same window flies
            ASSERT_NO_FATAL_FAILURE(StartIn(change.from));
            const auto [x, y] = HandTilePoint(change.from, 0);
            EXPECT_FALSE(Click(x, y));
            Draw();
            EXPECT_GT(_table->FlightCount(), 0u) << change.from.label.c_str() << ": a discard makes a flight";

            // The same discard and the window changes its shape before the next draw: the tiles are in place in the other table
            ASSERT_NO_FATAL_FAILURE(StartIn(change.from));
            const auto [x2, y2] = HandTilePoint(change.from, 0);
            EXPECT_FALSE(Click(x2, y2));
            SetWindow(*_renderer, change.to);
            Draw();
            EXPECT_EQ(_table->GetLayout().shape, ShapeOf(change.to)) << change.to.label.c_str();
            EXPECT_EQ(_table->FlightCount(), 0u) << change.from.label.c_str() << " to " << change.to.label.c_str() << ": another shape: no flight crosses it";
            ExpectEveryTileInsideTheOutput(change.to, "after the change");
        }
    }

    /// Three steps of the computers in `window` (the clock of the table has run 0.3 s after each; a fourth would make the component step on
    /// its own), every tile in the air inside the output: seat 1, on the right, draws and discards in the second.
    void MahjongTableShapesTest::FlyThreeSteps(const MatrixWindow& window) {
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

    // One window to a test: a second match in the same fixture keeps the flights of the first
    TEST_F(MahjongTableShapesTest, TheTilesOfTheNarrowTableFlyInsideTheView) {
        FlyThreeSteps(DesignWindowOf(TableShape::Narrow));
    }

    TEST_F(MahjongTableShapesTest, TheTilesOfTheNarrowTableFlyInsideAFittedWindowToo) {
        FlyThreeSteps(MakePhoneWindow(1024.0f, 768.0f, 1.0f));
    }

    TEST_F(MahjongTableShapesTest, TheTilesOfTheWideTableFlyInsideTheView) {
        FlyThreeSteps(DesignWindowOf(TableShape::Wide));
    }

    TEST_F(MahjongTableShapesTest, TheTilesOfTheWideTableFlyInsideAFittedWindowToo) {
        FlyThreeSteps(MakePhoneWindow(2560.0f, 1080.0f, 1.0f));
    }

    TEST_F(MahjongTableShapesTest, TheTilesOfTheWideTableFlyInsideALandscapePhone) {
        FlyThreeSteps(MakePhoneWindow(915.0f, 330.0f, 2.6f));
    }

}  // namespace BECore::Mahjong::Tests
