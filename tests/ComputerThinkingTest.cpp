#include <Common/EngineEnvironment.h>
#include <Mahjong/ComputerThinking.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/Match.h>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

/**
 * `ComputerThinking` (`Mahjong/ComputerThinking.h`): a search job run as a background task and polled for. The jobs are made by a
 * `TableSession` that is gone before they run, so the task is shown to touch no session. A wait is a poll with a limit. Under
 * `BE_SINGLE_THREAD` the job is done inside `Start` (no frame has begun, so `YieldIfNeeded()` never parks); the cases that need a
 * parked task are skipped there. Needs the engine services.
 */
namespace BECore::Mahjong::Tests {

    using BECore::Tests::EngineEnvironment;

    namespace {

        constexpr uint32_t kSeed = 31;
        constexpr int kStepLimit = 600;
        /// A passing test never waits this long.
        constexpr auto kWaitLimit = std::chrono::seconds(30);
        constexpr auto kPollPause = std::chrono::milliseconds(1);
        /// The longest a dropped run of `kSearchMaxWorlds` worlds may take to end: it checks its token before every rollout.
        constexpr auto kDropLimit = std::chrono::seconds(10);

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

        LevelProfile SearchProfile(int worlds) {
            LevelProfile profile = kNormalProfile;
            profile.searchWorlds = worlds;
            profile.searchCandidates = 2;
            return profile;
        }

        /// The first step of a table of computers that is a search (the ready ones are applied on the way), begun and handed over: the
        /// session and the match that made it are destroyed on return. With `ready` the first step that needs no search instead.
        ComputerStepJob FirstJob(const LevelProfile& profile, uint32_t seed, bool ready = false) {
            Match match;
            TableSession session(match);
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            const SeatSetup seats{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
            EXPECT_TRUE(session.StartMatch(settings, seed, seats, profile).has_value());
            for (int step = 0; step < kStepLimit; ++step) {
                std::optional<ComputerStepJob> job = session.BeginComputerStep();
                if (!job) {
                    break;
                }
                if (job->Ready() == ready) {
                    return *job;
                }
                job->Run();
                EXPECT_TRUE(session.FinishComputerStep(*job).has_value());
            }
            ADD_FAILURE() << "no step of the wanted kind";
            return ComputerStepJob{};
        }

        /// What the job's action is when it is run at once, on a copy.
        HandAction AtOnce(const ComputerStepJob& job) {
            ComputerStepJob copy = job;
            copy.Run();
            return copy.Action();
        }

        std::optional<ComputerStepJob> Poll(ComputerThinking& thinking) {
            std::optional<ComputerStepJob> finished;
            EXPECT_TRUE(WaitUntil([&] {
                finished = thinking.TakeFinished();
                return finished.has_value();
            })) << "the job never finished";
            return finished;
        }

        class ComputerThinkingTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void TearDown() override {
                EXPECT_TRUE(WaitUntil([] { return ComputerThinking::TasksAlive() == 0; }, kDropLimit)) << "a task of this case did not end";
            }
        };

    }  // namespace

    TEST_F(ComputerThinkingTest, ASearchJobGivesInTheBackgroundTheActionOfTheSameJobRunAtOnce) {
        for (uint32_t seed = kSeed; seed < kSeed + 3; ++seed) {
            const ComputerStepJob job = FirstJob(SearchProfile(1), seed);
            ASSERT_FALSE(job.Ready());
            ComputerThinking thinking;
            EXPECT_TRUE(thinking.Start(job));
#if BE_SINGLE_THREAD
            EXPECT_FALSE(thinking.Running()) << "the first slice runs inside Start and nothing parks before the first frame";
#endif
            const std::optional<ComputerStepJob> finished = Poll(thinking);
            ASSERT_TRUE(finished.has_value());
            EXPECT_TRUE(finished->Done());
            EXPECT_EQ(finished->Actor(), job.Actor());
            EXPECT_EQ(finished->Version(), job.Version());
            EXPECT_EQ(finished->StartCount(), job.StartCount());
            EXPECT_TRUE(finished->Action() == AtOnce(job)) << "seed " << seed;
            EXPECT_FALSE(thinking.Running());
            EXPECT_FALSE(thinking.TakeFinished().has_value()) << "a finished job is handed out once";
        }
    }

