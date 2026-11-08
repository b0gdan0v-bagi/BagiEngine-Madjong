#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Table/Hints.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The hints of the table (`Table/Hints.h`): the node `Hints` of `config/SceneConfig.xml` carries this component and the canvas it
     *        fills. It reads the game and the viewer's view and never changes the match.
     *
     * **The switch.** The panel `Setup` has the row `HintsRow`: a `UISegmented` bound to the variable `hintsChoice` (`on` | `off`) whose
     * buttons emit `MahjongSetHints` with that value. The flag lives on the game (`MahjongGame::HintsOn()`), is not part of the settings
     * and takes effect at once (unlike the seats, nothing waits for `Start`). The component ignores the action while `Setup` is not open (a
     * click that arrives late), accepts only `on` and `off` and sets `hintsChoice` from the game while the panel is open and after each
     * click, so the variable keeps the truth.
     *
     * **The waiting row.** While `HintsApply` holds (hints on, `Setup` closed, a person at the table, no cover, the hand being played) and
     * the viewer's hand waits, the canvas shows the caption and a cell per tile (`WaitCell0..12`: the picture `WaitTile<i>`, whose sprite
     * is `Tile_<notation>`, and the count of the unseen copies in the variable `hintCount<i>`); otherwise it shows nothing. The row is
     * worked out from `MakeTableView(hand, viewer)` only when the session version, the viewer or the rule `HintsApply` changed, and each
     * frame pushes only what differs from what is on the canvas. It also shows during the computers' turns.
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
        /// Works the row out again when the table changed (else keeps it) and puts the difference on the canvas.
        void PublishWaitingRow(const MahjongGame* game);
        void PushWaitingRow();

        eastl::vector<WaitEntry> _row;        ///< the row as it should be on screen
        eastl::vector<WaitEntry> _published;  ///< the row as it is on the canvas
        bool _rowKnown = false;               ///< `_row` was worked out for the key below
        bool _rowApplies = false;
        Seat _rowViewer = kNoSeat;
        uint64_t _rowVersion = 0;

        static MahjongHintsComponent* s_active;
    };

}  // namespace BECore::Mahjong
