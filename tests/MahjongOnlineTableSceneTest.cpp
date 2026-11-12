#include "MahjongHubTransport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <Common/TempPath.h>
#include <Mahjong/Save/MatchSave.h>
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

}  // namespace BECore::Mahjong::Tests
