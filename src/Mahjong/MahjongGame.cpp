#include "MahjongGame.h"

#include <BECore/GameManager/CoreManager.h>
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

    bool MahjongGame::SetLanguage(PoolString language) {
        if (!CoreManager::GetUIStrings().HasLanguage(language)) {
            return false;
        }
        _language = language;
        ApplyLanguage();
        return true;
    }

    void MahjongGame::ApplyLanguage() const {
        UIStrings& strings = CoreManager::GetUIStrings();
        if (strings.HasLanguage(_language)) {
            strings.SetLanguage(_language);
        }
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
        ApplyLanguage();
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