    TEST_F(ComputerThinkingTest, AReadyJobMakesNoTask) {
        const ComputerStepJob job = FirstJob(SearchProfile(1), kSeed, true);
        ASSERT_TRUE(job.Ready());
        ComputerThinking thinking;
        const int before = ComputerThinking::TasksAlive();
        EXPECT_FALSE(thinking.Start(job));
        EXPECT_EQ(ComputerThinking::TasksAlive(), before);
        EXPECT_FALSE(thinking.Running());
        const std::optional<ComputerStepJob> finished = thinking.TakeFinished();
        ASSERT_TRUE(finished.has_value());
        EXPECT_TRUE(finished->Action() == job.Action());
    }

    TEST_F(ComputerThinkingTest, NothingStartedHandsOutNothing) {
        ComputerThinking thinking;
        EXPECT_FALSE(thinking.Running());
        EXPECT_FALSE(thinking.TakeFinished().has_value());
        thinking.Drop();
        EXPECT_FALSE(thinking.TakeFinished().has_value());
    }

    TEST_F(ComputerThinkingTest, ADroppedFinishedJobIsNotHandedOut) {
        const ComputerStepJob job = FirstJob(SearchProfile(1), kSeed);
        ComputerThinking thinking;
        ASSERT_TRUE(thinking.Start(job));
        ASSERT_TRUE(WaitUntil([&] { return !thinking.Running(); })) << "the job never finished";
        thinking.Drop();
        EXPECT_FALSE(thinking.Running());
        EXPECT_FALSE(thinking.TakeFinished().has_value());
    }

    TEST_F(ComputerThinkingTest, ADropBeforeTheEndHandsOutNothingAndTheTaskEndsSoon) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "needs a task that is parked (BE_SINGLE_THREAD runs it inside Start)";
#endif
        const ComputerStepJob job = FirstJob(SearchProfile(kSearchMaxWorlds), kSeed);
        ASSERT_FALSE(job.Ready());
        ComputerThinking thinking;
        ASSERT_TRUE(thinking.Start(job));
        EXPECT_TRUE(thinking.Running());
        EXPECT_EQ(ComputerThinking::TasksAlive(), 1);
        thinking.Drop();
        EXPECT_FALSE(thinking.Running());
        EXPECT_FALSE(thinking.TakeFinished().has_value());
        EXPECT_TRUE(WaitUntil([] { return ComputerThinking::TasksAlive() == 0; }, kDropLimit)) << "the dropped task did not end";
    }

    TEST_F(ComputerThinkingTest, StartTwiceHandsOutOnlyTheSecond) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "needs a task that is parked (BE_SINGLE_THREAD runs it inside Start)";
#endif
        const ComputerStepJob slow = FirstJob(SearchProfile(kSearchMaxWorlds), kSeed);
        const ComputerStepJob quick = FirstJob(SearchProfile(1), kSeed + 1);
        ComputerThinking thinking;
        ASSERT_TRUE(thinking.Start(slow));
        ASSERT_TRUE(thinking.Start(quick));
        const std::optional<ComputerStepJob> finished = Poll(thinking);
        ASSERT_TRUE(finished.has_value());
        EXPECT_EQ(finished->Actor(), quick.Actor());
        EXPECT_TRUE(finished->Action() == AtOnce(quick));
        EXPECT_FALSE(thinking.TakeFinished().has_value());
        EXPECT_TRUE(WaitUntil([] { return ComputerThinking::TasksAlive() == 0; }, kDropLimit)) << "the replaced task did not end";
    }

    TEST_F(ComputerThinkingTest, ARunnerDestroyedWhileItRunsLeavesNothingBehind) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "needs a task that is parked (BE_SINGLE_THREAD runs it inside Start)";
#endif
        const ComputerStepJob job = FirstJob(SearchProfile(kSearchMaxWorlds), kSeed);
        auto thinking = std::make_unique<ComputerThinking>();
        ASSERT_TRUE(thinking->Start(job));
        EXPECT_TRUE(thinking->Running());
        thinking.reset();
        EXPECT_TRUE(WaitUntil([] { return ComputerThinking::TasksAlive() == 0; }, kDropLimit)) << "the task outlived its runner and did not end";
    }

    TEST_F(ComputerThinkingTest, ASecondStartAfterAFinishedOneWorks) {
        ComputerThinking thinking;
        for (uint32_t seed = kSeed; seed < kSeed + 2; ++seed) {
            const ComputerStepJob job = FirstJob(SearchProfile(1), seed);
            ASSERT_TRUE(thinking.Start(job));
            const std::optional<ComputerStepJob> finished = Poll(thinking);
            ASSERT_TRUE(finished.has_value());
            EXPECT_TRUE(finished->Action() == AtOnce(job));
        }
    }

}  // namespace BECore::Mahjong::Tests
