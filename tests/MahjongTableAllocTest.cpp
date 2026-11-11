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

    TEST_F(MahjongTableAllocTest, ThePointerAtRestOnATileAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        for (int step = 0; step < kStepsIntoTheHand && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        }
        Draw();
        const auto discard = std::find_if(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end(), [](const TileSlot& slot) { return slot.role == SlotRole::DiscardTile; });
        ASSERT_NE(discard, _table->GetLayout().slots.end());
        HoverAt(discard->rect.x + discard->rect.w * 0.5f, discard->rect.y + discard->rect.h * 0.5f);
        Draw();
        Draw();  // the name is worded and the plate placed; from here on nothing changes
        ASSERT_TRUE(OnScreen("TileHover", "TileHoverPlate"));

        _renderer->ClearCalls();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        _table->OnDraw();
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

}  // namespace BECore::Mahjong::Tests

#endif
