#include "ComputerThinking.h"

#include <BECore/GameManager/CoreManager.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskManager.h>

namespace BECore::Mahjong {

    namespace {

        std::atomic<int> g_tasksAlive{0};

    }  // namespace

    /// What the owner and the task share: the only thing the coroutine captures besides its copy of the job.
    struct ComputerThinkingState : RefCountedAtomic {
        explicit ComputerThinkingState(bool hasTask) : _hasTask(hasTask) {
            if (_hasTask) {
                g_tasksAlive.fetch_add(1, std::memory_order_relaxed);
            }
        }
        ~ComputerThinkingState() override {
            if (_hasTask) {
                g_tasksAlive.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        ComputerThinkingState(const ComputerThinkingState&) = delete;
        ComputerThinkingState& operator=(const ComputerThinkingState&) = delete;

        CancellationToken token = CancellationToken::Create();
        /// Written by the task before `done` is stored with release (or by `Start` for a ready job), read after an acquire load.
        std::optional<ComputerStepJob> result;
        std::atomic<bool> done{false};

    private:
        bool _hasTask;
    };

    namespace {

        /// The loop of the background task: the token before the yield, the yield before the rollout (`Awaitables.h`, "Frame budget").
        Task<void> Think(IntrusivePtr<ComputerThinkingState> state, ComputerStepJob job) {
            while (!job.Done()) {
                if (state->token.IsCancelled()) {
                    co_return;
                }
                co_await YieldIfNeeded();
                job.Step();
            }
            if (state->token.IsCancelled()) {
                co_return;
            }
            state->result = std::move(job);
            state->done.store(true, std::memory_order_release);
        }

    }  // namespace

    ComputerThinking::ComputerThinking() = default;

    ComputerThinking::~ComputerThinking() {
        Drop();
    }

    bool ComputerThinking::Start(ComputerStepJob job) {
        Drop();
        const bool ready = job.Done();
        _state = MakeIntrusive<ComputerThinkingState>(!ready);
        if (ready) {
            _state->result = std::move(job);
            _state->done.store(true, std::memory_order_release);
            return false;
        }
        CoreManager::GetTaskManager().Run(Think(_state, std::move(job)));
        return true;
    }

    bool ComputerThinking::Running() const {
        return _state && !_state->done.load(std::memory_order_acquire);
    }

    std::optional<ComputerStepJob> ComputerThinking::TakeFinished() {
        if (!_state || !_state->done.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        std::optional<ComputerStepJob> finished = std::move(_state->result);
        _state.Reset();
        return finished;
    }

    int ComputerThinking::TasksAlive() {
        return g_tasksAlive.load(std::memory_order_relaxed);
    }

    void ComputerThinking::Drop() {
        if (_state) {
            _state->token.Cancel();
            _state.Reset();
        }
    }

}  // namespace BECore::Mahjong
