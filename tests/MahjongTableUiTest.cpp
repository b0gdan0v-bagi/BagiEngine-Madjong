#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Table/TableLook.h>
#include <Mahjong/Table/UiPalette.h>
#include <Mahjong/Table/WindMarks.h>

/**
 * The table as drawn (`Components/MahjongTableComponent.h`): the shipped scene (`config/SceneConfig.xml` and its node files) is drawn into a `RecordingRenderer` and
 * the draw calls are read back (the fixture is `MahjongTableTestSupport.h`). Needs the engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seeds tried while looking for a table with a meld and for one where seat 0 deals.
        constexpr uint32_t kSeedsToTry = 40;
        /// Steps after which the live wall has certainly shrunk by a few stacks.
        constexpr int kStepsForTheWall = 40;
        /// East's hand and seats 1 and 3 holding a pair of 5p that a pung of East's 5p completes (a claimed tile on the side seats).
        constexpr const char* kEastPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// Four winds, a dragon and plain tiles: 13 tiles.
        constexpr const char* kAllWindsHand = "Ew Sw Ww Nw Gd 1p 2p 3p 4s 5s 6s 7m 8m";
        constexpr const char* kPairOfFives = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
        /// The turn of the tiles of a seat at screen position `position`, worked out here and not taken from the layout.
        constexpr float TurnAt(int position) {
            return position == 1 ? 270.0f : (position == 3 ? 90.0f : 0.0f);
        }

    }  // namespace

    class MahjongTableUiTest : public MahjongTableTestBase {
    protected:
        /// The rectangles drawn in exactly this colour, in the order drawn.
        std::vector<DrawCall> RectsOf(const Color& color) const {
            std::vector<DrawCall> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call);
                }
            }
            return found;
        }

        /// A hand of four computers played until it has an open set, a bonus tile and discards (a copy of the model: any viewer can lay it).
        HandModel TableWithMeldBonusAndDiscards() {
            for (uint32_t seed = 1; seed <= kSeedsToTry; ++seed) {
                Start(kAllComputers, seed, true);
                const bool found = StepUntil([&] {
                    int melds = 0;
                    int bonus = 0;
                    for (Seat seat = 0; seat < kSeatCount; ++seat) {
                        melds += static_cast<int>(Hand().GetHand(seat).melds.size());
                        bonus += static_cast<int>(Hand().GetHand(seat).bonus.size());
                    }
                    return melds > 0 && bonus > 0 && Hand().Discards().size() > 2;
                });
                if (found) {
                    return Hand();
                }
            }
            ADD_FAILURE() << "a table with an open set and a bonus tile";
            return Hand();
        }

        /// Seat `claimer` pungs the 5p that East (seat 0) discards; the person at seat 0 is the viewer.
        HandModel ClaimedPung(Seat claimer) {
            FixedWallSpec spec = SpecOf(kEastPlainHand, "5p");
            spec.hands[claimer] = kPairOfFives;
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_TRUE(model.Claim(claimer, ClaimKind::Pung).has_value());
            return model;
        }

        /// The viewer (seat 0) holds all four winds, a dragon and plain tiles (13, and `9s` drawn by the dealer); the others hold quiet hands.
        HandModel TableWithAllWinds(Seat east, Wind roundWind) {
            FixedWallSpec spec = SpecOf(kAllWindsHand, "9s");
            return MakeModel(spec, east, roundWind);
        }

        /// What the bars of `kSeatWindMark` and `kRoundWindMark` along the top edge of `slot` say in the last draw: one bar over the whole width
        /// is `Seat` or `Round`, the seat bar over the left half and the round bar over the right half is `Both`.
        WindMark MarkDrawnOn(const TileSlot& slot) const {
            const float bar = kWindMarkBarHeight * LayoutScale(kViewWidth, kViewHeight);
            const auto covers = [&](const Color& color, float x0, float x1) {
                for (const DrawCall& call : RectsOf(color)) {
                    if (call.rect.y == slot.rect.y && call.rect.h == bar && call.rect.x == x0 && call.rect.x + call.rect.w == x1) {
                        return true;
                    }
                }
                return false;
            };
            const float left = slot.rect.x;
            const float right = slot.rect.x + slot.rect.w;
            const float middle = slot.rect.x + slot.rect.w * 0.5f;
            if (covers(kSeatWindMark, left, middle) && covers(kRoundWindMark, middle, right)) {
                return WindMark::Both;
            }
            if (covers(kSeatWindMark, left, right)) {
                return WindMark::Seat;
            }
            return covers(kRoundWindMark, left, right) ? WindMark::Round : WindMark::None;
        }

        static int NotWallStacks(const TableLayout& layout) {
            int tiles = 0;
            for (const TileSlot& slot : layout.slots) {
                tiles += slot.role == SlotRole::WallStack ? 0 : 1;
            }
            return tiles;
        }
    };

    TEST_F(MahjongTableUiTest, TheSceneViewIsTheDesignSizeOfTheLayout) {
        const Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
        ASSERT_NE(scene, nullptr);
        EXPECT_FLOAT_EQ(scene->GetViewWidth(), kViewWidth);
        EXPECT_FLOAT_EQ(scene->GetViewHeight(), kViewHeight);
    }

    /// A renderer that gives no texture of its own (a backend that fails): the felt is the flat colour then.
    class RefusingRenderer : public TurnRecordingRenderer {
    public:
        IntrusivePtr<ITexture> CreateTexture(const uint8_t*, uint32_t, uint32_t) override {
            return {};
        }
    };

    TEST_F(MahjongTableUiTest, WithoutAnActiveGameOnlyTheFeltIsDrawn) {
        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(MahjongGame::GetActive(), nullptr);
        _manager->SetActiveScene("MahjongScene"_intern);

        Draw();

        // The table draws the felt and nothing else (the canvases of the scene draw their own, with no match behind them)
        ASSERT_FALSE(_renderer->calls.empty());
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Texture);
        EXPECT_TRUE(TileCalls().empty());
        EXPECT_TRUE(_table->GetLayout().slots.empty());
        // ... and the rim round it with its lip: no plate, no shadow
        EXPECT_EQ(RectsOf(kFeltRim).size(), 4u);
        EXPECT_EQ(RectsOf(kFeltLip).size(), 4u);
        EXPECT_TRUE(RectsOf(kTileShadow).empty());
    }

    TEST_F(MahjongTableUiTest, TheFeltIsOneTextureOverTheViewMadeOnceAndNotTheAtlas) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const DrawCall& felt = _renderer->calls[0];
        ASSERT_EQ(felt.kind, DrawKind::Texture);
        EXPECT_NE(felt.texture, _atlasTexture);
        EXPECT_FALSE(felt.source.has_value()) << "the whole texture";
        EXPECT_FLOAT_EQ(felt.rect.x, 0.0f);
        EXPECT_FLOAT_EQ(felt.rect.y, 0.0f);
        EXPECT_FLOAT_EQ(felt.rect.w, kViewWidth);
        EXPECT_FLOAT_EQ(felt.rect.h, kViewHeight);
        EXPECT_TRUE(RectsOf(kFelt).empty());
        EXPECT_FLOAT_EQ(felt.texture->GetWidth(), static_cast<float>(kFeltTextureWidth));
        EXPECT_FLOAT_EQ(felt.texture->GetHeight(), static_cast<float>(kFeltTextureHeight));

        const ITexture* first = felt.texture;
        Draw();
        ASSERT_EQ(_renderer->calls[0].kind, DrawKind::Texture);
        EXPECT_EQ(_renderer->calls[0].texture, first) << "made once, not per frame";
    }

    TEST_F(MahjongTableUiTest, WithoutATextureTheFeltIsTheFlatColourFirstAndIsNotAskedForAgain) {
        auto refusing = New<RefusingRenderer>();
        refusing->width = kViewWidth;
        refusing->height = kViewHeight;
        _renderer = refusing;
        CoreManager::GetRendererManager().SetRenderer(_renderer);
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));

        Draw();
        Draw();

        ASSERT_FALSE(_renderer->calls.empty());
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);
        EXPECT_TRUE(SameColor(_renderer->calls[0].color, kFelt));
        EXPECT_FLOAT_EQ(_renderer->calls[0].rect.w, kViewWidth);
        EXPECT_FLOAT_EQ(_renderer->calls[0].rect.h, kViewHeight);
        for (const DrawCall& call : _renderer->calls) {
            EXPECT_FALSE(call.kind == DrawKind::Texture && call.rect.w == kViewWidth && call.rect.h == kViewHeight) << "no picture of the felt";
        }
    }

    TEST_F(MahjongTableUiTest, AnotherRendererGetsAFeltOfItsOwn) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        ASSERT_EQ(_renderer->calls[0].kind, DrawKind::Texture);

        auto second = New<TurnRecordingRenderer>();
        second->width = kViewWidth;
        second->height = kViewHeight;
        _renderer = second;
        CoreManager::GetRendererManager().SetRenderer(_renderer);
        Draw();

        ASSERT_EQ(_renderer->calls[0].kind, DrawKind::Texture) << "a felt made by this renderer";
        EXPECT_NE(_renderer->calls[0].texture, _atlasTexture);
        EXPECT_FLOAT_EQ(_renderer->calls[0].texture->GetWidth(), static_cast<float>(kFeltTextureWidth));
    }

    TEST_F(MahjongTableUiTest, TheRimIsFourRectanglesAlongTheEdgeRightAfterTheFelt) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        ASSERT_GE(_renderer->calls.size(), 9u);
        const std::vector<DrawCall> rim = RectsOf(kFeltRim);
        ASSERT_EQ(rim.size(), 4u);
        for (size_t i = 0; i < rim.size(); ++i) {
            EXPECT_EQ(rim[i].rect.x, _renderer->calls[1 + i].rect.x) << "the rim is drawn right after the felt";
            EXPECT_EQ(rim[i].rect.y, _renderer->calls[1 + i].rect.y);
        }
        float area = 0.0f;
        for (const DrawCall& side : rim) {
            area += side.rect.w * side.rect.h;
            EXPECT_TRUE(side.rect.w == 8.0f || side.rect.h == 8.0f) << "a rim side is 8 units wide";
        }
        const float inner = (kViewWidth - 16.0f) * (kViewHeight - 16.0f);
        EXPECT_FLOAT_EQ(area, kViewWidth * kViewHeight - inner);
    }

    TEST_F(MahjongTableUiTest, TheLipIsFourBarsOf2UnitsOnTheInnerSideOfTheRimRightAfterIt) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const std::vector<DrawCall> lip = RectsOf(kFeltLip);
        ASSERT_EQ(lip.size(), 4u);
        float area = 0.0f;
        for (size_t i = 0; i < lip.size(); ++i) {
            EXPECT_EQ(lip[i].rect.x, _renderer->calls[5 + i].rect.x) << "the lip is drawn right after the rim";
            EXPECT_EQ(lip[i].rect.y, _renderer->calls[5 + i].rect.y);
            EXPECT_TRUE(lip[i].rect.w == 2.0f || lip[i].rect.h == 2.0f) << "a lip bar is 2 units wide";
            EXPECT_GE(lip[i].rect.x, 8.0f) << "inside the rim";
            EXPECT_GE(lip[i].rect.y, 8.0f);
            EXPECT_LE(lip[i].rect.x + lip[i].rect.w, kViewWidth - 8.0f);
            EXPECT_LE(lip[i].rect.y + lip[i].rect.h, kViewHeight - 8.0f);
            area += lip[i].rect.w * lip[i].rect.h;
        }
        const float inner = (kViewWidth - 16.0f) * (kViewHeight - 16.0f);
        const float innerOfLip = (kViewWidth - 20.0f) * (kViewHeight - 20.0f);
        EXPECT_FLOAT_EQ(area, inner - innerOfLip) << "the bars meet at the corners and do not overlap";
    }

    TEST_F(MahjongTableUiTest, NoBoxIsPaintedUnderTheDiscardBlocksOrTheWall) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        // After the felt (1), the rim (4) and the lip (4) comes the ground of the centre panel, then only its marks up to the first seat plate
        const TableLayout& layout = _table->GetLayout();
        ASSERT_GE(_renderer->calls.size(), 10u);
        EXPECT_EQ(_renderer->calls[9].kind, DrawKind::Rect);
        EXPECT_TRUE(SameColor(_renderer->calls[9].color, kSeatPlate));
        EXPECT_TRUE(SameRect(_renderer->calls[9].rect, layout.zones.centerPanel));
        size_t next = 10;
        while (next < _renderer->calls.size() && (SameColor(_renderer->calls[next].color, kPanelTurnLight) || SameColor(_renderer->calls[next].color, kPanelDealerMark))) {
            ++next;
        }
        ASSERT_LT(next, _renderer->calls.size());
        EXPECT_TRUE(SameColor(_renderer->calls[next].color, kSeatPlate));
        EXPECT_TRUE(SameRect(_renderer->calls[next].rect, layout.zones.seatPlates[0]));
    }

    TEST_F(MahjongTableUiTest, EveryTileThatIsNotAWallStackHasAShadowOfTwoLayersOffsetBy2) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const TableLayout& layout = _table->GetLayout();
        const std::vector<DrawCall> tight = RectsOf(kTileShadow);
        const std::vector<DrawCall> soft = RectsOf(kTileShadowSoft);
        ASSERT_EQ(static_cast<int>(tight.size()), NotWallStacks(layout));
        ASSERT_EQ(soft.size(), tight.size()) << "one pair per tile";
        // Every tight layer comes right after its soft one
        size_t pairs = 0;
        for (size_t i = 0; i + 1 < _renderer->calls.size(); ++i) {
            if (_renderer->calls[i].kind == DrawKind::Rect && SameColor(_renderer->calls[i].color, kTileShadowSoft)) {
                EXPECT_TRUE(SameColor(_renderer->calls[i + 1].color, kTileShadow)) << "call " << i;
                ++pairs;
            }
        }
        EXPECT_EQ(pairs, tight.size());
        size_t next = 0;
        for (const TileSlot& slot : layout.slots) {
            if (slot.role == SlotRole::WallStack) {
                continue;
            }
            EXPECT_EQ(tight[next].rect.x, slot.rect.x + 2.0f);
            EXPECT_EQ(tight[next].rect.y, slot.rect.y + 2.0f);
            EXPECT_EQ(tight[next].rect.w, slot.rect.w);
            EXPECT_EQ(tight[next].rect.h, slot.rect.h);
            // The soft layer reaches past the tight one on every side and is drawn before it
            EXPECT_LT(soft[next].rect.x, tight[next].rect.x);
            EXPECT_LT(soft[next].rect.y, tight[next].rect.y);
            EXPECT_GT(soft[next].rect.x + soft[next].rect.w, tight[next].rect.x + tight[next].rect.w);
            EXPECT_GT(soft[next].rect.y + soft[next].rect.h, tight[next].rect.y + tight[next].rect.h);
            EXPECT_LT(kTileShadowSoft.a, kTileShadow.a) << "the wider layer is the fainter";
            ++next;
        }
    }

    TEST_F(MahjongTableUiTest, TheWallIsDrawnFromTheStackCellAndHasNoShadow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const TableLayout& layout = _table->GetLayout();
        const std::vector<DrawCall> calls = TileCalls();
        ASSERT_EQ(calls.size(), layout.slots.size());
        int stacks = 0;
        for (size_t i = 0; i < calls.size(); ++i) {
            if (layout.slots[i].role != SlotRole::WallStack) {
                continue;
            }
            ++stacks;
            ASSERT_TRUE(calls[i].source.has_value());
            EXPECT_TRUE(SameRect(*calls[i].source, StackRect())) << "stack " << stacks;
            EXPECT_TRUE(SameRect(calls[i].rect, layout.slots[i].rect));
        }
        EXPECT_GT(stacks, 0);
        EXPECT_EQ(static_cast<int>(RectsOf(kTileShadow).size()), NotWallStacks(layout)) << "no shadow under a stack";
    }

    TEST_F(MahjongTableUiTest, TheViewerSeesItsOwnHandFaceUpAndTheOthersFaceDown) {
        bool sawEastAtSeat0 = false;
        for (uint32_t seed = 1; seed <= kSeedsToTry; ++seed) {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup, seed));
            Draw();

            const PlayerHand& own = Hand().GetHand(0);
            ASSERT_EQ(_table->GetLayout().viewer, 0);
            EXPECT_EQ(own.concealed.size(), Hand().East() == 0 ? kDealtTiles + 1u : static_cast<size_t>(kDealtTiles)) << "seed " << seed;
            sawEastAtSeat0 = sawEastAtSeat0 || Hand().East() == 0;
            ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, 0)) << "seed " << seed;
            EXPECT_EQ(FaceUpHandTiles(0), static_cast<int>(own.concealed.size())) << "seed " << seed;
            for (Seat seat = 1; seat < kSeatCount; ++seat) {
                EXPECT_EQ(FaceUpHandTiles(seat), 0) << "no face of seat " << seat << ", seed " << seed;
            }
        }
        EXPECT_TRUE(sawEastAtSeat0) << "one of the seeds deals 14 tiles to the viewer";
    }

    TEST_F(MahjongTableUiTest, TheViewerSeatSitsAtTheBottomWhicheverSeatItIs) {
        SeatSetup seats = kAllComputers;
        seats[2] = SeatKind::Person;
        ASSERT_NO_FATAL_FAILURE(Start(seats));

        Draw();

        EXPECT_EQ(_table->GetLayout().viewer, 2);
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, 2));
        EXPECT_EQ(FaceUpHandTiles(2), static_cast<int>(Hand().GetHand(2).concealed.size()));
    }

    TEST_F(MahjongTableUiTest, ASpectatorSeesEveryConcealedTileWhileThePlayIsOn) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_TRUE(StepUntil([&] { return !Hand().Discards().empty(); }));

        Draw();

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(true, kNoSeat));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(FaceUpHandTiles(seat), static_cast<int>(Hand().GetHand(seat).concealed.size())) << "seat " << seat;
        }
    }

    TEST_F(MahjongTableUiTest, WhileTheScreenIsCoveredNoConcealedTileIsFaceUp) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        TableSession& session = _game->GetSession();
        session.RunComputers();
        ASSERT_TRUE(session.HandoffPending());

        Draw();

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kNoSeat));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(FaceUpHandTiles(seat), 0) << "seat " << seat;
        }

        // The person it waits for presses "Show": that hand, and only that one, is face up
        const Seat actor = session.Actor();
        session.Reveal();
        Draw();

        ASSERT_FALSE(session.HandoffPending());
        EXPECT_EQ(_table->GetLayout().viewer, actor);
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, actor));
        EXPECT_EQ(FaceUpHandTiles(actor), static_cast<int>(Hand().GetHand(actor).concealed.size()));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat != actor) {
                EXPECT_EQ(FaceUpHandTiles(seat), 0) << "seat " << seat;
            }
        }
    }

    TEST_F(MahjongTableUiTest, AllFourHandsAreFaceUpWhenTheHandIsOver) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->GetSession().RunComputers();
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);

        Draw();

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(true, kNoSeat));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(FaceUpHandTiles(seat), static_cast<int>(Hand().GetHand(seat).concealed.size())) << "seat " << seat;
        }
    }

    TEST_F(MahjongTableUiTest, ThePersonSeesEveryHandAfterTheHandIsOverToo) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        TableSession& session = _game->GetSession();
        // The person always discards its first tile; the computers play on until the hand ends
        for (int step = 0; step < kActionLimit && session.GetPhase() == SessionPhase::Playing; ++step) {
            session.RunComputers();
            if (session.GetPhase() != SessionPhase::Playing) {
                break;
            }
            const auto legal = Hand().LegalActions(session.Actor());
            ASSERT_FALSE(legal.empty());
            ASSERT_TRUE(session.PerformPerson(legal.back()).has_value());
        }
        ASSERT_EQ(session.GetPhase(), SessionPhase::HandOver);

        Draw();

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(true, kNoSeat));
    }

    TEST_F(MahjongTableUiTest, MeldsBonusTilesAndDiscardsAreDrawnWhereTheLayoutSaysAndTheWallFollowsTheDraws) {
        bool found = false;
        for (uint32_t seed = 1; seed <= kSeedsToTry && !found; ++seed) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers, seed, true));
            found = StepUntil([&] {
                int melds = 0;
                int bonus = 0;
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    melds += static_cast<int>(Hand().GetHand(seat).melds.size());
                    bonus += static_cast<int>(Hand().GetHand(seat).bonus.size());
                }
                return melds > 0 && bonus > 0 && Hand().Discards().size() > 2;
            });
        }
        ASSERT_TRUE(found) << "a table with an open set and a bonus tile";

        Draw();

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(true, kNoSeat));
        TableInput input;
        input.viewer = 0;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            input.concealedCount[seat] = static_cast<int>(Hand().GetHand(seat).concealed.size());
            input.melds[seat] = Hand().GetHand(seat).melds;
            input.bonusCount[seat] = static_cast<int>(Hand().GetHand(seat).bonus.size());
        }
        for (const DiscardEntry& entry : Hand().Discards()) {
            input.discardSeats.push_back(entry.seat);
        }
        input.wallRemaining = static_cast<int>(Hand().GetWall().Remaining());
        const TableLayout expected = MakeTableLayout(input);
        const TableLayout& drawn = _table->GetLayout();
        ASSERT_EQ(drawn.slots.size(), expected.slots.size());
        std::array<int, 5> roles{};
        for (size_t i = 0; i < expected.slots.size(); ++i) {
            EXPECT_EQ(drawn.slots[i].role, expected.slots[i].role) << i;
            EXPECT_EQ(drawn.slots[i].rect.x, expected.slots[i].rect.x) << i;
            EXPECT_EQ(drawn.slots[i].rect.y, expected.slots[i].rect.y) << i;
            ++roles[static_cast<size_t>(drawn.slots[i].role)];
        }
        EXPECT_GT(roles[static_cast<size_t>(SlotRole::MeldTile)], 0);
        EXPECT_GT(roles[static_cast<size_t>(SlotRole::BonusTile)], 0);
        EXPECT_EQ(roles[static_cast<size_t>(SlotRole::DiscardTile)], static_cast<int>(Hand().Discards().size()));
        EXPECT_EQ(roles[static_cast<size_t>(SlotRole::WallStack)], WallStackCount(static_cast<int>(Hand().GetWall().Remaining())));

        // More draws, fewer stacks
        const int stacksBefore = roles[static_cast<size_t>(SlotRole::WallStack)];
        const int remainingBefore = static_cast<int>(Hand().GetWall().Remaining());
        for (int step = 0; step < kStepsForTheWall && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        }
        Draw();
        int stacksAfter = 0;
        for (const TileSlot& slot : _table->GetLayout().slots) {
            stacksAfter += slot.role == SlotRole::WallStack ? 1 : 0;
        }
        ASSERT_LT(static_cast<int>(Hand().GetWall().Remaining()), remainingBefore);
        EXPECT_EQ(stacksAfter, WallStackCount(static_cast<int>(Hand().GetWall().Remaining())));
        EXPECT_LE(stacksAfter, stacksBefore);
    }

    TEST_F(MahjongTableUiTest, WithoutAnAtlasTheTilesAreRectanglesAndTheLayoutIsTheSame) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        const size_t slots = _table->GetLayout().slots.size();
        ASSERT_GT(slots, 0u);

        _table->SetAtlasTexture({});
        Draw();

        EXPECT_TRUE(TileCalls().empty());
        EXPECT_EQ(_table->GetLayout().slots.size(), slots);
        EXPECT_GE(_renderer->calls.size(), slots) << "a rectangle for every tile";
    }

    TEST_F(MahjongTableUiTest, TheTilesOfTheSideSeatsAreTurnedTowardsTheTableForEveryViewerAndNoOtherTileIs) {
        const HandModel table = TableWithMeldBonusAndDiscards();
        for (const Seat viewer : {kSeat0, kSeat2}) {
            SeatSetup seats = kAllComputers;
            seats[viewer] = SeatKind::Person;
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(table);
            Draw();

            const TableLayout& layout = _table->GetLayout();
            ASSERT_EQ(layout.viewer, viewer);
            size_t sideSlots = 0;
            for (const TileSlot& slot : layout.slots) {
                const int position = slot.role == SlotRole::WallStack ? 0 : SeatPosition(viewer, slot.seat);
                EXPECT_EQ(slot.rotation, TurnAt(position)) << "viewer " << viewer << " seat " << slot.seat << " role " << static_cast<int>(slot.role);
                sideSlots += slot.rotation != 0.0f ? 1 : 0;
                if (slot.rotation != 0.0f) {
                    EXPECT_GT(slot.rect.w, slot.rect.h) << "a turned tile lies on its side";
                }
            }
            ASSERT_GT(sideSlots, 0u);
            ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, viewer));
            // Every turned draw is a tile of a side seat: the viewer's and the top seat's tiles are plain texture calls (`ExpectTableDrawn`)
            EXPECT_EQ(_renderer->turned.size(), sideSlots) << "viewer " << viewer;
            for (const TurnedDraw& draw : _renderer->turned) {
                EXPECT_EQ(_renderer->calls[draw.call].kind, DrawKind::Texture);
                EXPECT_TRUE(draw.transform.rotation == 90.0f || draw.transform.rotation == 270.0f);
            }
        }
    }

    TEST_F(MahjongTableUiTest, WithoutAnAtlasTheSideSeatsFacesAreTurnedRectanglesOverTheSameScreenRectangle) {
        const HandModel table = TableWithMeldBonusAndDiscards();
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        Lay(table);
        _table->SetAtlasTexture({});
        Draw();

        const TableLayout& layout = _table->GetLayout();
        EXPECT_TRUE(TileCalls().empty());
        size_t sideSlots = 0;
        for (const TileSlot& slot : layout.slots) {
            if (slot.rotation == 0.0f) {
                continue;
            }
            ++sideSlots;
            const Rect upright = UprightRect(slot.rect, slot.rotation);
            // The face: a turned rectangle with the unturned rectangle of the slot, about the centre of the slot, over the slot's screen rectangle
            bool found = false;
            for (const TurnedDraw& draw : _renderer->turned) {
                const DrawCall& call = _renderer->calls[draw.call];
                if (draw.upright.x == upright.x && draw.upright.y == upright.y && draw.upright.w == upright.w && draw.upright.h == upright.h && draw.transform.rotation == slot.rotation) {
                    EXPECT_EQ(call.kind, DrawKind::Rect);
                    EXPECT_EQ(draw.transform.pivotX, RectCentreX(slot.rect));
                    EXPECT_EQ(draw.transform.pivotY, RectCentreY(slot.rect));
                    EXPECT_EQ(call.rect.x, slot.rect.x);
                    EXPECT_EQ(call.rect.y, slot.rect.y);
                    EXPECT_EQ(call.rect.w, slot.rect.w);
                    EXPECT_EQ(call.rect.h, slot.rect.h);
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found) << "seat " << slot.seat << " role " << static_cast<int>(slot.role) << " index " << slot.index;
        }
        ASSERT_GT(sideSlots, 0u);
        // A turned face is never drawn upright, and a turned draw is of a side seat only
        for (const TurnedDraw& draw : _renderer->turned) {
            EXPECT_TRUE(draw.transform.rotation == 90.0f || draw.transform.rotation == 270.0f);
            EXPECT_EQ(_renderer->calls[draw.call].kind, DrawKind::Rect);
        }
    }

    TEST_F(MahjongTableUiTest, TheClaimedEdgeBarOfASideSeatSetLiesInsideTheScreenRectangleOnTheSideOfTheClaimEdge) {
        for (const Seat claimer : {kSeat1, kSeat3}) {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            Lay(ClaimedPung(claimer));
            Draw();

            const TileSlot* claimed = nullptr;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.claimEdge != Edge::None) {
                    ASSERT_EQ(claimed, nullptr) << "one claimed tile";
                    claimed = &slot;
                }
            }
            ASSERT_NE(claimed, nullptr) << "seat " << claimer;
            ASSERT_EQ(claimed->seat, claimer);
            ASSERT_NE(claimed->rotation, 0.0f);
            EXPECT_EQ(claimed->claimEdge, Edge::Bottom) << "seat 0, where the claim came from, is at the bottom of the screen";
            const std::vector<DrawCall> bars = RectsOf(kClaimedEdge);
            ASSERT_EQ(bars.size(), 1u) << "seat " << claimer;
            const Rect& bar = bars[0].rect;
            const Rect& tile = claimed->rect;
            EXPECT_GE(bar.x, tile.x);
            EXPECT_LE(bar.x + bar.w, tile.x + tile.w);
            EXPECT_GE(bar.y, tile.y);
            EXPECT_EQ(bar.y + bar.h, tile.y + tile.h) << "flush with the bottom edge of the screen rectangle";
            EXPECT_EQ(bar.w, tile.w) << "the whole width of the lying tile";
            EXPECT_LT(bar.h, tile.h);
        }
    }

    TEST_F(MahjongTableUiTest, TheOwnWindAndTheRoundWindOfTheViewerAreMarkedOnItsHandTilesAndNoOtherTile) {
        struct Case {
            const char* name;
            Seat east;
            Wind roundWind;
            WindMark onEastTile, onSouthTile, onWestTile, onNorthTile;
        };
        // Seat 0 is East when it deals and South when seat 3 does; the round is East or South
        const Case cases[] = {
            {"East in the East round", kSeat0, Wind::East, WindMark::Both, WindMark::None, WindMark::None, WindMark::None},
            {"East in the South round", kSeat0, Wind::South, WindMark::Seat, WindMark::Round, WindMark::None, WindMark::None},
            {"South in the East round", kSeat3, Wind::East, WindMark::Round, WindMark::Seat, WindMark::None, WindMark::None},
            {"South in the South round", kSeat3, Wind::South, WindMark::None, WindMark::Both, WindMark::None, WindMark::None},
            {"North in the West round", kSeat1, Wind::West, WindMark::None, WindMark::None, WindMark::Round, WindMark::Seat},
        };
        for (const Case& test : cases) {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            Lay(TableWithAllWinds(test.east, test.roundWind));
            Draw();

            const Seat viewer = _table->GetLayout().viewer;
            ASSERT_EQ(viewer, kSeat0) << test.name;
            ASSERT_EQ(Hand().SeatWind(viewer), MakeSeatWinds(test.east)[viewer]) << test.name;
            const PlayerHand& own = Hand().GetHand(viewer);
            int rects = 0;
            int hand = 0;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role != SlotRole::HandTile || slot.seat != viewer) {
                    continue;
                }
                ++hand;
                const Tile tile = own.concealed[static_cast<size_t>(slot.index)];
                const WindMark want = tile == T("Ew") ? test.onEastTile : tile == T("Sw") ? test.onSouthTile : tile == T("Ww") ? test.onWestTile : tile == T("Nw") ? test.onNorthTile : WindMark::None;
                EXPECT_EQ(MarkDrawnOn(slot), want) << test.name << ", " << ToString(tile);
                rects += want == WindMark::None ? 0 : (want == WindMark::Both ? 2 : 1);
            }
            EXPECT_EQ(hand, static_cast<int>(own.concealed.size())) << test.name;
            // A tile that is both has two rectangles, the others one each, and nothing else is drawn in these colours
            EXPECT_EQ(static_cast<int>(RectsOf(kSeatWindMark).size() + RectsOf(kRoundWindMark).size()), rects) << test.name;
        }
    }

    TEST_F(MahjongTableUiTest, NoWindIsMarkedForASpectatorOrUnderTheCover) {
        const HandModel model = TableWithAllWinds(kSeat0, Wind::South);
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Lay(model);
        Draw();
        EXPECT_TRUE(RectsOf(kSeatWindMark).empty());
        EXPECT_TRUE(RectsOf(kRoundWindMark).empty());

        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Lay(model);
        _game->GetSession().RunComputers();
        ASSERT_TRUE(_game->GetSession().HandoffPending());
        Draw();
        EXPECT_TRUE(RectsOf(kSeatWindMark).empty());
        EXPECT_TRUE(RectsOf(kRoundWindMark).empty());

        _game->GetSession().Reveal();
        Draw();
        EXPECT_FALSE(RectsOf(kSeatWindMark).empty()) << "the person who pressed Show sees the marks of its hand";
    }

    TEST_F(MahjongTableUiTest, DrawingChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
        const std::string hand = Snapshot(Hand());
        const uint64_t version = _game->GetSession().Version();
        const Seat viewer = _game->GetSession().Viewer();

        Draw();
        Draw();

        EXPECT_EQ(Snapshot(Hand()), hand);
        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetSession().Viewer(), viewer);
    }

}  // namespace BECore::Mahjong::Tests
