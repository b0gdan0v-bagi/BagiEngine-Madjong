#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/ComputerView.h>

namespace BECore::Mahjong {

    /**
     * Which discards of the viewer's own turn keep a declared calling hand waiting. Plain C++, no scene: computed from the `TableView`
     * of the viewer (`MakeTableView`), so it knows no more than the viewer sees. Drawn (`kKeepsWaitingMark`) whatever the `Hints`
     * switch says.
     */

    /// `true` for the id of each tile whose discard (one copy) leaves the hand waiting (`IsWaiting`, melds counted), and only while the
    /// viewer's hand is declared and the viewer is to act in `Phase::Discard` with something to choose from. All false otherwise (before
    /// the declaration, off the viewer's turn, in a claim or robbing window, after the end). `DeclaringDiscards` (`Hints.h`) cannot say
    /// this: after the declaration `LegalActions` lists no discard with `declareCalling`.
    [[nodiscard]] Array<bool, kTileIdCount> KeepsWaitingDiscards(const TableView& view);

}  // namespace BECore::Mahjong
