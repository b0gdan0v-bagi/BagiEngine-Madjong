#include "MahjongGame.h"

#include <Games/GameManager.h>
#include <Generated/MahjongGame.gen.hpp>
#include <random>

namespace BECore::Mahjong {

    MahjongGame* MahjongGame::GetActive() {
        auto* active = GameManager::GetInstance().GetActive();
        return active ? active->Cast<MahjongGame>() : nullptr;
    }

    PoolString MahjongGame::GetName() const {
        return "Mahjong"_intern;
    }

    PoolString MahjongGame::GetSceneName() const {
        return "MahjongScene"_intern;
    }

    IControllableGame* MahjongGame::AsControllable() {
        return &_control;
    }

    void MahjongGame::Start() {
        // A match is dealt at once so that the table is never empty, and the person is asked who plays before it goes on
        StartRandomMatch();
        OpenSetup();
    }

    void MahjongGame::Reset() {
        StartRandomMatch();
    }

    MatchStatus MahjongGame::StartMatch(uint32_t seed) {
        const MatchStatus started = _session.StartMatch(_settings, seed, _seats);
        if (started.has_value()) {
            CloseSetup();
        }
        return started;
    }

    MatchStatus MahjongGame::StartRandomMatch() {
        std::random_device device;
        const MatchStatus started = StartMatch(device());
        if (!started.has_value()) {
            LOG_ERROR(Format("MahjongGame: the match did not start: {}", EnumUtils<MatchError>::ToString(started.error())));
        }
        return started;
    }

}  // namespace BECore::Mahjong
