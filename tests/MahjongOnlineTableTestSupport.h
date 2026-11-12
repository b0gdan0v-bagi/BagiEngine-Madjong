#pragma once

#include "MahjongHubTransport.h"
#include "MahjongTableTestSupport.h"

#include <Common/TempPath.h>
#include <Mahjong/Save/MatchSave.h>
#include <chrono>
#include <memory>
#include <vector>

/**
 * The table of a started online game in the shipped scene: the game's client over the in-memory hub (`MahjongHubWorld`) at a seat beside
 * `HubPlayer`s, on the fixture of the table tests. Shared by `MahjongOnlineTableSceneTest` and `MahjongTableAllocTest`. The match save of
 * the game goes to a temp folder named after the test suite, so suites that run at once do not meet.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kPumpLimit = 2000;
        constexpr std::chrono::milliseconds kTick{10};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr eastl::string_view kUrl = "ws://hub.test";
        constexpr const char* kHostName = "Ann";

    }  // namespace

    class MahjongOnlineTableTestBase : public MahjongTableTestBase {
    protected:
        void SetUp() override {
            _folder = TestTempDirectory(::testing::UnitTest::GetInstance()->current_test_suite()->name());
            std::filesystem::remove_all(_folder);
            SetMatchSaveDirectory(_folder);
            MahjongTableTestBase::SetUp();
            ASSERT_TRUE(_game->SetOnlineSettings(ClientSettings()));
        }

        /// The game's client runs on the real clock (`OnlineClient::Update()`), so what it waits for is real time.
        virtual RoomClientSettings ClientSettings() {
            return _world.MakeClientSettings(_links);
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

        /// The game's client hosts a room of @p hands hands with @p others more people (the seats left over are computers; @p hintsAllowed is the host's choice for the room), starts it, and waits for
        /// the table.
        void StartRoom(int hands = kShortMatchHands, int others = 3, bool hintsAllowed = false) {
            OnlineClient& client = _game->GetOnline();
            ASSERT_TRUE(client.Host(kUrl, kHostName, OnlineRoomOptions{hands, kMinLevel, hintsAllowed}));
            ASSERT_TRUE(PumpUntil([&] { return client.Stage() == OnlineStage::Lobby; }));
            for (const char* name : {"Bo", "Cy", "Di"}) {
                if (static_cast<int>(_others.size()) == others) {
                    break;
                }
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
        bool PlayUntil(Done done, bool viewerActs = true, int limit = kPumpLimit) {
            constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();
            uint64_t acted = kNever;
            for (int count = 0; count < limit; ++count) {
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

        MahjongHubWorld _world;
        HubClientLinks _links;
        std::vector<std::unique_ptr<HubPlayer>> _others;
        std::filesystem::path _folder;
    };

}  // namespace BECore::Mahjong::Tests
