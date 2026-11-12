#pragma once

#include <EASTL/array.h>
#include <EASTL/string_view.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Table/UiPalette.h>

namespace BECore::Mahjong {

    /**
     * The danger marks of the hints: how risky each tile of the viewer's hand is to discard while an opponent has declared a calling
     * hand (the reading of `Game/DiscardDanger.h`, which says what a grade does and does not mean). Plain C++, no scene: computed from
     * the `TableView` of the viewer (`MakeTableView`), so it knows no more than the viewer sees. The hints draw a bar inside the left
     * edge of each marked tile (`kSafeMark`, `kRiskyMark`, `kDangerMark`), only while the `Hints` switch is on.
     */
    CORE_ENUM(DangerMark, uint8_t, None, Safe, Risky, Dangerous)

    /// The mark of the kind of each tile the viewer holds: `Safe` for `Danger::Safe`, `Risky` for `Medium`, `Dangerous` for `High`, `None`
    /// for `Low` (the ordinary case is not worth a mark) and for every kind not in the concealed hand. All `None` unless some opponent has
    /// declared (`HasDeclaredOpponent`), the viewer is to act in `Phase::Discard` (`view.currentSeat == view.seat`) and has something to
    /// choose from (`view.legal` not empty): off the viewer's turn, in a claim or robbing window and after the end there is no discard to mark.
    [[nodiscard]] eastl::array<DangerMark, kTileIdCount> DangerMarks(const TableView& view);

    /// The colour of the bar of `mark` (`kSafeMark`, `kRiskyMark`, `kDangerMark`; `None` has no bar and gets `kDangerMark`).
    [[nodiscard]] Color DangerMarkColor(DangerMark mark) noexcept;

    /// The key of the line that says what the bar of `mark` means (`hints.hover.*`), worded under the name of a hovered hand tile; empty for
    /// `None`.
    [[nodiscard]] eastl::string_view DangerHoverKey(DangerMark mark) noexcept;

}  // namespace BECore::Mahjong
