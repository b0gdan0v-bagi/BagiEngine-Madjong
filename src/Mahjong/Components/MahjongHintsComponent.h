#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Table/DangerMarks.h>
#include <Mahjong/Table/HintCard.h>

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
     * click, so the variable keeps the truth. **In a room** that choice counts only where the host allowed hints (`MahjongGame::HintsActive()` /
     * `CoachActive()`, the effective mode; a client cannot turn them on against the room), and every hint is worked out from the viewer's view of the
     * display copy with the server's list of actions as `legal` and nothing else.
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
     * windows, shown by name in both, `MahjongTableComponent` swaps the canvases) is shown exactly while hints apply and the viewer has actions to choose from.
     * It is a **switch** (`MahjongHintAdvice`, ignored when the button is not offered; the state is the component's, kept across turns, hands and
     * matches, not saved): while it is on, both buttons are drawn `selected` and every table the viewer is to act on gets `AdviceFor(view)` worked out
     * once (`ExplainAdviceFor` names the same action), published as the sentence `AdviceText` in the variable `hintAdvice` for the label `AdviceText`, a
     * discard that is advised framed on the hand. The advice belongs to the table it was worked out on: any change of the session `Version()`, a cover,
     * hints off or a pause drop it, the next table the viewer acts on gets its own, and a new language words it again.
     *
     * **The card of the coach.** In the mode Coach the sentence is not a label of its own: the advice of the table is the card `CoachCard` of the
     * node `HintsWhy` (`config/nodes/MahjongHintsWhy.xml`, on the wall ring, text and tile pictures, no input), made by `MakeAdviceCard` (the
     * sentence as its title, the advised move and the next best with their numbers, why in words, the tally). The ledger of the game
     * (`MahjongGame::GetCoach()`, fed by the session, in a room by the table component's `Perform`) holds the note of the person's last differing move; while it is up the card is the note
     * (`MakeNoteCard`: the computer's choice and the person's move) instead, exactly while the hints apply, the note is of this hand and of the
     * viewer. The note stays through the computers' turns (they move within a second of the person's action) and goes with the next accepted
     * action of the person (the ledger), a left or touch mouse-down that it was up for, a press of `Hint`, the end of the hand, a new match, a
     * pause, a cover and a change of the mode. The card is made and published only when what it says moved (`CardKey`: the advice or the note,
     * the language, the tally); every variable of it is emptied when it goes down. The component only reads the ledger and clears it. **It never
     * acts:** it performs no action, moves no version and the control API does not know it.
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

        /// The danger mark the hints draw on `tile` of the viewer's hand now (`DangerMarks`): `None` while no mark is drawn (hints off, no
        /// declared opponent, not the viewer's discard). The name plate of a hovered hand tile words it, so a bar is never unexplained.
        [[nodiscard]] DangerMark DangerOf(Tile tile) const noexcept {
            return _key.applies && tile.IsValid() ? _dangers[tile.Id()] : DangerMark::None;
        }

        /// The view of @p viewer on the table on screen, the one thing every hint is worked out from: `MakeTableView` of the displayed hand (in a
        /// room a display copy: the viewer's own tiles and what lies open, the other hands counts only). A display hand lists no action, so in a
        /// room `legal` is the server's list (`ListHandActions`) and nothing else is added; none while the link is down (`Reconnecting`).
        [[nodiscard]] static TableView ViewOf(const MahjongGame& game, Seat viewer);

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnMouseDown(const SDLEvents::MouseButtonDownEvent& event);
        /// Sets the variable `hintsChoice` to the flag of the game.
        static void PublishChoice(const MahjongGame& game);

        /// What the hints depend on: when it is equal to the last frame's, nothing needs to be worked out again.
        struct Key {
            bool applies = false;  ///< `HintsApply` of the effective mode, so a room's setting or the player's mode that moves it moves the key
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
        /// Works the advice of the viewer's table out (the switch is on and the button offered).
        void Advise(const MahjongGame& game, const Key& key);
        /// The button (offered, switched on) and the sentence of the advice, which is a label of its own unless `coach`.
        void PushAdvice(bool coach);
        /// The note of the coach: drops what no longer holds (and clears the ledger) and says whether it is up (`_coachShown`).
        void PushCoach(MahjongGame* game, const Key& key);
        /// The card of the coach: the note when it is up, else the advice in the mode Coach, else nothing; made again only when its key moved.
        void PushCard(MahjongGame* game, bool coach);
        /// Puts `card` into the variables and the elements of `CoachCard` and shows the plate while `open`.
        void PublishCard(const HintCard& card, bool open);
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

        bool _adviceOn = false;             ///< the switch `Hint`
        bool _adviceOnShown = false;        ///< the buttons are drawn `selected`
        bool _advised = false;              ///< the advice of the table of `_adviceKey` was worked out (it may have none)
        std::optional<HandAction> _advice;  ///< the advice worked out on the table of `_adviceKey`
        TableView _adviceView;              ///< the view it was asked on (the sentence names the pending discard of a window)
        Key _adviceKey;
        std::optional<Explanation> _explained;  ///< the reasons of `_advice`, asked on the same table
        uint64_t _adviceGeneration = 0;         ///< grows with every advice worked out, so the card knows a new one
        bool _adviceDirty = false;              ///< the sentence is to be published again (worked out again)
        bool _adviceShown = false;
        uint64_t _adviceStringsVersion = 0;

        bool _coachShown = false;               ///< the note is up (on the card)
        const MahjongGame* _coachOf = nullptr;  ///< the game whose note was last looked at
        uint64_t _coachSeen = 0;                ///< `CoachLedger::Version()` of the note last looked at
        CoachHandKey _coachHand;                ///< the hand that note was first seen in
        uint64_t _coachPublished = 0;           ///< the version of the note that is up

        /// What the card says: nothing, the advice of the table or the note of the ledger.
        enum class CardSource : uint8_t { None, Advice, Note };
        /// What the card depends on: when it is equal to the last published one, nothing is made again.
        struct CardKey {
            CardSource source = CardSource::None;
            uint64_t version = 0;  ///< `_adviceGeneration` or `CoachLedger::Version()`
            uint64_t strings = 0;  ///< `UIStrings::GetVersion()`
            CoachTally hand;
            CoachTally match;
            bool operator==(const CardKey&) const = default;
        };
        CardKey _cardKey;

        static MahjongHintsComponent* s_active;
    };

}  // namespace BECore::Mahjong
