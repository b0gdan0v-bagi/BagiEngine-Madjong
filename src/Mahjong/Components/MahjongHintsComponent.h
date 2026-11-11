#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Table/Coach.h>
#include <Mahjong/Table/DangerMarks.h>
#include <Mahjong/Table/Hints.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SDLEvents {
        struct MouseButtonDownEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The hints of the table (`Table/Hints.h`): the node `Hints` (`config/nodes/MahjongHints.xml`) carries this component and the canvas it
     *        fills. It reads the game and the viewer's view and never changes the match.
     *
     * **The switch.** The page `Options` has the row `HintsRow`: a `UISegmented` bound to the variable `hintsChoice` (`off` | `hints` | `coach`,
     * Coach includes the hints) whose buttons emit `MahjongSetHints` with that value. The mode lives on the game (`MahjongGame::GetHintsMode()`),
     * is not part of the settings and takes effect at once (unlike the seats, nothing waits for `Start`). The component ignores the action
     * while the page is not open (a click that arrives late), accepts only those three ids and sets `hintsChoice` from the game while the page is open and after each
     * click, so the variable keeps the truth.
     *
     * **The waiting row.** While `HintsApply` holds (hints on, neither `Setup` nor the rules open, a person at the table, no cover, the hand being played) and
     * the viewer's hand waits, the canvas shows the caption and a cell per tile (`WaitCell0..12`: the picture `WaitTile<i>`, whose sprite
     * is `Tile_<notation>`, and the count of the unseen copies in the variable `hintCount<i>`); otherwise it shows nothing. The row is
     * worked out from `MakeTableView(hand, viewer)` only when the session version, the viewer or the rule `HintsApply` changed, and each
     * frame pushes only what differs from what is on the canvas. It also shows during the computers' turns.
     *
     * **The marks and the advice.** On the viewer's turn the component draws over the hand of the table (it reads the layout of the last
     * draw of `MahjongTableComponent`, which draws first): a bar under every tile whose discard declares the calling hand
     * (`DeclaringDiscards`, exactly what the `Declare` switch allows) and, while an opponent has declared, a bar inside the left edge of each hand tile that is safe to
     * discard (green), risky (amber) or dangerous (red) (`DangerMarks`; an ordinary tile has none). The button `HintButton` (in the nodes `HintsButton` on the table and `ActionsCompact` in the narrow
     * windows, shown by name in both, `MahjongTableComponent` swaps the canvases) is shown exactly while hints apply and the viewer has actions to choose from; `MahjongHintAdvice` (ignored when the
     * button is not offered) stores `AdviceFor(view)` and the component publishes the sentence `AdviceText` as the variable `hintAdvice` for the label `AdviceText`; a discard that is advised is
     * framed on the hand. A second press while that advice is up opens the reasons (`ExplainAdviceFor`, `ReasonLines`) in the panel
     * `WhyPanel` of the node `HintsWhy` (`config/nodes/MahjongHintsWhy.xml`, on the wall ring, text only, no input), as the variables `hintWhy0..5`; a third
     * press closes it. The advice belongs to the table it was asked on: any change of the session `Version()`, a cover, hints off or
     * `Setup` open drop it and the reasons with it, and a new language words both again.
     *
     * **The coach's note.** In the mode Coach the ledger of the game (`MahjongGame::GetCoach()`, fed by the session) holds the note of the
     * person's last differing move; the panel `CoachPanel` of the same node shows its lines (`CoachNoteLines`, the variables `coachNote0..2`)
     * exactly while the hints apply, the note is of this hand and of the viewer. The note stays through the computers' turns (they move within a
     * second of the person's action) and goes with the next accepted action of the person (the ledger), a left or touch mouse-down that the
     * panel was up for, the press of `Hint`, the end of the hand, a new match, a pause, a cover and a change of the mode. The component only
     * reads the ledger and clears it, and publishes only when the note, the language or the shown state changed. **It never acts:** it performs no action, moves no version and the
     * control API does not know it.
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
        void OnMouseDown(const SDLEvents::MouseButtonDownEvent& event);
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
        /// The panel of the reasons (`HintsWhy`): the lines and the plate while `open`, empty variables and no plate otherwise.
        void PushWhy(bool open);
        /// The note of the coach: drops what no longer holds (and clears the ledger), publishes the lines and the plate when they changed.
        void PushCoach(MahjongGame* game, const Key& key);
        void DropAdvice();
        void DrawMarks(const MahjongGame& game) const;

        eastl::vector<WaitEntry> _row;                      ///< the row as it should be on screen
        eastl::vector<WaitEntry> _published;                ///< the row as it is on the canvas
        eastl::array<bool, kTileIdCount> _declaring{};      ///< the tiles whose discard declares the calling hand
        eastl::array<DangerMark, kTileIdCount> _dangers{};  ///< the danger of each tile of the hand while an opponent has declared
        bool _offered = false;                              ///< the button is offered: hints apply and the viewer has actions
        bool _offeredShown = false;
        bool _known = false;  ///< `_key` and what follows from it are worked out
        Key _key;

        std::optional<HandAction> _advice;  ///< the advice that was asked for on the table of `_adviceKey`
        TableView _adviceView;              ///< the view it was asked on (the sentence names the pending discard of a window)
        Key _adviceKey;
        std::optional<Explanation> _explained;  ///< the reasons of `_advice`, asked on the same table
        bool _whyOpen = false;                  ///< a second press opened the reasons, a third closes them
        bool _whyDirty = false;                 ///< the lines are to be published again (opened)
        bool _whyShown = false;
        uint64_t _whyStringsVersion = 0;
        bool _adviceDirty = false;  ///< the sentence is to be published again (asked again)
        bool _adviceShown = false;
        uint64_t _adviceStringsVersion = 0;

        bool _coachShown = false;
        const MahjongGame* _coachOf = nullptr;  ///< the game whose note was last looked at
        uint64_t _coachSeen = 0;                ///< `CoachLedger::Version()` of the note last looked at
        CoachHandKey _coachHand;                ///< the hand that note was first seen in
        uint64_t _coachPublished = 0;           ///< the version whose words are in the variables
        uint64_t _coachStringsVersion = 0;

        static MahjongHintsComponent* s_active;
    };

}  // namespace BECore::Mahjong
