#pragma once

#include <EASTL/array.h>
#include <EASTL/string_view.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Table/UiPalette.h>

namespace BECore::Mahjong {

    /**
     * The danger marks of the hints: how risky each tile of the viewer's hand is to discard while an opponent has declared a calling
     * hand (`Game/DiscardDanger.h` says what a grade does and does not mean). Plain C++, no scene: computed from the `TableView` of the
     * viewer (`MakeTableView`), so it knows no more than the viewer sees. Drawn (`kSafeMark`, `kRiskyMark`, `kDangerMark`) only while
     * the `Hints` switch is on.
     */
    CORE_ENUM(DangerMark, uint8_t, None, Safe, Risky, Dangerous)

    /// The mark of each tile kind the viewer holds: `Safe`, `Risky` for `Danger::Medium`, `Dangerous` for `High`; `None` for `Low` (the
    /// ordinary case is not worth a mark) and for kinds not in the concealed hand. All `None` unless an opponent has declared
    /// (`HasDeclaredOpponent`) and the viewer is to act in `Phase::Discard` with something to choose from: elsewhere there is no discard
    /// to mark.
    [[nodiscard]] Array<DangerMark, kTileIdCount> DangerMarks(const TableView& view);

    /// The colour of the bar of `mark`; `None` has no bar and gets `kDangerMark`.
    [[nodiscard]] Color DangerMarkColor(DangerMark mark) noexcept;

    /// The key `hints.hover.*` of the line under a hovered hand tile that says what the bar of `mark` means; empty for `None`.
    [[nodiscard]] StringView DangerHoverKey(DangerMark mark) noexcept;

}  // namespace BECore::Mahjong
