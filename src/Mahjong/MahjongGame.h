#pragma once

#include <Games/IGame.h>

namespace BECore::Mahjong {

    /**
     * @brief Concrete IGame for mahjong (a skeleton: no rules yet).
     *
     * Activates "MahjongScene" when started. Auto-registered into AbstractFactory<IGame> via the BE_CLASS reflection machinery, no hand-written
     * registrar. The rules will live in plain C++ under Game/ and be owned here; the scene glue in Components/ will reach them through GetActive().
     */
    class MahjongGame : public IGame {
        BE_CLASS(MahjongGame)
    public:
        MahjongGame() = default;
        ~MahjongGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        /// The active game, or nullptr when no game is active or the active game is not Mahjong.
        static MahjongGame* GetActive();
    };

}  // namespace BECore::Mahjong
