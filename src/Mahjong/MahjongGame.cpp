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

    void MahjongGame::Start() {
        StartRandomMatch();
    }

    void MahjongGame::Reset() {
        StartRandomMatch();
    }

    MatchStatus MahjongGame::StartMatch(uint32_t seed) {
        return _match.Start(MahjongSettings{}, seed);
    }

    void MahjongGame::StartRandomMatch() {
        std::random_device device;
        if (const MatchStatus started = StartMatch(device()); !started.has_value()) {
            LOG_ERROR(Format("MahjongGame: the match did not start: {}", EnumUtils<MatchError>::ToString(started.error())).c_str());
        }
    }

}  // namespace BECore::Mahjong
