#pragma once

#include <Mahjong/Game/ComputerSearch.h>
#include <condition_variable>
#include <thread>

namespace BECore::Mahjong::Arena {

    /**
     * @brief A pool that plays the indexes of one loop on several threads: the `WorldLoop` of `SearchRun::RunWorlds` for the tools.
     *
     * `WorldThreads(n)` starts `n - 1` threads and counts the caller as the n-th; they live until the destructor joins them, because a
     * hand has hundreds of search decisions and a thread per decision would cost more than it saves. `ForEach(count, body)` hands the
     * indexes `0 .. count - 1` out by an atomic counter, runs `body(i)` once for each on any of the threads and returns when all are
     * done; what `body(i)` wrote is visible to the caller after it returns. With one thread it is a plain loop and nothing is started.
     *
     * One `ForEach` at a time: the pool belongs to one caller (a seed thread of `RunArena`) and is not safe to call from two threads
     * at once, nor from inside a `body`. The body must not throw (the engine builds without exceptions, so nothing escapes a worker).
     */
    class WorldThreads {
    public:
        /// `threads` below 1 is 1.
        explicit WorldThreads(int threads);
        ~WorldThreads();

        WorldThreads(const WorldThreads&) = delete;
        WorldThreads& operator=(const WorldThreads&) = delete;

        /// The threads that play, the caller included.
        [[nodiscard]] int Threads() const noexcept;

        void ForEach(int count, const std::function<void(int index)>& body);

        /// `ForEach` as a `WorldLoop`; valid while this pool lives.
        [[nodiscard]] WorldLoop AsLoop();

    private:
        void Helper();
        void TakeIndexes(const std::function<void(int index)>& body, int count);

        eastl::vector<std::thread> _helpers;
        std::mutex _mutex;
        std::condition_variable _wake;
        std::condition_variable _finished;
        /// The job of the current generation; written by `ForEach` under the mutex, read by the helpers after they wake.
        const std::function<void(int index)>* _body = nullptr;
        int _count = 0;
        std::atomic<int> _next{0};
        uint64_t _generation = 0;
        /// The helpers that have not yet finished the current generation (each helper takes part in every one).
        int _busy = 0;
        bool _stop = false;
    };

}  // namespace BECore::Mahjong::Arena
