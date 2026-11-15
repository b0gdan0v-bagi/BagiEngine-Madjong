#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * The name of a tile under the mouse pointer: which slot lies under a point, which tile it shows, where the plate goes. The table
     * component runs them each frame on the layout it has just built, so the answer never indexes a hand that changed after it.
     */

    /// The topmost slot whose screen `rect` contains the point (x, y); null when none. Slots are drawn in layout order, so the last match
    /// is on top (squeezed rows overlap). A wall stack is never returned: it has no name. A selected tile's region stays the unraised
    /// slot, as in `HitHandTile`.
    [[nodiscard]] const TileSlot* SlotAt(const TableLayout& layout, float x, float y) noexcept;

    /// The tile `slot` shows face up, none for a back, a wall stack and an index outside the model. It is the draw's own rule
    /// (`drawSlot` of `MahjongTableComponent`; `seatFaceUp` is `Faces::Shows(seat)`), so the hover reads what the draw puts face up and
    /// nothing else: it cannot peek.
    [[nodiscard]] Optional<Tile> TileOfSlot(const HandModel& hand, const TileSlot& slot, bool seatFaceUp) noexcept;

    /// Where the name plate of a tile goes: the engine's rule for the tooltip of a button (`UITooltip::Place`), applied to a tile.
    [[nodiscard]] Rect HoverPlateRect(const Rect& tile, float plateWidth, float plateHeight, float viewWidth, float viewHeight) noexcept;

}  // namespace BECore::Mahjong
