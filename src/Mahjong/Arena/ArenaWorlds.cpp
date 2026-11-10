#include "ArenaWorlds.h"

namespace BECore::Mahjong::Arena {

    WorldThreads::WorldThreads(int threads) {
        for (int thread = 1; thread < threads; ++thread) {
            _helpers.emplace_back([this] { Helper(); });
        }
    }

    WorldThreads::~WorldThreads() {
        {
            std::lock_guard lock(_mutex);
            _stop = true;
        }
        _wake.notify_all();
        for (std::thread& helper : _helpers) {
            helper.join();
        }
    }

    int WorldThreads::Threads() const noexcept {
        return static_cast<int>(_helpers.size()) + 1;
    }

    void WorldThreads::TakeIndexes(const std::function<void(int index)>& body, int count) {
        for (int index = _next.fetch_add(1); index < count; index = _next.fetch_add(1)) {
            body(index);
        }
    }

    void WorldThreads::Helper() {
        uint64_t seen = 0;
        for (;;) {
            std::unique_lock lock(_mutex);
            _wake.wait(lock, [&] { return _stop || _generation != seen; });
            if (_stop) {
                return;
            }
            seen = _generation;
            const std::function<void(int index)>& body = *_body;
            const int count = _count;
            lock.unlock();
            TakeIndexes(body, count);
            lock.lock();
            if (--_busy == 0) {
                _finished.notify_one();
            }
        }
    }

    void WorldThreads::ForEach(int count, const std::function<void(int index)>& body) {
        if (_helpers.empty() || count <= 1) {
            for (int index = 0; index < count; ++index) {
                body(index);
            }
            return;
        }
        {
            std::lock_guard lock(_mutex);
            _body = &body;
            _count = count;
            _next.store(0);
            _busy = static_cast<int>(_helpers.size());
            ++_generation;
        }
        _wake.notify_all();
        TakeIndexes(body, count);
        std::unique_lock lock(_mutex);
        _finished.wait(lock, [&] { return _busy == 0; });
        _body = nullptr;
    }

    WorldLoop WorldThreads::AsLoop() {
        return [this](int count, const std::function<void(int index)>& body) { ForEach(count, body); };
    }

}  // namespace BECore::Mahjong::Arena
