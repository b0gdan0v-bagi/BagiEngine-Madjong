#include "MahjongTableTestSupport.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>

// The table drawn again with nothing changed allocates nothing (the entry steady-frame-allocs): the layout, its input and the snapshots
// keep the capacity of their lists between draws. Only a build that counts allocations can see it (`macos-profile`).
#if BE_PROFILER_ALLOC

namespace BECore::Mahjong::Tests {

    namespace {

        /// Computer steps played before the draws: the pile and the melds have tiles, so their lists are not empty
        constexpr int kStepsIntoTheHand = 24;

    }  // namespace

    class MahjongTableAllocTest : public MahjongTableTestBase {};

    TEST_F(MahjongTableAllocTest, ATableDrawnAgainUnchangedAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        for (int step = 0; step < kStepsIntoTheHand && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        }
        ASSERT_FALSE(_game->GetMatch().Hand().Discards().empty());
        Draw();
        Draw();  // both snapshots have been filled once: from here on their lists have the capacity they need

        _renderer->ClearCalls();
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        _table->OnDraw();
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(allocations, 0u);
        EXPECT_FALSE(_renderer->calls.empty());
    }

}  // namespace BECore::Mahjong::Tests

#endif
