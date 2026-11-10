#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

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

    TEST_F(MahjongTableUiTest, WithoutAnActiveGameOnlyTheFeltIsDrawn) {
        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(MahjongGame::GetActive(), nullptr);
        _manager->SetActiveScene("MahjongScene"_intern);

        Draw();

        // The table draws the felt and nothing else (the canvases of the scene draw their own, with no match behind them)
        ASSERT_FALSE(_renderer->calls.empty());
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);
        EXPECT_TRUE(TileCalls().empty());
        EXPECT_TRUE(_table->GetLayout().slots.empty());
        // ... and the rim round it: no plate, no shadow
        EXPECT_EQ(RectsOf(kFeltRim).size(), 4u);
        EXPECT_TRUE(RectsOf(kZonePlate).empty());
        EXPECT_TRUE(RectsOf(kTileShadow).empty());
    }

    TEST_F(MahjongTableUiTest, TheRimIsFourRectanglesAlongTheEdgeRightAfterTheFelt) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        ASSERT_GE(_renderer->calls.size(), 5u);
        EXPECT_EQ(_renderer->calls[0].color.r, kFelt.r);
        EXPECT_EQ(_renderer->calls[0].color.g, kFelt.g);
        EXPECT_EQ(_renderer->calls[0].color.b, kFelt.b);
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

    TEST_F(MahjongTableUiTest, ThePlatesAreTheDiscardBlocksAndTheWallRingPaddedBy4) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const std::vector<DrawCall> plates = RectsOf(kZonePlate);
        ASSERT_EQ(plates.size(), 5u) << "four discard blocks and the wall ring";
        const TableZones& zones = _table->GetLayout().zones;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(plates[seat].rect.x, zones.discards[seat].x - 4.0f) << "seat " << seat;
            EXPECT_EQ(plates[seat].rect.y, zones.discards[seat].y - 4.0f) << "seat " << seat;
            EXPECT_EQ(plates[seat].rect.w, zones.discards[seat].w + 8.0f) << "seat " << seat;
            EXPECT_EQ(plates[seat].rect.h, zones.discards[seat].h + 8.0f) << "seat " << seat;
        }
        EXPECT_EQ(plates[4].rect.x, zones.wall.x - 4.0f);
        EXPECT_EQ(plates[4].rect.w, zones.wall.w + 8.0f);
        EXPECT_EQ(plates[4].rect.h, zones.wall.h + 8.0f);
    }

    TEST_F(MahjongTableUiTest, TheWallPlateKeepsTheSameGapToTheTopAndTheBottomDiscardPlates) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const std::vector<DrawCall> plates = RectsOf(kZonePlate);
        ASSERT_EQ(plates.size(), 5u);
        const Rect& top = plates[2].rect;
        const Rect& bottom = plates[0].rect;
        const Rect& wall = plates[4].rect;
        EXPECT_GT(GapBelow(top, wall), 0.0f) << "the plates do not merge";
        EXPECT_EQ(GapBelow(top, wall), GapBelow(wall, bottom));
    }

    TEST_F(MahjongTableUiTest, EveryTileThatIsNotAWallStackHasAShadowOffsetBy2) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();

        const TableLayout& layout = _table->GetLayout();
        const std::vector<DrawCall> shadows = RectsOf(kTileShadow);
        ASSERT_EQ(static_cast<int>(shadows.size()), NotWallStacks(layout));
        size_t next = 0;
        for (const TileSlot& slot : layout.slots) {
            if (slot.role == SlotRole::WallStack) {
                continue;
            }
            EXPECT_EQ(shadows[next].rect.x, slot.rect.x + 2.0f);
            EXPECT_EQ(shadows[next].rect.y, slot.rect.y + 2.0f);
            EXPECT_EQ(shadows[next].rect.w, slot.rect.w);
            EXPECT_EQ(shadows[next].rect.h, slot.rect.h);
            ++next;
        }
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

    TEST_F(MahjongTableUiTest, ASpectatorSeesNoConcealedTileWhileThePlayIsOn) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_TRUE(StepUntil([&] { return !Hand().Discards().empty(); }));

        Draw();

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kNoSeat));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(FaceUpHandTiles(seat), 0) << "seat " << seat;
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

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kNoSeat));
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
