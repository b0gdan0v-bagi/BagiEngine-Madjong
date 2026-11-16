#pragma once

#include <BECore/Scene/Components/PointerTargetComponent.h>
#include <Mahjong/Table/DangerMarks.h>
#include <Mahjong/Table/HintCard.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The hints of the table (`Table/Hints.h`): the node `Hints` (`config/nodes/MahjongHints.xml`) carries this component and the canvas it
     *        fills. It reads the game and the viewer's view and never changes the match. What each hint shows and when it applies:
     *        `src/Mahjong/Components/CLAUDE.md`, "The hints (`MahjongHintsComponent`)".
     *
     * **The switch.** `MahjongSetHints` (`off` | `hints` | `coach`) sets the mode of the game (`MahjongGame::GetHintsMode()`) at once, not at
     * `Start`; the component ignores it while `Options` is not open and any other id, and sets `hintsChoice` from the game, so the variable
     * keeps the truth. **In a room** the choice counts only where the host allowed hints (`MahjongGame::HintsActive()` / `CoachActive()`), and
     * every hint is worked out from the viewer's view of the display copy with the server's list of actions as `legal` and nothing else.
     *
     * **Zero allocations in a steady frame.** The waiting row is worked out from `MakeTableView(hand, viewer)` only when the session version,
     * the viewer or the rule `HintsApply` changed, and each frame pushes only what differs from the canvas; it also shows during the
     * computers' turns. The advice belongs to the table it was worked out on: a change of `Version()`, a cover, hints off or a pause drop
     * it, and a new language words it again. The room's line is made again only when its word or the language moved.
     *
     * **The marks** are drawn over the layout of the last draw of `MahjongTableComponent`, which draws first. `HintButton` is shown by name in
     * both the nodes `HintsButton` and `ActionsCompact` (the table component swaps the canvases) and is a **switch** (`MahjongHintAdvice`,
     * ignored when the button is not offered; its state is the component's and is not saved); `ExplainAdviceFor` names the same action as
     * `AdviceFor(view)`.
     *
     * **The card of the coach.** The ledger of the game (`MahjongGame::GetCoach()`) holds the note of the person's last differing move; while it
     * is up for this hand and viewer the card is the note (`MakeNoteCard`), else the advice (`MakeAdviceCard`). The note stays through the
     * computers' turns (they move within a second of the person's action). The card is published only when what it says moved (`CardKey`) and
     * every variable of it is emptied when it goes down. The component only reads the ledger and clears it. **It never acts:** it performs no
     * action, moves no version and the control API does not know it.
     *
     * **The pointer.** The component is a `PointerTargetComponent` with the whole output window as its shape, on the node `Hints`, which
     * is above the table, so it is asked first and only watches: a press clears a shown note or takes the advice down when it is on the card,
     * and a press or a motion is never taken, so both go on to the table (and to a canvas above, which took them before).
     */
    class MahjongHintsComponent : public PointerTargetComponent {
        BE_CLASS(MahjongHintsComponent)
    public:
        MahjongHintsComponent() = default;
        ~MahjongHintsComponent() override;

        static MahjongHintsComponent* GetActive() noexcept {
            return s_active;
        }

        /// The danger mark the hints draw on `tile` of the viewer's hand now (`DangerMarks`): `None` while no mark is drawn (hints off, no
        /// declared opponent, not the viewer's discard). The name plate of a hovered hand tile words it, so a bar is never unexplained.
        [[nodiscard]] DangerMark DangerOf(Tile tile) const noexcept {
            return _key.applies && tile.IsValid() ? _dangers[tile.Id()] : DangerMark::None;
        }

        /// The view of @p viewer on the table on screen, the one thing every hint is worked out from: `MakeTableView` of the displayed hand. A
        /// display hand lists no action, so in a room `legal` is the server's list and nothing else; none while the link is down.
        [[nodiscard]] static TableView ViewOf(const MahjongGame& game, Seat viewer);

    protected:
        void OnTargetAttached() override;
        void OnTargetDraw() override;
        bool OnPointerPress(const PointerEvent& event) override;
        bool OnPointerMove(const PointerEvent& event) override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);
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
        [[nodiscard]] static Optional<TableView> ViewToAdvise(const MahjongGame* game);

        void Refresh(const MahjongGame* game, const Key& key);
        void PushWaitingRow();
        void Advise(const MahjongGame& game, const Key& key);
        /// The button (offered, switched on) and the sentence of the advice, which is a label of its own unless `coach`.
        void PushAdvice(bool coach);
        /// The line `RoomHintsText` at the table of a room (allowed, allowed but the player's mode is Off, not allowed), while neither the sentence nor the card is up.
        void PushRoomHints(const MahjongGame* game);
        /// The note of the coach: drops what no longer holds (and clears the ledger) and says whether it is up (`_coachShown`).
        void PushCoach(MahjongGame* game, const Key& key);
        /// The card of the coach: the note when it is up, else the advice in the mode Coach, else nothing; made again only when its key moved.
        void PushCard(MahjongGame* game, bool coach);
        void PublishCard(const HintCard& card, bool open);
        void DropAdvice();
        void DrawMarks(const MahjongGame& game) const;

        Vector<WaitEntry> _row;                      ///< the row as it should be on screen
        Vector<WaitEntry> _published;                ///< the row as it is on the canvas
        Array<bool, kTileIdCount> _declaring{};      ///< the tiles whose discard declares the calling hand
        Array<DangerMark, kTileIdCount> _dangers{};  ///< the danger of each tile of the hand while an opponent has declared
        bool _offered = false;                       ///< the button is offered: hints apply and the viewer has actions
        bool _offeredShown = false;
        bool _known = false;  ///< `_key` and what follows from it are worked out
        Key _key;

        bool _adviceOn = false;        ///< the switch `Hint`
        bool _adviceOnShown = false;   ///< the buttons are drawn `selected`
        bool _advised = false;         ///< the advice of the table of `_adviceKey` was worked out (it may have none)
        Optional<HandAction> _advice;  ///< the advice worked out on the table of `_adviceKey`
        TableView _adviceView;         ///< the view it was asked on (the sentence names the pending discard of a window)
        Key _adviceKey;
        Optional<Explanation> _explained;  ///< the reasons of `_advice`, asked on the same table
        uint64_t _adviceGeneration = 0;    ///< grows with every advice worked out, so the card knows a new one
        bool _adviceDirty = false;         ///< the sentence is to be published again (worked out again)
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

        /// What the line of the room says; `None` hides it.
        enum class RoomHintsWord : uint8_t { None, Allowed, AllowedOff, NotAllowed };
        RoomHintsWord _roomWord = RoomHintsWord::None;  ///< the word on the canvas
        uint64_t _roomStringsVersion = 0;               ///< `UIStrings::GetVersion()` it was worded with

        static MahjongHintsComponent* s_active;
    };

}  // namespace BECore::Mahjong
