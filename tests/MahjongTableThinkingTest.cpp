#include "MahjongTableTestSupport.h"

#include <Events/SceneEvents.h>
#include <Mahjong/ComputerThinking.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <chrono>
#include <thread>

/**
 * A search seat thinks in the background on the table (`Components/MahjongTableComponent.h`, "Pace"; the runner is
 * `Mahjong/ComputerThinking.h`): the table keeps drawing meanwhile, the answer is applied at the pause end or when it arrives, and
 * nothing stale is applied. The tables are started through the session with a search `LevelProfile` (no level turns the search on),
 * on a seed whose first decision is a search. A wait is a poll with a limit; the cases that need a parked task are skipped under
 * `BE_SINGLE_THREAD`, where the job is done inside `Start`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kSeedsToSearch = 300;
        constexpr int kWorldsQuick = 1;
        constexpr int kWorldsSlow = kSearchMaxWorlds;
        /// A passing test never waits this long.
        constexpr auto kWaitLimit = std::chrono::seconds(60);
        constexpr auto kPollPause = std::chrono::milliseconds(1);
        constexpr float kMostOf = 0.9f;
        constexpr float kTheRest = 0.2f;
        constexpr float kFrame = 0.01f;

        template <class Condition>
        bool WaitUntil(Condition condition, std::chrono::steady_clock::duration limit = kWaitLimit) {
            const auto end = std::chrono::steady_clock::now() + limit;
            while (!condition()) {
                if (std::chrono::steady_clock::now() > end) {
                    return false;
                }
                std::this_thread::sleep_for(kPollPause);
            }
            return true;
        }

        void Elapse(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

        LevelProfile SearchProfile(int worlds) {
            LevelProfile profile = kNormalProfile;
            profile.searchWorlds = worlds;
            profile.searchCandidates = 2;
            return profile;
        }

        /// The first seed whose first step of a table of computers is a search (the one the table begins in its first frame).
        uint32_t SearchSeed(const MahjongSettings& settings, const LevelProfile& profile) {
            for (uint32_t seed = 1; seed <= kSeedsToSearch; ++seed) {
                Match match;
                TableSession session(match);
                EXPECT_TRUE(session.StartMatch(settings, seed, kAllComputers, profile).has_value());
                const std::optional<ComputerStepJob> job = session.BeginComputerStep();
                if (job && !job->Ready()) {
                    return seed;
                }
            }
            ADD_FAILURE() << "no seed whose first decision is a search";
            return 1;
        }

    }  // namespace

    class MahjongTableThinkingTest : public MahjongTableTestBase {
    protected:
        void TearDown() override {
            MahjongTableTestBase::TearDown();  // the table leaves the scene: its runner drops the task, which ends by itself
            EXPECT_TRUE(WaitUntil([] { return ComputerThinking::TasksAlive() == 0; })) << "a task of this case did not end";
        }

        /// A table of computers by the profile on a seed that starts with a search, drawn.
        void StartSearchTable(int worlds, AnimationSpeed speed = AnimationSpeed::Normal) {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));  // closes `Setup`, as a person's start does
            _game->SetAnimationSpeed(speed);
            const LevelProfile profile = SearchProfile(worlds);
            _seed = SearchSeed(_game->GetSettings(), profile);
            ASSERT_TRUE(_game->GetSession().StartMatch(_game->GetSettings(), _seed, kAllComputers, profile).has_value());
            Draw();
        }

        float Pause() const {
            return ScaledSeconds(kComputerStepSeconds, _game->GetAnimationSpeed());
        }

        uint64_t Version() const {
            return _game->GetSession().Version();
        }

        /// What `StepComputer` of a session of the same seed and profile does `steps` times.
        std::string TwinAfter(uint64_t steps, int worlds) const {
            Match match;
            TableSession twin(match);
            EXPECT_TRUE(twin.StartMatch(_game->GetSettings(), _seed, kAllComputers, SearchProfile(worlds)).has_value());
            for (uint64_t step = 0; step < steps; ++step) {
                EXPECT_TRUE(twin.StepComputer().has_value());
            }
            return Snapshot(match.Hand());
        }

        /// Frames of `kFrame` seconds until the version moves from `version`; the number of frames that were drawn while it did not move
        /// and a search was running.
        bool FramesUntilApplied(uint64_t version) {
            return WaitUntil([&] {
                Elapse(kFrame);
                Draw();
                return Version() != version;
            });
        }

        uint32_t _seed = 1;
    };

    /// The same, with the manual clock of a control script (the frames are `step`s).
    class MahjongTableThinkingManualClockTest : public MahjongTableThinkingTest {
    protected:
        void SetUp() override {
            MahjongTableThinkingTest::SetUp();
            CoreManager::GetControlService().SetManualClock(true);
        }

        void TearDown() override {
            CoreManager::GetControlService().SetManualClock(false);
            MahjongTableThinkingTest::TearDown();
        }
    };

    TEST_F(MahjongTableThinkingTest, TheTableKeepsDrawingAndTakingTimeWhileASearchRuns) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "the job is done inside Start in the single-thread build: nothing runs in the background";
#endif
        ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorldsSlow));
        const uint64_t version = Version();
        const std::string before = Snapshot(Hand());
        Elapse(Pause() * (kMostOf + kTheRest));  // the pause is over: only the answer is missing
        int framesWhileThinking = 0;
        for (int frame = 0; frame < 20; ++frame) {
            const bool thinking = _table->ComputerThinks();
            Elapse(kFrame);
            Draw();
            if (thinking && _table->ComputerThinks()) {
                ++framesWhileThinking;
                EXPECT_FALSE(TileCalls().empty()) << "the table is drawn";
            }
        }
        EXPECT_GT(framesWhileThinking, 0) << "the runner was running during the frames";
        EXPECT_EQ(Version(), version) << "nothing is applied before the answer";
        EXPECT_EQ(Snapshot(Hand()), before);
    }

    TEST_F(MahjongTableThinkingTest, ASearchThatEndsWithinThePauseIsAppliedAtThePauseEnd) {
        ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorldsQuick));
        const uint64_t version = Version();
        Elapse(Pause() * kMostOf);
        EXPECT_EQ(Version(), version) << "the pause is not over";
        EXPECT_TRUE(WaitUntil([&] { return !_table->ComputerThinks(); })) << "the search never ended";
        EXPECT_EQ(Version(), version) << "the answer waits for the pause end";
        Elapse(Pause() * kTheRest);
        EXPECT_GT(Version(), version) << "applied at the pause end";
    }

    TEST_F(MahjongTableThinkingTest, ASlowSearchIsAppliedWhenItArrivesAndNeverBeforeThePause) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "the job is done inside Start in the single-thread build";
#endif
        constexpr int kWorlds = 24;
        ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorlds, AnimationSpeed::Fast));
        const uint64_t version = Version();
        Elapse(Pause() * kMostOf);
        EXPECT_EQ(Version(), version) << "the pause is not over";
        Elapse(Pause() * kTheRest + kFrame);
        if (!_table->ComputerThinks()) {
            GTEST_SKIP() << "the search was over within the pause on this machine";
        }
        EXPECT_EQ(Version(), version) << "the pause is over, the answer is not";
        ASSERT_TRUE(FramesUntilApplied(version));
        EXPECT_GE(Version(), version + 1);
        EXPECT_EQ(Snapshot(Hand()), TwinAfter(Version() - version, kWorlds));
    }

    TEST_F(MahjongTableThinkingTest, TheAppliedActionIsTheOneOfStepComputer) {
        ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorldsQuick));
        const uint64_t version = Version();
        Elapse(Pause() * (kMostOf + kTheRest));
        ASSERT_TRUE(FramesUntilApplied(version));
        EXPECT_EQ(Snapshot(Hand()), TwinAfter(Version() - version, kWorldsQuick));
    }

    TEST_F(MahjongTableThinkingTest, ANewMatchDropsTheSearchAndAppliesNothingOfIt) {
        ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorldsSlow));
        Elapse(Pause() * 0.3f);
#if !BE_SINGLE_THREAD
        EXPECT_TRUE(_table->ComputerThinks());
#endif
        ASSERT_TRUE(_game->StartMatch(kSeed).has_value());  // every seat at its level: no search
        const uint64_t version = Version();
        const std::string before = Snapshot(Hand());
        Elapse(kFrame);
        EXPECT_FALSE(_table->ComputerThinks());
        EXPECT_EQ(Version(), version);
        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_TRUE(WaitUntil([] { return ComputerThinking::TasksAlive() == 0; })) << "the dropped task did not end";
    }

    TEST_F(MahjongTableThinkingTest, ASetupOrRulesScreenOverTheTableDropsTheSearch) {
        for (const bool rules : {false, true}) {
            SCOPED_TRACE(rules ? "rules" : "setup");
            ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorldsSlow));
            Elapse(Pause() * 0.3f);
            const uint64_t version = Version();
            const std::string before = Snapshot(Hand());
            if (rules) {
                _game->OpenRules();
            } else {
                _game->OpenSetup();
            }
            ASSERT_TRUE(_game->IsTablePaused());
            Elapse(Pause() * 2.0f);
            EXPECT_FALSE(_table->ComputerThinks());
            EXPECT_EQ(Version(), version) << "a paused table plays nothing";
            EXPECT_EQ(Snapshot(Hand()), before);
            EXPECT_TRUE(WaitUntil([] { return ComputerThinking::TasksAlive() == 0; })) << "the dropped task did not end";
            ASSERT_TRUE(_game->StartMatch(kSeed).has_value());  // closes the screen for the next round
        }
    }

    TEST_F(MahjongTableThinkingTest, AControlActionDropsTheSearchAndTheNextFrameBeginsAgain) {
        ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorldsQuick));  // the control step below runs the search at once: keep it cheap
        Elapse(Pause() * 0.3f);                                   // a job begun at the old version is in the air
        const uint64_t version = Version();
        ASSERT_TRUE(_game->GetSession().StepComputer().has_value());  // what a control client's action does: the version moves
        EXPECT_EQ(Version(), version + 1);
        const std::string before = Snapshot(Hand());
        Elapse(kFrame);
        EXPECT_EQ(Version(), version + 1) << "the job of the old version is not applied";
        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_TRUE(WaitUntil([&] { return !_table->ComputerThinks(); })) << "the next frame's job never ended";
        Elapse(Pause());
        EXPECT_GT(Version(), version + 1) << "the table plays on from the new state";
    }

    TEST_F(MahjongTableThinkingTest, ATableOfNormalSeatsStartsNoTask) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        const int alive = ComputerThinking::TasksAlive();
        const uint64_t version = Version();
        Elapse(Pause() * kMostOf);
        EXPECT_EQ(ComputerThinking::TasksAlive(), alive);
        EXPECT_FALSE(_table->ComputerThinks());
        Elapse(Pause() * kTheRest);
        EXPECT_GT(Version(), version) << "the step is applied at the pause end as it always was";
        EXPECT_FALSE(_table->ComputerThinks());
        EXPECT_EQ(ComputerThinking::TasksAlive(), alive);
    }

    TEST_F(MahjongTableThinkingManualClockTest, TheDecisionIsComputedAtOnceAndAppliedInTheFrameThatEndsThePause) {
        constexpr int kWorlds = 2;
        ASSERT_NO_FATAL_FAILURE(StartSearchTable(kWorlds));
        const uint64_t version = Version();
        Elapse(Pause() * kMostOf);
        EXPECT_EQ(Version(), version) << "the pause is not over";
        EXPECT_FALSE(_table->ComputerThinks()) << "no task on the manual clock";
        Elapse(Pause() * kTheRest);
        EXPECT_GT(Version(), version) << "applied in the frame that ends the pause";
        EXPECT_EQ(Snapshot(Hand()), TwinAfter(Version() - version, kWorlds));
    }

}  // namespace BECore::Mahjong::Tests
