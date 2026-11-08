#include "HandTestSupport.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Games/GameManager.h>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/TileAtlas.h>
#include <gtest/gtest.h>

/**
 * The table as drawn (`Components/MahjongTableComponent.h`): the shipped `config/SceneConfig.xml` (read from the source tree, a game's
 * content is not mounted in the test environment) is drawn into a `RecordingRenderer` and the draw calls are read back. The renderer is as
 * big as the view of the scene, so the view transform is the identity and a call's rect is a slot of the layout. Needs the engine services.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr uint32_t kSeed = 7;
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kTwoPeople{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        /// A stand-in atlas: the draw calls carry the source rects, the pixels do not matter.
        constexpr uint32_t kTextureSide = 4;
        /// Seeds tried while looking for a table with a meld and for one where seat 0 deals.
        constexpr uint32_t kSeedsToTry = 40;
        /// Actions of a hand played by computers; a loop fails fast.
        constexpr int kActionLimit = 600;
        /// Steps after which the live wall has certainly shrunk by a few stacks.
        constexpr int kStepsForTheWall = 40;

        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

        /// The tile at `part` of a meld as laid, worked out here and not taken from the component.
        Tile LaidTile(const Meld& meld, int part) {
            return meld.kind == MeldKind::Chow ? Tile(static_cast<uint8_t>(meld.first.Id() + part)) : meld.first;
        }

    }  // namespace

    class MahjongTableUiTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            _renderer->width = kViewWidth;
            _renderer->height = kViewHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(ShippedSceneConfig())) << ShippedSceneConfig().string();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);  // GameManager switches to the scene of the game through CoreManager
            GameManager::GetInstance().Initialize();

            _game = MahjongGame::GetActive();
            ASSERT_NE(_game, nullptr);
            _table = MahjongTableComponent::GetActive();
            ASSERT_NE(_table, nullptr);
            const std::vector<uint8_t> pixels(kTextureSide * kTextureSide * 4, 255);
            _table->SetAtlasTexture(_renderer->CreateTexture(pixels.data(), kTextureSide, kTextureSide));
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void Start(const SeatSetup& seats, uint32_t seed = kSeed, bool flowers = false) {
            MahjongSettings settings = _game->GetSettings();
            settings.flowers = flowers;
            _game->SetSettings(settings);
            _game->SetSeats(seats);
            ASSERT_TRUE(_game->StartMatch(seed).has_value());
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// The draw calls of tiles, in the order of the layout's slots.
        std::vector<DrawCall> TileCalls() const {
            std::vector<DrawCall> calls;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture) {
                    calls.push_back(call);
                }
            }
            return calls;
        }

        const HandModel& Hand() const {
            return _game->GetMatch().Hand();
        }

        /// What the model says the slot shows: a tile, or nullopt for a back. `faces` says whose concealed tiles are open.
        std::optional<Tile> Expected(const TileSlot& slot, bool allFaces, Seat faceSeat) const {
            const PlayerHand& held = Hand().GetHand(slot.seat);
            const bool open = allFaces || slot.seat == faceSeat;
            switch (slot.role) {
                case SlotRole::HandTile:
                    return open ? std::optional<Tile>(held.concealed[slot.index]) : std::nullopt;
                case SlotRole::MeldTile: {
                    const Meld& meld = held.melds[slot.index];
                    return (!meld.concealed || open) ? std::optional<Tile>(LaidTile(meld, slot.part)) : std::nullopt;
                }
                case SlotRole::BonusTile:
                    return held.bonus[slot.index];
                case SlotRole::DiscardTile:
                    return Hand().Discards()[slot.index].tile;
                case SlotRole::WallStack:
                    break;
            }
            return std::nullopt;
        }

        /// Every slot of the layout was drawn once, in order, at its rect, with the face the model allows; nothing else was a tile.
        void ExpectTableDrawn(bool allFaces, Seat faceSeat) {
            const std::vector<DrawCall> calls = TileCalls();
            const TableLayout& layout = _table->GetLayout();
            ASSERT_EQ(calls.size(), layout.slots.size());
            for (size_t i = 0; i < calls.size(); ++i) {
                const TileSlot& slot = layout.slots[i];
                const std::optional<Tile> tile = Expected(slot, allFaces, faceSeat);
                const Rect expected = tile ? SourceRect(*tile) : BackRect();
                ASSERT_TRUE(calls[i].source.has_value()) << "slot " << i;
                EXPECT_EQ(calls[i].source->x, expected.x) << "slot " << i << " role " << static_cast<int>(slot.role) << " seat " << slot.seat;
                EXPECT_EQ(calls[i].source->y, expected.y) << "slot " << i << " role " << static_cast<int>(slot.role) << " seat " << slot.seat;
                EXPECT_EQ(calls[i].rect.x, slot.rect.x) << "slot " << i;
                EXPECT_EQ(calls[i].rect.y, slot.rect.y) << "slot " << i;
                EXPECT_EQ(calls[i].rect.w, slot.rect.w) << "slot " << i;
                EXPECT_EQ(calls[i].rect.h, slot.rect.h) << "slot " << i;
            }
        }

        /// The number of hand tiles of `seat` in the layout that are drawn face up.
        int FaceUpHandTiles(Seat seat) const {
            const std::vector<DrawCall> calls = TileCalls();
            const TableLayout& layout = _table->GetLayout();
            int faceUp = 0;
            for (size_t i = 0; i < calls.size() && i < layout.slots.size(); ++i) {
                const TileSlot& slot = layout.slots[i];
                const bool isBack = calls[i].source->x == BackRect().x && calls[i].source->y == BackRect().y;
                if (slot.role == SlotRole::HandTile && slot.seat == seat && !isBack) {
                    ++faceUp;
                }
            }
            return faceUp;
        }

        /// Steps the computers (all seats) until `done` or the limit; false when the limit came first.
        template <typename Done>
        bool StepUntil(Done done) {
            for (int step = 0; step < kActionLimit; ++step) {
                if (done()) {
                    return true;
                }
                if (_game->GetSession().GetPhase() != SessionPhase::Playing || !_game->GetSession().StepComputer().has_value()) {
                    return false;
                }
            }
            return false;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        MahjongGame* _game = nullptr;
        MahjongTableComponent* _table = nullptr;
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

        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);
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
