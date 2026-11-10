#pragma once

#include <Mahjong/Game/TableSession.h>
#include <TaskSystem/CancellationToken.h>

namespace BECore::Mahjong {

    /// What the owner and the task share (`ComputerThinking.cpp`).
    struct ComputerThinkingState;

    /**
     * @brief Runs the search of one `ComputerStepJob` as a background task, so a table can keep drawing while a computer seat thinks.
     *
     * Main-thread API: the owner (the table component) `Start`s a job, polls `TakeFinished()` once a frame and `Drop`s the run when
     * the answer is no longer wanted. Nothing here blocks or spins; a finished answer is found by polling a flag.
     *
     * - The task is a `Task<void>` on the engine's pool. Its loop checks the `CancellationToken`, then `co_await YieldIfNeeded()`
     *   (free on a threaded build; in the single-thread build, the browser, it keeps the page drawing), then plays one rollout
     *   (`ComputerStepJob::Step`). The job's `SearchRun` holds copies only, so the loop touches no session, model, rng or scene.
     * - **The one rule: the coroutine captures only its shared state** (a reference counted block: the result slot, the done flag and
     *   the token) and its own copy of the job, never `this`, the session or the game. A task that outlives its owner, or one that was
     *   dropped, ends by itself and frees its frame.
     * - Publication: the task writes the finished job, then stores the done flag with release; `TakeFinished` loads it with acquire,
     *   then reads the job. Every `Start` makes a state of its own, so an answer of a dropped or replaced run can never be handed out,
     *   also when it finished at that very moment.
     * - A job that needs no search (`Done()` at the start) starts no task: it is finished at once and `TakeFinished` gives it back.
     */
    class ComputerThinking {
    public:
        ComputerThinking();
        /// Drops the run (`Drop`).
        ~ComputerThinking();
        ComputerThinking(const ComputerThinking&) = delete;
        ComputerThinking& operator=(const ComputerThinking&) = delete;

        /// Drops any earlier run, then takes `job`: a job that is `Done()` is finished at once with no task, any other is run in the
        /// background. Returns whether a task was started. In the single-thread build the first slice runs inside this call.
        bool Start(ComputerStepJob job);

        /// A run was started and its answer is not ready yet (false after `Drop`, after `TakeFinished` and for a finished run).
        [[nodiscard]] bool Running() const;

        /// The finished job, once; nullopt while it runs, when nothing was started and after `Drop`. Poll it in `OnUpdate`.
        [[nodiscard]] std::optional<ComputerStepJob> TakeFinished();

        /// Cancels the run: the task ends at its next check and nothing of it is ever handed out.
        void Drop();

        /// The background tasks started in this process whose shared state is not yet freed (a task that ended or was dropped and has
        /// freed its frame is not counted). Reads the same from any thread; what a test polls to see that a dropped run really ended.
        [[nodiscard]] static int TasksAlive();

    private:
        IntrusivePtr<ComputerThinkingState> _state;
    };

}  // namespace BECore::Mahjong
