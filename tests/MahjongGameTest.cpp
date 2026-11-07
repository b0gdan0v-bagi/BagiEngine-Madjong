#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Games/GameManager.h>
#include <Mahjong/MahjongGame.h>
#include <gtest/gtest.h>

/**
 * The game is registered and starts on its scene: the shipped SceneConfig.xml is read from the source tree (a game's content is not mounted in the
 * test environment) and the game is started through GameManager like the executable does. Needs the engine services.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    namespace {

        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

    }  // namespace

    class MahjongGameTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(ShippedSceneConfig())) << ShippedSceneConfig().string();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);  // GameManager switches to the scene of the game through CoreManager
            GameManager::GetInstance().Initialize();
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
        }

        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(MahjongGameTest, IsTheActiveGame) {
        MahjongGame* game = MahjongGame::GetActive();
        ASSERT_NE(game, nullptr);
        EXPECT_EQ(game->GetName(), "Mahjong"_intern);
        EXPECT_EQ(game->GetSceneName(), "MahjongScene"_intern);
    }

    TEST_F(MahjongGameTest, ItsSceneIsShippedAndActive) {
        const PoolString sceneName = "MahjongScene"_intern;
        ASSERT_NE(_manager->GetSceneByName(sceneName), nullptr);
        Scene* active = _manager->GetActiveScene();
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(active->GetName(), sceneName);
    }

}  // namespace BECore::Mahjong::Tests
