#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/ComputerView.h>

namespace BECore::Mahjong {

    /**
     * The marks of a declared calling hand that are not about the hints: which discards of the viewer's own turn keep the declared hand
     * waiting. Plain C++, no scene: computed from the `TableView` of the viewer (`MakeTableView`), so it knows no more than the viewer
     * sees. The table draws a bar on those tiles (`kKeepsWaitingMark`) whatever the `Hints` switch says.
     */

    /// `true` for the id of each tile whose discard (one copy of it) leaves the concealed tiles waiting (`IsWaiting` of `HandShape.h`, the
    /// melds counted), and only while the viewer's hand is declared (`view.own.callingDeclared`), the viewer is to act in `Phase::Discard`
    /// (`view.currentSeat == view.seat`) and has something to choose from (`view.legal` not empty). All false otherwise: before the
    /// declaration, off the viewer's turn, in a claim or robbing window, for no seat, after the end. `DeclaringDiscards` (`Hints.h`) cannot
    /// say this: after the declaration `LegalActions` no longer lists a discard with `declareCalling`.
    [[nodiscard]] eastl::array<bool, kTileIdCount> KeepsWaitingDiscards(const TableView& view);

}  // namespace BECore::Mahjong
