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
     *
     * **The marks and the advice.** On the viewer's turn the component draws over the hand of the table (it reads the layout of the last
     * draw of `MahjongTableComponent`, which draws first): a bar under every tile whose discard declares the calling hand
     * (`DeclaringDiscards`, exactly what the `Declare` switch allows). The button `HintButton` is shown exactly while hints apply and the
     * viewer has actions to choose from; `MahjongHintAdvice` (ignored when the button is not offered) stores `AdviceFor(view)` and the
     * component publishes the sentence `AdviceText` as the variable `hintAdvice` for the label `AdviceText`; a discard that is advised is
     * framed on the hand. The advice belongs to the table it was asked on: any change of the session `Version()`, a cover, hints off or
     * `Setup` open drop it, and a new language words it again. **It never acts:** it performs no action, moves no version and the
     * control API does not know it.
     */
    class MahjongHintsComponent : public IComponent {
        BE_CLASS(MahjongHintsComponent)
    public:
        /// A bar under a tile whose discard declares the calling hand.
        static constexpr Color kDeclareMark{90, 220, 120, 255};
        /// The frame on the tile the computer would discard.
        static constexpr Color kAdvisedFrame{80, 200, 255, 255};

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

        /// What the hints depend on: when it is equal to the last frame's, nothing needs to be worked out again.
        struct Key {
            bool applies = false;  ///< `HintsApply`
            Seat viewer = kNoSeat;
            uint64_t version = 0;  ///< `TableSession::Version()`
            bool operator==(const Key&) const = default;
        };
        [[nodiscard]] static Key KeyOf(const MahjongGame* game);
        /// The view of the viewer when the hints apply and the viewer is to act with something to choose from, else nullopt.
        [[nodiscard]] static std::optional<TableView> ViewToAdvise(const MahjongGame* game);

        /// Works the row, the marks and the button out again when the table changed (else keeps them).
        void Refresh(const MahjongGame* game, const Key& key);
        /// Puts the difference between the row and the canvas on the canvas.
        void PushWaitingRow();
        /// The button and the sentence of the advice.
        void PushAdvice();
        void DropAdvice();
        void DrawMarks(const MahjongGame& game) const;

        eastl::vector<WaitEntry> _row;                  ///< the row as it should be on screen
        eastl::vector<WaitEntry> _published;            ///< the row as it is on the canvas
        eastl::array<bool, kTileIdCount> _declaring{};  ///< the tiles whose discard declares the calling hand
        bool _offered = false;                          ///< the button is offered: hints apply and the viewer has actions
        bool _offeredShown = false;
        bool _known = false;  ///< `_key` and what follows from it are worked out
        Key _key;

        std::optional<HandAction> _advice;  ///< the advice that was asked for on the table of `_adviceKey`
        TableView _adviceView;              ///< the view it was asked on (the sentence names the pending discard of a window)
        Key _adviceKey;
        bool _adviceDirty = false;  ///< the sentence is to be published again (asked again)
        bool _adviceShown = false;
        uint64_t _adviceStringsVersion = 0;

        static MahjongHintsComponent* s_active;
    };

}  // namespace BECore::Mahjong
