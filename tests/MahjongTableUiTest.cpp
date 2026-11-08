#include "MahjongTableTestSupport.h"

/**
 * The table as drawn (`Components/MahjongTableComponent.h`): the shipped `config/SceneConfig.xml` is drawn into a `RecordingRenderer` and
 * the draw calls are read back (the fixture is `MahjongTableTestSupport.h`). Needs the engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seeds tried while looking for a table with a meld and for one where seat 0 deals.
        constexpr uint32_t kSeedsToTry = 40;
        /// Steps after which the live wall has certainly shrunk by a few stacks.
        constexpr int kStepsForTheWall = 40;

    }  // namespace

    class MahjongTableUiTest : public MahjongTableTestBase {};

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
