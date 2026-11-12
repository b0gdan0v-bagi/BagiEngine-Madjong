#include "MahjongHubTransport.h"
#include "StringsTestSupport.h"

#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Mahjong/MahjongGame.h>
#include <gtest/gtest.h>

/**
 * The online play in `MahjongGame` (`MahjongGame.h`: `GetOnline`, `SetOnlineSettings`, `IsOnlineOpen`, `StoredOnline`, `SetOnlineName`) and
 * the `online` slot (`Save/MatchSave.h`): the name and the kept session survive a restart of the game object, a session that the client
 * forgot leaves the slot, a flush with nothing new writes nothing, and a slot that cannot be used is one warning and nothing applies. The
 * client talks to the in-memory hub of `MahjongHubTransport.h`. The token is a secret: it is only ever compared here, never printed.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;
    namespace fs = std::filesystem;
    using std::chrono::milliseconds;

    namespace {

        constexpr const char* kFolderPrefix = "BagiMahjongOnlineSaveTest";
        constexpr const char* kOnlineFile = "online.save";
        constexpr eastl::string_view kUrl = "ws://hub.test";
        constexpr milliseconds kTick{10};
        constexpr int kStepLimit = 2000;

        class MahjongOnlineSaveTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                EXPECT_TRUE(LoadShippedStrings(CoreManager::GetUIStrings()).empty());
                folder = TestTempDirectory(kFolderPrefix);
                fs::remove_all(folder);
                SetMatchSaveDirectory(folder);
            }

            void TearDown() override {
                SetMatchSaveDirectory(fs::path());
                std::error_code error;
                fs::remove_all(folder, error);
                CoreManager::GetUIStrings().Clear();
            }

            /// A game that starts on the folder as the executable does, its client on the transports of the world.
            IntrusivePtr<MahjongGame> StartedGame() {
                RoomClientSettings settings;
                settings.transportFactory = world.Factory(links);
                settings.pingInterval = milliseconds(0);  // the manual clock jumps: no heartbeat to answer
                settings.silenceLimit = milliseconds(0);
                IntrusivePtr<MahjongGame> game = New<MahjongGame>();
                EXPECT_TRUE(game->SetOnlineSettings(std::move(settings)));
                game->Start();
                return game;
            }

            /// Moves the clock and updates the client until @p done, or the step limit.
            template <typename Done>
            bool Pump(MahjongGame& game, Done done) {
                for (int count = 0; count < kStepLimit && !done(); ++count) {
                    world.Advance(kTick);
                    game.GetOnline().Update(world.Now());
                }
                return done();
            }

            bool InStage(MahjongGame& game, OnlineStage stage) {
                return game.GetOnline().Stage() == stage;
            }

            /// The state of the slot as the next game would read it, or the problem.
            std::expected<OnlineValues, SaveProblem> Slot() const {
                MatchSaveStore store(CoreManager::GetFileSystem());
                EXPECT_TRUE(store.Open(folder));
                return store.LoadOnline();
            }

            void WriteSlot(const MahjongOnlineSave& save) const {
                MatchSaveStore store(CoreManager::GetFileSystem());
                ASSERT_TRUE(store.Open(folder));
                ASSERT_TRUE(store.SaveOnline(save));
            }

            fs::path folder;
            // The links come before the games that use them, and the world outlives them
            MahjongHubWorld world;
            HubClientLinks links;
        };

        RoomSession SomeSession() {
            RoomSession session;
            session.url = kUrl;
            session.room = "K7M4Q";
            session.token = "00112233445566778899aabbccddeeff";
            return session;
        }

    }  // namespace

    TEST_F(MahjongOnlineSaveTest, TheGameStartsWithTheStoredNameAndSession) {
        WriteSlot(MakeOnlineSave("Анна", SomeSession()));
        IntrusivePtr<MahjongGame> game = StartedGame();
        EXPECT_EQ(game->StoredOnline().name, "Анна");
        EXPECT_TRUE(game->StoredOnline().session.IsValid());
        EXPECT_TRUE(game->StoredOnline().session.url == kUrl);
        EXPECT_TRUE(game->StoredOnline().session.room == "K7M4Q");
        EXPECT_TRUE(game->StoredOnline().session.token == SomeSession().token);  // compared, never printed
        EXPECT_EQ(game->FindOnline(), nullptr) << "reading the slot makes no client";
    }

    TEST_F(MahjongOnlineSaveTest, ASlotThatCannotBeUsedIsOneWarningAndNothingApplies) {
        RoomSession badToken = SomeSession();
        badToken.token = "short";
        WriteSlot(MakeOnlineSave("Ann", badToken));
        IntrusivePtr<MahjongGame> game = StartedGame();
        EXPECT_TRUE(game->StoredOnline().name.empty()) << "not even the name of a broken slot applies";
        EXPECT_FALSE(game->StoredOnline().session.IsValid());
        EXPECT_TRUE(game->IsSetupOpen()) << "the game plays on";
        game->FlushSave();
        EXPECT_TRUE(fs::exists(folder / kOnlineFile)) << "the file stays until a choice replaces it";
        EXPECT_EQ(Slot().error().kind, SaveProblemKind::Broken);
    }

    TEST_F(MahjongOnlineSaveTest, ARoomIsKeptWhenTheClientKeepsItAndForgottenWhenItDoesNot) {
        IntrusivePtr<MahjongGame> game = StartedGame();
        game->SetOnlineName("Ann");
        game->FlushSave();
        ASSERT_TRUE(Slot().has_value());
        EXPECT_EQ(Slot()->name, "Ann");
        EXPECT_FALSE(Slot()->session.IsValid());

        ASSERT_TRUE(game->GetOnline().Host(kUrl, "Ann", OnlineRoomOptions{kShortMatchHands, kMinLevel}));
        ASSERT_TRUE(Pump(*game, [&] { return InStage(*game, OnlineStage::Lobby); }));
        game->FlushSave();
        const std::expected<OnlineValues, SaveProblem> kept = Slot();
        ASSERT_TRUE(kept.has_value());
        EXPECT_EQ(kept->name, "Ann");
        EXPECT_TRUE(kept->session.IsValid());
        EXPECT_TRUE(kept->session.room == game->GetOnline().Room());
        EXPECT_TRUE(kept->session.token == game->GetOnline().SessionToKeep().token);
        EXPECT_TRUE(game->StoredOnline().session.token == kept->session.token) << "the game follows the client";

        ASSERT_TRUE(game->GetOnline().Leave());
        ASSERT_TRUE(Pump(*game, [&] { return !game->GetOnline().SessionToKeep().IsValid(); }));
        game->FlushSave();
        const std::expected<OnlineValues, SaveProblem> forgotten = Slot();
        ASSERT_TRUE(forgotten.has_value());
        EXPECT_EQ(forgotten->name, "Ann") << "the name stays";
        EXPECT_FALSE(forgotten->session.IsValid()) << "the token is gone from the file";
        EXPECT_FALSE(game->StoredOnline().session.IsValid());
    }

    TEST_F(MahjongOnlineSaveTest, TheSlotIsRemovedWhenThereIsNeitherNameNorSession) {
        IntrusivePtr<MahjongGame> game = StartedGame();
        game->SetOnlineName("Ann");
        game->FlushSave();
        ASSERT_TRUE(fs::exists(folder / kOnlineFile));
        game->SetOnlineName("");
        game->FlushSave();
        EXPECT_FALSE(fs::exists(folder / kOnlineFile));
    }

    TEST_F(MahjongOnlineSaveTest, TheSameFlushWritesNothingTwice) {
        IntrusivePtr<MahjongGame> game = StartedGame();
        game->FlushSave();
        EXPECT_FALSE(fs::exists(folder / kOnlineFile)) << "nothing chosen, nothing written";
        game->SetOnlineName("Ann");
        game->FlushSave();
        ASSERT_TRUE(fs::exists(folder / kOnlineFile));

        fs::remove(folder / kOnlineFile);
        game->FlushSave();
        EXPECT_FALSE(fs::exists(folder / kOnlineFile)) << "the name did not move: no second write";
        game->SetOnlineName("Ann");
        game->FlushSave();
        EXPECT_FALSE(fs::exists(folder / kOnlineFile)) << "the same name is no move";
        game->SetOnlineName("Bob");
        game->FlushSave();
        EXPECT_TRUE(fs::exists(folder / kOnlineFile));

        const std::expected<OnlineValues, SaveProblem> loaded = Slot();
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(loaded->name, "Bob");
    }

    TEST_F(MahjongOnlineSaveTest, AStoredSessionBringsThePlayerBackAfterTheGameObjectIsMadeAgain) {
        std::string room;
        {
            IntrusivePtr<MahjongGame> game = StartedGame();
            ASSERT_TRUE(game->GetOnline().Host(kUrl, "Ann", OnlineRoomOptions{kShortMatchHands, kMinLevel}));
            ASSERT_TRUE(Pump(*game, [&] { return InStage(*game, OnlineStage::Lobby); }));
            room.assign(game->GetOnline().Room().c_str(), game->GetOnline().Room().size());
            ASSERT_TRUE(game->GetOnline().Start());  // a room in the lobby is gone with its last member; a started one holds the seat
            ASSERT_TRUE(Pump(*game, [&] { return InStage(*game, OnlineStage::Game); }));
            game->Stop();  // writes the slot, as closing the game does
        }
        links.links.clear();

        IntrusivePtr<MahjongGame> again = StartedGame();
        ASSERT_TRUE(again->StoredOnline().session.IsValid());
        EXPECT_TRUE(again->StoredOnline().session.room == room.c_str());
        ASSERT_TRUE(again->GetOnline().Resume(again->StoredOnline().session));
        EXPECT_TRUE(Pump(*again, [&] { return InStage(*again, OnlineStage::Game); })) << "the stored token is the one the room knows";
        EXPECT_TRUE(again->GetOnline().Room() == room.c_str());
        EXPECT_EQ(world.Judge().Offences().size(), 0u);
    }

    TEST_F(MahjongOnlineSaveTest, TheOnlinePanelIsAFlagThatPausesTheTableAndSetupDoesNotClose) {
        IntrusivePtr<MahjongGame> game = StartedGame();
        ASSERT_TRUE(game->IsSetupOpen());
        EXPECT_FALSE(game->IsOnlineOpen());
        game->CloseSetup();
        EXPECT_FALSE(game->IsTablePaused());
        game->OpenOnline();
        EXPECT_TRUE(game->IsOnlineOpen());
        EXPECT_TRUE(game->IsTablePaused());
        game->CloseSetup();
        EXPECT_TRUE(game->IsOnlineOpen()) << "a start or a control action does not close the panel";
        game->CloseOnline();
        EXPECT_FALSE(game->IsTablePaused());

        game->OpenSetup();
        game->OpenOnline();
        game->CloseOnline();
        EXPECT_TRUE(game->IsSetupOpen()) << "closing the panel gives Setup back";

        game->OpenOnline();
        game->Stop();
        EXPECT_FALSE(game->IsOnlineOpen());
        game->OpenOnline();
        game->Start();
        EXPECT_FALSE(game->IsOnlineOpen());
    }

    TEST_F(MahjongOnlineSaveTest, TheSettingsAreRefusedOnceTheClientExists) {
        IntrusivePtr<MahjongGame> game = New<MahjongGame>();
        EXPECT_TRUE(game->SetOnlineSettings(RoomClientSettings{}));
        EXPECT_EQ(game->FindOnline(), nullptr);
        OnlineClient& client = game->GetOnline();
        EXPECT_EQ(&game->GetOnline(), &client) << "one client for the life of the game";
        EXPECT_FALSE(game->SetOnlineSettings(RoomClientSettings{}));
    }

}  // namespace BECore::Mahjong::Tests
