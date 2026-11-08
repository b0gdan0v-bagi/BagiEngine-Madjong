#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The hints of the table (`Table/Hints.h`): the node `Hints` of `config/SceneConfig.xml` carries this component. It reads the
     *        game and the viewer's view and never changes the match.
     *
     * **The switch.** The panel `Setup` has the row `HintsRow`: a `UISegmented` bound to the variable `hintsChoice` (`on` | `off`) whose
     * buttons emit `MahjongSetHints` with that value. The flag lives on the game (`MahjongGame::HintsOn()`), is not part of the settings
     * and takes effect at once (unlike the seats, nothing waits for `Start`). The component ignores the action while `Setup` is not open (a
     * click that arrives late), accepts only `on` and `off` and sets `hintsChoice` from the game while the panel is open and after each
     * click, so the variable keeps the truth.
     */
    class MahjongHintsComponent : public IComponent {
        BE_CLASS(MahjongHintsComponent)
    public:
        MahjongHintsComponent() = default;
        ~MahjongHintsComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// The attached component, if any; the game assumes a single one.
        static MahjongHintsComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        /// Sets the variable `hintsChoice` to the flag of the game.
        static void PublishChoice(const MahjongGame& game);

        static MahjongHintsComponent* s_active;
    };

}  // namespace BECore::Mahjong
