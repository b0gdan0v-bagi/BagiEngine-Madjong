#pragma once

#include <Mahjong/Game/TableSession.h>
#include <TaskSystem/CancellationToken.h>

namespace BECore::Mahjong {

    /// What the owner and the task share (`ComputerThinking.cpp`).
    struct ComputerThinkingState;

    /**
     * @brief Runs the search of one `ComputerStepJob` as a background task, so a table can keep drawing while a computer seat thinks.
     *
     * Main-thread API: the owner `Start`s a job, polls `TakeFinished()` once a frame and `Drop`s the run when the answer is no longer
     * wanted. Nothing blocks: no `Wait()` (it deadlocks in the single-thread build, where the first slice runs inside `Start`).
     *
     * **The one rule: the coroutine captures only its shared state** (result slot, done flag, token) and its own copy of the job, never
     * `this`, the session or the game. A task that outlives its owner, or was dropped, ends by itself and frees its frame. Every `Start`
     * makes a state of its own, so the answer of a dropped or replaced run is never handed out, also when it finished at that moment.
     * The task stores the done flag with release, `TakeFinished` loads it with acquire.
     */
    class ComputerThinking {
    public:
        ComputerThinking();
        /// Drops the run (`Drop`).
        ~ComputerThinking();
        ComputerThinking(const ComputerThinking&) = delete;
        ComputerThinking& operator=(const ComputerThinking&) = delete;

        /// Drops any earlier run, then takes `job`: a `Done()` job is finished at once with no task. Returns whether a task was started.
        bool Start(ComputerStepJob job);

        /// A run was started and its answer is not ready yet.
        [[nodiscard]] bool Running() const;

        /// The finished job, once; nullopt while it runs, when nothing was started and after `Drop`.
        [[nodiscard]] Optional<ComputerStepJob> TakeFinished();

        /// Cancels the run: the task ends at its next check.
        void Drop();

        /// The tasks of this process whose state is not freed yet; a test polls it to see that a dropped run really ended.
        [[nodiscard]] static int TasksAlive();

    private:
        IntrusivePtr<ComputerThinkingState> _state;
    };

}  // namespace BECore::Mahjong
