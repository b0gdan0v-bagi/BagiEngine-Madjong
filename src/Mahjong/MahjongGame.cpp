#include "MahjongGame.h"

#include <Games/GameManager.h>
#include <Generated/MahjongGame.gen.hpp>

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

}  // namespace BECore::Mahjong
