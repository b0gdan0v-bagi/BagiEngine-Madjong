#include "MahjongHubTransport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <Common/TempPath.h>
#include <Mahjong/Save/MatchSave.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/TileHover.h>
#include <memory>

/**
 * The table of a started online game in the shipped scene: the game's client over the in-memory hub with three other people
 * (`HubPlayer`), the display pair that `MahjongGame` shows while the client has a table (`IsOnlineTable()`, `GetMatch()`), and what
 * must not notice it (the local match, the save slot, the control API). The unit under test is the scene's glue; the translation of a
 * view is `MahjongOnlineMirror`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kPumpLimit = 2000;
        constexpr std::chrono::milliseconds kTick{10};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr const char* kTempFolder = "BagiMahjongOnlineTableSceneTest";
        constexpr eastl::string_view kUrl = "ws://hub.test";
        constexpr const char* kHostName = "Ann";

    }  // namespace

    class MahjongOnlineTableSceneTest : public MahjongTableTestBase {
    protected:
        void SetUp() override {
            _folder = TestTempDirectory(kTempFolder);
            std::filesystem::remove_all(_folder);
            SetMatchSaveDirectory(_folder);
            MahjongTableTestBase::SetUp();
            ASSERT_TRUE(_game->SetOnlineSettings(_world.MakeClientSettings(_links)));
        }

        void TearDown() override {
            _others.clear();
            MahjongTableTestBase::TearDown();
            SetMatchSaveDirectory(std::filesystem::path());
            std::error_code error;
            std::filesystem::remove_all(_folder, error);
        }

        /// One frame as the application runs it: the clock of the world, the others read what came, the scene updates and draws.
        void Frame() {
            _world.Advance(kTick);
            for (const std::unique_ptr<HubPlayer>& other : _others) {
                other->Update();
            }
            AdvanceTime(kFrameSeconds);
            Draw();
            if (!_links.links.empty() && _game->GetOnline().Seat() >= 0) {
                _links.Last().judgeSeat = _game->GetOnline().Seat();
            }
        }

        template <typename Done>
        bool PumpUntil(Done done) {
            for (int count = 0; count < kPumpLimit; ++count) {
                if (done()) {
                    return true;
                }
                Frame();
            }
            return done();
        }

        /// The game's client hosts a room of @p hands hands with three people, starts it, and waits for the table.
        void StartRoom(int hands = kShortMatchHands) {
            OnlineClient& client = _game->GetOnline();
            ASSERT_TRUE(client.Host(kUrl, kHostName, OnlineRoomOptions{hands, kMinLevel}));
            ASSERT_TRUE(PumpUntil([&] { return client.Stage() == OnlineStage::Lobby; }));
            for (const char* name : {"Bo", "Cy", "Di"}) {
                _others.push_back(std::make_unique<HubPlayer>(_world));
                HubPlayer& player = *_others.back();
                ASSERT_TRUE(player.Client().Join(kUrl, client.Room(), name));
                ASSERT_TRUE(PumpUntil([&] { return player.Client().Stage() == OnlineStage::Lobby; }));
            }
            ASSERT_TRUE(PumpUntil([&] { return client.Lobby().canStart; }));
            ASSERT_TRUE(client.Start());
            ASSERT_TRUE(PumpUntil([&] { return _game->IsOnlineTable(); }));
        }

        const OnlineTable& Table() const {
            return _game->GetOnline().Table();
        }

        Seat Viewer() const {
            return _game->GetSession().Viewer();
        }

        /// Whether the server lists an action of @p kind for the game's client now.
        bool Lists(ActionKind kind) const {
            const auto& actions = Table().actions;
            return _game->GetOnline().Stage() == OnlineStage::Game &&
                   eastl::any_of(actions.begin(), actions.end(), [&](const ControlAction& listed) { return !listed.nextHand && listed.hand.kind == kind; });
        }

        /// Frames until @p done is true after one, the other people acting for themselves (the first action of their list, once per view); the game's client acts the same way
        /// unless @p viewerActs is false (then it waits for the test). False when the limit came first.
        template <typename Done>
        bool PlayUntil(Done done, bool viewerActs = true) {
            constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();
            uint64_t acted = kNever;
            for (int count = 0; count < kPumpLimit; ++count) {
                Frame();
                if (done()) {
                    return true;
                }
                for (const std::unique_ptr<HubPlayer>& other : _others) {
                    other->ActFirst();
                }
                OnlineClient& client = _game->GetOnline();
                if (viewerActs && client.Stage() == OnlineStage::Game && !Table().actions.empty() && Table().version != acted) {
                    acted = Table().version;
                    client.Act(0);
                }
            }
            return false;
        }

        /// The files of the save folder that hold the match, by name.
        std::vector<std::pair<std::string, std::string>> MatchSlot() const {
            std::vector<std::pair<std::string, std::string>> files;
            for (const auto& entry : std::filesystem::directory_iterator(_folder)) {
                const std::string name = entry.path().filename().string();
                if (name.find(std::string(kMatchSlot.data(), kMatchSlot.size())) == std::string::npos) {
                    continue;
                }
                std::ifstream in(entry.path(), std::ios::binary);
                std::ostringstream content;
                content << in.rdbuf();
                files.emplace_back(name, content.str());
            }
            return files;
        }

        MahjongHubWorld _world;
        HubClientLinks _links;
        std::vector<std::unique_ptr<HubPlayer>> _others;
        std::filesystem::path _folder;
    };

    TEST_F(MahjongOnlineTableSceneTest, TheGameShowsTheDisplayPairWhileTheRoomHasATableAndGivesTheLocalGameBack) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Frame();
        Frame();
        const std::vector<std::pair<std::string, std::string>> savedBefore = MatchSlot();
        ASSERT_FALSE(savedBefore.empty()) << "the local match is saved";
        const bool canContinue = _game->CanContinue();
        const Match* const local = &_game->GetLocalMatch();
        const uint64_t localVersion = _game->GetLocalSession().Version();
        const uint64_t localStarts = _game->GetLocalSession().StartCount();
        const eastl::array<int, kSeatCount> localScores = _game->GetLocalMatch().Scores();
        const size_t localMoves = _game->GetLocalMatch().Hand().Moves().size();
        EXPECT_EQ(&_game->GetMatch(), local);
        EXPECT_FALSE(_game->IsOnlineTable());

        ASSERT_NO_FATAL_FAILURE(StartRoom());

        // The table of the room is shown; the local pair is untouched
        EXPECT_NE(&_game->GetMatch(), local);
        EXPECT_EQ(&_game->GetLocalMatch(), local);
        EXPECT_TRUE(_game->GetSession().IsRemote());
        EXPECT_FALSE(_game->GetLocalSession().IsRemote());
        EXPECT_EQ(_game->GetSession().Viewer(), _game->GetOnline().Seat());
        EXPECT_EQ(_game->GetLocalSession().Version(), localVersion);
        EXPECT_EQ(_game->GetLocalSession().StartCount(), localStarts);
        EXPECT_EQ(_game->GetLocalMatch().Scores(), localScores);
        EXPECT_EQ(_game->GetLocalMatch().Hand().Moves().size(), localMoves);

        // The panels are closed and the table is played (not paused)
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(_game->IsOnlineOpen());
        EXPECT_FALSE(_game->IsTablePaused());
        EXPECT_EQ(_game->CanContinue(), canContinue) << "the offer of Continue survives the room";

        // The save slot is the local match's, the frames of the room did not touch it; the match log and the upload saw nothing
        for (int i = 0; i < 5; ++i) {
            Frame();
        }
        EXPECT_EQ(MatchSlot(), savedBefore);
        EXPECT_EQ(_world.Judge().Offences().size(), 0u);

        // The control API does not see the room
        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        EXPECT_FALSE(control->GetState().has_value());
        EXPECT_FALSE(control->GetActions().has_value());
        EXPECT_FALSE(control->PerformAction(Json::object()).has_value());
        EXPECT_FALSE(control->Reset(Json::object()).has_value());
        EXPECT_EQ(_game->GetLocalSession().Version(), localVersion) << "a refused reset started nothing";

        // Leave: the local game is as it was, behind Setup
        ASSERT_TRUE(_game->GetOnline().Leave());
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        EXPECT_EQ(&_game->GetMatch(), local);
        EXPECT_FALSE(_game->GetSession().IsRemote());
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_EQ(_game->GetLocalSession().Version(), localVersion);
        EXPECT_EQ(_game->GetLocalMatch().Scores(), localScores);
        EXPECT_EQ(MatchSlot(), savedBefore);
        EXPECT_TRUE(control->GetState().has_value()) << "the control API plays the local game again";
    }

    TEST_F(MahjongOnlineTableSceneTest, TheTableDrawsTheOwnTilesFaceUpAndEveryoneElsesAsBacks) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Hand().Discards().size() >= 6 && _game->GetSession().GetPhase() == SessionPhase::Playing; }));
        const Seat viewer = Viewer();
        ASSERT_TRUE(IsSeat(viewer));
        ExpectTableDrawn(false, viewer);
        EXPECT_EQ(FaceUpHandTiles(viewer), static_cast<int>(Hand().GetHand(viewer).concealed.size()));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat == viewer) {
                continue;
            }
            EXPECT_EQ(FaceUpHandTiles(seat), 0) << "seat " << seat;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.seat == seat && slot.role == SlotRole::HandTile) {
                    EXPECT_FALSE(TileOfSlot(Hand(), slot, false).has_value());
                }
            }
        }
        EXPECT_EQ(_world.Judge().Offences().size(), 0u);
    }

    TEST_F(MahjongOnlineTableSceneTest, AClickOnAHandTileSendsTheListedDiscardAndTheNextViewMovesThePile) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }));
        const Seat viewer = Viewer();
        const size_t piled = Hand().Discards().size();
        constexpr int kIndex = 3;
        const Tile clicked = Hand().GetHand(viewer).concealed[kIndex];
        const uint64_t version = Table().version;

        ASSERT_FALSE(ClickHandTile(kIndex)) << "no canvas took the click";
        ASSERT_TRUE(PlayUntil([&] { return Table().version != version; }, false));
        ASSERT_EQ(Hand().Discards().size(), piled + 1);
        EXPECT_EQ(Hand().Discards().back().tile, clicked);
        EXPECT_EQ(Hand().Discards().back().seat, viewer);
        EXPECT_EQ(_game->GetOnline().Notice().serial, 0u) << "nothing to say";
    }

    TEST_F(MahjongOnlineTableSceneTest, AClaimWindowShowsTheClaimButtonsAndPassSendsPass) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Pass) && Hand().GetPhase() == Phase::Claim; }));
        Draw();
        EXPECT_TRUE(_table->GetButtons()[ButtonKind::Pass].shown);
        int claims = 0;
        for (const ButtonKind kind : {ButtonKind::ClaimMahjong, ButtonKind::ClaimPung, ButtonKind::ClaimKong, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2}) {
            claims += _table->GetButtons()[kind].shown ? 1 : 0;
        }
        EXPECT_GT(claims, 0) << "a window that lists a pass lists a claim";
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Declare].shown);
        EXPECT_EQ(Hand().PendingClaimSeats(), eastl::vector<Seat>{Viewer()});

        const uint64_t version = _world.Game()->GetStateVersion();
        EmitAction(SpecOf(ButtonKind::Pass).action, SpecOf(ButtonKind::Pass).value);
        EXPECT_GT(_world.Game()->GetStateVersion(), version) << "the server took the pass";
        ASSERT_TRUE(PlayUntil([&] { return !Lists(ActionKind::Pass) || Hand().GetPhase() != Phase::Claim; }, false));
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Pass].shown);
    }

    TEST_F(MahjongOnlineTableSceneTest, AClickWhileTheServerListsNoDiscardDoesNothing) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Table().actions.empty() && _game->GetSession().GetPhase() == SessionPhase::Playing; }));
        const uint64_t version = _world.Game()->GetStateVersion();
        const size_t piled = Hand().Discards().size();
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Pass].shown);
        ClickHandTile(0);
        EmitAction(SpecOf(ButtonKind::Pass).action, SpecOf(ButtonKind::Pass).value);
        for (int i = 0; i < 5; ++i) {
            Frame();
        }
        EXPECT_EQ(_world.Game()->GetStateVersion(), version) << "nothing was sent";
        EXPECT_EQ(Hand().Discards().size(), piled);
    }

    TEST_F(MahjongOnlineTableSceneTest, AnActionThatTheServerRefusedAsStaleSaysNothingAndTheNextViewFixesTheTable) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        // A window in which the game's client and another person may both decide
        const auto otherDecides = [&] {
            return eastl::any_of(_others.begin(), _others.end(),
                                 [](const std::unique_ptr<HubPlayer>& other) { return !other->Table().actions.empty() && other->Client().Stage() == OnlineStage::Game; });
        };
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Pass) && Hand().GetPhase() == Phase::Claim && otherDecides(); }));
        Draw();
        for (const std::unique_ptr<HubPlayer>& other : _others) {
            other->ActFirst();  // the other decides: the view of the game's client is old before it clicks
        }
        const uint64_t shown = Table().version;
        ASSERT_GT(_world.Game()->GetStateVersion(), shown);
        EmitAction(SpecOf(ButtonKind::Pass).action, SpecOf(ButtonKind::Pass).value);
        EXPECT_EQ(_world.Game()->GetStateVersion(), shown + 1) << "the stale pass changed nothing";
        ASSERT_TRUE(PlayUntil([&] { return Table().version == _world.Game()->GetStateVersion(); }, false));
        EXPECT_EQ(_game->GetOnline().Notice().serial, 0u) << "a stale action is silent";
        EXPECT_TRUE(CoreManager::GetUIVars().Get("onlineNotice"_intern).empty());
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::Playing);
        EXPECT_EQ(Hand().Discards().size(), Table().discards.size()) << "the table is the view's";
    }

    TEST_F(MahjongOnlineTableSceneTest, NoHintIsShownInARoomWhateverTheSwitchSays) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(_game->HintsOn()) << "the default switch";
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }));
        Frame();
        const auto hintsCanvas = Canvas("HintsButton");
        EXPECT_FALSE(hintsCanvas && hintsCanvas->TakesInput() && OnScreen("HintsButton", "HintButton"));
        const auto compactCanvas = Canvas("ActionsCompact");
        EXPECT_FALSE(compactCanvas && compactCanvas->TakesInput() && OnScreen("ActionsCompact", "HintButton"));
        EXPECT_FALSE(OnScreen("Hints", "WaitPanel"));
        EXPECT_FALSE(OnScreen("Hints", "AdviceText"));
    }

}  // namespace BECore::Mahjong::Tests
