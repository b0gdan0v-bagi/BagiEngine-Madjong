#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/ComputerExplain.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /**
     * What the hints of the table say (the node `Hints`): which tiles the viewer's hand waits for, which discards declare the calling
     * hand, and what the computer player would do in the viewer's place. Plain C++, no scene: everything is computed from the `TableView`
     * of the viewer (`MakeTableView`), so a hint never knows more than the viewer sees. The words are keys of `config/UIStrings.xml`;
     * the captions of the canvas are said by the scene itself, only the sentences of the advice are built here.
     */

    /// The tiles of the waiting row: the 13 orphans are the most a hand can wait for.
    inline constexpr int kMaxWaitingTiles = 13;

    /// One tile the hand waits for and the copies of it the viewer cannot see (it may be 0: a dead wait is still shown).
    struct WaitEntry {
        Tile tile;
        int unseen = 0;
    };

    /// The tiles that would complete the viewer's hand, in tile id order, each with its `UnseenCopies`. Empty unless the concealed tiles
    /// are `3n + 1` and the hand is waiting (`WinningTiles(view.own)`, melds counted). Independent of whose turn it is and of the
    /// declaration.
    [[nodiscard]] Vector<WaitEntry> WaitingTiles(const TableView& view);

    /// `true` for the id of each tile whose discard declares the calling hand: the `Discard` actions of `view.legal` with
    /// `declareCalling`, exactly what the `Declare` switch allows. All false when the viewer has no turn.
    [[nodiscard]] Array<bool, kTileIdCount> DeclaringDiscards(const TableView& view);

    /// What the computer player would do in the viewer's place: `ComputerPlayer(kAdviceSeed).Choose(view)` (`Game/ComputerExplain.h`), an
    /// element of `view.legal`; empty when that is empty.
    [[nodiscard]] Optional<HandAction> AdviceFor(const TableView& view);

    /// One sentence that says `advice` ("Discard 5 Dots."): one key `hints.advice.*` per kind with placeholders. `view` names the pending
    /// discard of a claim. Never empty.
    [[nodiscard]] String AdviceText(const UIStrings& strings, const TableView& view, const HandAction& advice);

    /// `AdviceFor(view)` with the numbers it was chosen by (`ExplainAdvice`); empty when there is nothing to do.
    [[nodiscard]] Optional<Explanation> ExplainAdviceFor(const TableView& view);

    /**
     * @brief Whether the hints are on screen now: `hintsOn` (`MahjongGame::HintsActive()`: the player's mode, unless a room forbids
     * them), the table is not paused behind a modal screen (`MahjongGame::IsTablePaused()`), the session is `Playing` with a viewer who is
     * a person (never a spectator), and no hot-seat cover is up. The one place of the rule; the components ask it every frame.
     */
    [[nodiscard]] bool HintsApply(bool hintsOn, bool tablePaused, const TableSession& session);

}  // namespace BECore::Mahjong
